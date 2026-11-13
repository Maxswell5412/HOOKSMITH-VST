#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>

// -----------------------------------------------------------------------------
// Tempo-synced arpeggiator. Swallows the notes you hold and plays them back
// one at a time on the beat grid (straight, triplet or dotted), into the
// hosted instrument. Everything here runs on the audio thread, no allocations.
// -----------------------------------------------------------------------------
namespace hs
{
class Arpeggiator
{
public:
    enum Mode { Up = 0, Down, UpDown, Random, AsPlayed };

    static juce::StringArray modeNames()   { return { "Up", "Down", "Up/Down", "Random", "As Played" }; }

    Arpeggiator()
    {
        order.ensureStorageAllocated (128);
        seqNote.ensureStorageAllocated (128 * 4);
        seqVel.ensureStorageAllocated (128 * 4);
    }

    // Returns true if the message was a note the arp took over.
    bool handle (const juce::MidiMessage& m)
    {
        if (m.isNoteOn())
        {
            const int n = m.getNoteNumber();
            if (vel[(size_t) n] == 0)
                order.add (n);
            vel[(size_t) n] = (juce::uint8) juce::jmax (1, (int) m.getVelocity());
            channel = m.getChannel();
            if (order.size() == 1)
                stepCounter = 0;   // a fresh chord always starts at the beginning of the pattern
            return true;
        }
        if (m.isNoteOff())
        {
            const int n = m.getNoteNumber();
            vel[(size_t) n] = 0;
            order.removeFirstMatchingValue (n);
            return true;
        }
        if (m.isAllNotesOff() || m.isAllSoundOff())
            clearHeld();
        return false;
    }

    void process (juce::MidiBuffer& out, double startPpq, int numSamples, double beatsPerSample,
                  double rateBeats, int mode, int octaves, float gate)
    {
        if (numSamples <= 0 || beatsPerSample <= 0.0 || rateBeats <= 0.0)
            return;

        const double endPpq = startPpq + numSamples * beatsPerSample;
        auto posOf = [&] (double p)
        {
            return juce::jlimit (0, numSamples - 1, (int) ((p - startPpq) / beatsPerSample));
        };

        // Transport jumped (loop / relocate): don't leave a note hanging.
        if (sounding >= 0 && (offPpq < startPpq - 1.0e-6 || offPpq > startPpq + rateBeats + 1.0e-6))
            noteOff (out, 0);

        auto k = (juce::int64) std::ceil (startPpq / rateBeats - 1.0e-9);

        for (;;)
        {
            const double boundary = (double) k * rateBeats;

            if (sounding >= 0 && offPpq < endPpq && offPpq <= boundary)
            {
                noteOff (out, posOf (offPpq));
                continue;
            }
            if (boundary >= endPpq)
                break;

            if (sounding >= 0)
                noteOff (out, posOf (boundary));

            if (! order.isEmpty())
            {
                int note = 0, velocity = 100;
                nextNote (mode, octaves, note, velocity);
                out.addEvent (juce::MidiMessage::noteOn (channel, note, (juce::uint8) velocity), posOf (boundary));
                sounding = note;
                offPpq = boundary + rateBeats * juce::jlimit (0.05f, 1.0f, gate);
                ++stepsPlayed;
            }
            ++k;
        }
    }

    // Stop the current note (arp switched off).
    void stop (juce::MidiBuffer& out, int samplePos)
    {
        if (sounding >= 0)
            noteOff (out, samplePos);
    }

    void clearHeld()
    {
        vel.fill (0);
        order.clearQuick();
        stepCounter = 0;
    }

    int getSoundingNote() const noexcept   { return sounding; }
    juce::uint32 getStepsPlayed() const noexcept { return stepsPlayed; }

private:
    void noteOff (juce::MidiBuffer& out, int pos)
    {
        out.addEvent (juce::MidiMessage::noteOff (channel, sounding), pos);
        sounding = -1;
    }

    void nextNote (int mode, int octaves, int& note, int& velocity)
    {
        seqNote.clearQuick();
        seqVel.clearQuick();

        // Base order: pitch order, or the order you pressed the keys.
        std::array<int, 128> base {};
        int count = 0;
        if (mode == AsPlayed)
        {
            for (int n : order) base[(size_t) count++] = n;
        }
        else
        {
            for (int n = 0; n < 128; ++n)
                if (vel[(size_t) n] > 0) base[(size_t) count++] = n;
        }

        octaves = juce::jlimit (1, 4, octaves);
        for (int o = 0; o < octaves; ++o)
            for (int i = 0; i < count; ++i)
            {
                const int n = base[(size_t) i] + 12 * o;
                if (n <= 127)
                {
                    seqNote.add (n);
                    seqVel.add (vel[(size_t) base[(size_t) i]]);
                }
            }

        const int size = seqNote.size();
        if (size == 0) { note = 60; velocity = 0; return; }

        int idx = 0;
        switch (mode)
        {
            case Down:   idx = size - 1 - (stepCounter % size); break;
            case UpDown:
            {
                const int period = size > 1 ? 2 * size - 2 : 1;
                const int s = stepCounter % period;
                idx = s < size ? s : period - s;
                break;
            }
            case Random: idx = rng.nextInt (size); break;
            case Up:
            case AsPlayed:
            default:     idx = stepCounter % size; break;
        }

        ++stepCounter;
        note = seqNote.getUnchecked (idx);
        velocity = seqVel.getUnchecked (idx);
    }

    std::array<juce::uint8, 128> vel {};
    juce::Array<int> order, seqNote, seqVel;
    juce::Random rng;
    int channel = 1;
    int sounding = -1;
    double offPpq = 0.0;
    int stepCounter = 0;
    juce::uint32 stepsPlayed = 0;
};
} // namespace hs
