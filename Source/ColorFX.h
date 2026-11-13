#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>
#include <vector>

// -----------------------------------------------------------------------------
// COLOR: a retro "colour" strip for each part, in the spirit of RC-20.
//   DRIVE  tape-style saturation (warm, slightly asymmetric)
//   WOBBLE wow and flutter (slow + fast pitch drift)
//   NOISE  tape hiss + vinyl crackle
//   CRUSH  bit depth and sample-rate reduction
//   TONE   rolls off the top end (worn tape / old sampler)
//   MIX    dry / coloured blend
// TAPE STOP: slows the whole output down to a stop, then spins back up.
// Allocation-free once prepared.
// -----------------------------------------------------------------------------
namespace hs
{
struct ColorSettings
{
    bool on = false;
    float drive = 0, wobble = 0, noise = 0, crush = 0, tone = 0, mix = 1;
};

class ColorFX
{
public:
    void prepare (double sampleRate, juce::uint32 seed)
    {
        sr = sampleRate;
        const int len = juce::nextPowerOfTwo ((int) (sr * 0.04) + 4);
        for (auto& d : delay) d.assign ((size_t) len, 0.0f);
        mask = len - 1;
        writePos = 0;
        rng = seed | 1u;
        reset();
    }

    void reset()
    {
        for (auto& d : delay) std::fill (d.begin(), d.end(), 0.0f);
        lp1 = lp2 = { 0, 0 };
        dcX = dcY = { 0, 0 };
        holdCount = 0; held = { 0, 0 };
        hissLp = { 0, 0 };
        crackle = 0;
        wowPhase = flutterPhase = 0;
        drift = driftTarget = 0;
        driftCount = 0;
    }

    void process (float* L, float* R, int n, const ColorSettings& s)
    {
        if (! s.on || delay[0].empty()) return;

        const float drive = s.drive, wob = s.wobble, mix = s.mix;
        const float g = 1.0f + drive * drive * 14.0f;
        const float makeup = 1.0f / (1.0f + 0.75f * (g - 1.0f));   // keeps the level about the same as you turn it up
        const float bias = 0.12f * drive;
        const float biasOut = std::tanh (bias);

        const float cutoff = 20000.0f * std::pow (0.06f, s.tone);
        const float lpA = 1.0f - std::exp (-juce::MathConstants<float>::twoPi * juce::jmin (cutoff, (float) sr * 0.45f) / (float) sr);

        const int holdLen = 1 + (int) (s.crush * s.crush * 15.0f);
        const float levels = std::pow (2.0f, 16.0f - 12.0f * s.crush);

        const float hissLevel = s.noise * s.noise * 0.02f;
        const float crackleRate = s.noise * 18.0f / (float) sr;

        const double wowInc = juce::MathConstants<double>::twoPi * 0.55 / sr;
        const double flutInc = juce::MathConstants<double>::twoPi * 6.3 / sr;
        const float baseDelay = wob * 0.0045f * (float) sr;   // samples

        for (int i = 0; i < n; ++i)
        {
            float x[2] = { L[i], R != nullptr ? R[i] : L[i] };
            const float dry[2] = { x[0], x[1] };

            // ---- wobble (modulated delay)
            if (wob > 0.001f)
            {
                if (--driftCount <= 0) { driftTarget = rand01() * 2.0f - 1.0f; driftCount = (int) (sr * 0.25); }
                drift += (driftTarget - drift) * 0.00002f;
                wowPhase += wowInc; flutterPhase += flutInc;
                if (wowPhase > juce::MathConstants<double>::twoPi) wowPhase -= juce::MathConstants<double>::twoPi;
                if (flutterPhase > juce::MathConstants<double>::twoPi) flutterPhase -= juce::MathConstants<double>::twoPi;
                const float mod = (float) std::sin (wowPhase) * 0.7f + (float) std::sin (flutterPhase) * 0.12f + drift * 0.25f;
                const float d = baseDelay * (1.0f + 0.8f * mod) + 2.0f;
                for (int c = 0; c < 2; ++c)
                {
                    delay[(size_t) c][(size_t) writePos] = x[c];
                    x[c] = readDelay (c, d);
                }
                writePos = (writePos + 1) & mask;
            }

            for (int c = 0; c < 2; ++c)
            {
                float v = x[c];

                // ---- drive: asymmetric tape saturation + DC blocker
                if (drive > 0.001f)
                {
                    v = (std::tanh (g * v + bias) - biasOut) * makeup;
                    const float y = v - dcX[(size_t) c] + 0.995f * dcY[(size_t) c];
                    dcX[(size_t) c] = v; dcY[(size_t) c] = y;
                    v = y;
                }

                // ---- crush: sample & hold + bit reduction
                if (s.crush > 0.005f)
                {
                    if (c == 0 && --holdCount <= 0) holdCount = holdLen;
                    if (holdCount == holdLen) held[(size_t) c] = std::round (v * levels) / levels;
                    v = held[(size_t) c];
                }

                // ---- tone: 12 dB/oct low-pass
                if (s.tone > 0.001f)
                {
                    lp1[(size_t) c] += lpA * (v - lp1[(size_t) c]);
                    lp2[(size_t) c] += lpA * (lp1[(size_t) c] - lp2[(size_t) c]);
                    v = lp2[(size_t) c];
                }

                x[c] = v;
            }

            // ---- noise: hiss + crackle
            if (hissLevel > 0.0f)
            {
                if (rand01() < crackleRate) crackle = (rand01() * 0.6f + 0.4f) * s.noise * 0.25f * (rand01() < 0.5f ? -1.0f : 1.0f);
                for (int c = 0; c < 2; ++c)
                {
                    hissLp[(size_t) c] += 0.35f * ((rand01() * 2.0f - 1.0f) - hissLp[(size_t) c]);
                    x[c] += hissLp[(size_t) c] * hissLevel + crackle;
                }
                crackle *= 0.6f;
            }

            L[i] = dry[0] + (x[0] - dry[0]) * mix;
            if (R != nullptr) R[i] = dry[1] + (x[1] - dry[1]) * mix;
        }
    }

private:
    float rand01()
    {
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        return (float) (rng & 0xffffff) / 16777216.0f;
    }

