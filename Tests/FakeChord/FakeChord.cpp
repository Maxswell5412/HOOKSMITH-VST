// A stand-in for Cthulhu in the tests: every key outputs a chord (minor triad on black keys,
// major 7th on white keys, rooted on the key), like a chord-memory preset.
#include <juce_audio_processors/juce_audio_processors.h>

class FakeChord : public juce::AudioProcessor
{
public:
    FakeChord() : AudioProcessor (BusesProperties().withOutput ("Out", juce::AudioChannelSet::stereo(), true)) {}
    const juce::String getName() const override { return "FakeChord"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer& midi) override
    {
        b.clear();
        juce::MidiBuffer out;
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (! m.isNoteOnOrOff()) continue;
            const int r = m.getNoteNumber();
            const bool black = juce::MidiMessage::isMidiNoteBlack (r);
            std::vector<int> ivs = black ? std::vector<int> { 0, 3, 7 } : std::vector<int> { 0, 4, 7, 11 };
            for (int iv : ivs)
                out.addEvent (m.isNoteOn() ? juce::MidiMessage::noteOn (1, r + iv, m.getVelocity()) : juce::MidiMessage::noteOff (1, r + iv), meta.samplePosition);
        }
        midi.swapWith (out);
    }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
};

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FakeChord(); }