    float readDelay (int c, float d) const
    {
        const auto& buf = delay[(size_t) c];
        const float pos = (float) writePos - d;
        const int i0 = (int) std::floor (pos);
        const float f = pos - (float) i0;
        auto at = [&] (int k) { return buf[(size_t) (k & mask)]; };
        const float xm1 = at (i0 - 1), x0 = at (i0), x1 = at (i0 + 1), x2 = at (i0 + 2);
        const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2,
                    c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
        return ((c3 * f + c2) * f + c1) * f + x0;
    }

    double sr = 44100.0;
    std::array<std::vector<float>, 2> delay;
    int mask = 0, writePos = 0;
    juce::uint32 rng = 1;
    std::array<float, 2> lp1 {}, lp2 {}, dcX {}, dcY {}, held {}, hissLp {};
    int holdCount = 0;
    float crackle = 0;
    double wowPhase = 0, flutterPhase = 0;
    float drift = 0, driftTarget = 0;
    int driftCount = 0;
};

// -----------------------------------------------------------------------------
class TapeStop
{
public:
    enum State { Live, Stopping, Stopped, Starting, Rejoin };

    void prepare (double sampleRate)
    {
        sr = sampleRate;
        const int len = juce::nextPowerOfTwo ((int) (sr * 8.0));
        for (auto& b : ring) b.assign ((size_t) len, 0.0f);
        mask = len - 1;
        writePos = 0;
        state = Live;
        lp = { 0, 0 };
    }

    State getState() const { return state; }

    void process (float* L, float* R, int n, bool wantStop, float stopSeconds, float startSeconds)
    {
        if (ring[0].empty()) return;
        const double stopInc = 1.0 / juce::jmax (1.0, stopSeconds * sr);
        const double startInc = 1.0 / juce::jmax (1.0, startSeconds * sr);
        const int fadeLen = (int) (sr * 0.02);

        for (int i = 0; i < n; ++i)
        {
            const float in[2] = { L[i], R != nullptr ? R[i] : L[i] };
            ring[0][(size_t) writePos] = in[0];
            ring[1][(size_t) writePos] = in[1];

            switch (state)
            {
                case Live:
                    if (wantStop) { state = Stopping; progress = 0.0; readPos = (double) writePos; lp = { in[0], in[1] }; }
                    break;
                case Stopping:
                    if (! wantStop) { state = Starting; progress = rate; }
                    break;
                case Stopped:
                    if (! wantStop) { state = Starting; progress = 0.0; readPos = (double) writePos - 1.0; }
                    break;
                case Starting:
                    if (wantStop) { state = Stopping; progress = 1.0 - rate; }
                    break;
                case Rejoin:
                    if (wantStop) { state = Stopping; progress = 0.0; readPos = (double) writePos; }
                    break;
            }

            float out[2] = { in[0], in[1] };
            if (state == Stopping)
            {
                progress += stopInc;
                const double t = juce::jmin (1.0, progress);
                rate = std::pow (1.0 - t, 1.6);             // the motor winds down
                if (t >= 1.0) { state = Stopped; rate = 0.0; }
                tapeRead (out);
            }
            else if (state == Stopped)
            {
                out[0] = out[1] = 0.0f;
            }
            else if (state == Starting)
            {
                progress += startInc;
                rate = std::pow (juce::jmin (1.0, progress), 0.8);
                tapeRead (out);
                if (progress >= 1.0) { state = Rejoin; fade = 0; }
            }
            else if (state == Rejoin)
            {
                // crossfade from the tape copy back to the live signal
                float tape[2]; rate = 1.0; tapeRead (tape);
                const float a = (float) fade / (float) fadeLen;
                out[0] = tape[0] + (in[0] - tape[0]) * a;
                out[1] = tape[1] + (in[1] - tape[1]) * a;
                if (++fade >= fadeLen) state = Live;
            }

            L[i] = out[0];
            if (R != nullptr) R[i] = out[1];
            writePos = (writePos + 1) & mask;
        }
    }

private:
    void tapeRead (float* out)
    {
        readPos += rate;
        const int i0 = (int) std::floor (readPos);
        const float f = (float) (readPos - i0);
        // darker as it slows, like a real tape machine
        const float a = (float) juce::jlimit (0.02, 1.0, rate * 1.2);
        for (int c = 0; c < 2; ++c)
        {
            const auto& b = ring[(size_t) c];
            const float v = b[(size_t) (i0 & mask)] + (b[(size_t) ((i0 + 1) & mask)] - b[(size_t) (i0 & mask)]) * f;
            lp[(size_t) c] += a * (v - lp[(size_t) c]);
            out[c] = lp[(size_t) c] * (float) juce::jmin (1.0, rate * 4.0 + 0.0);
        }
    }

    double sr = 44100.0;
    std::array<std::vector<float>, 2> ring;
    int mask = 0, writePos = 0, fade = 0;
    double readPos = 0, progress = 0, rate = 1.0;
    std::array<float, 2> lp {};
    State state = Live;
};
} // namespace hs
