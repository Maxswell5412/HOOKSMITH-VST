#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <array>

// -----------------------------------------------------------------------------
// PART TONE (one per part, after COLOR):
//   HIGH PASS             - 12 dB/oct state-variable filter
//   LOW PASS              - a Moog-style 4-pole ladder (24 dB/oct) with RES and DRIVE, the classic
//                           Minimoog bass sound; WOBBLE sweeps it in time with the DAW
//   GAIN                  - middle = unity. Left turns it down cleanly. Right pushes the part
//                           into a bus compressor in the style of the classic console bus comp:
//                           4:1, 10 ms attack, auto release, soft knee, linked stereo, with a
//                           little console saturation. The louder you push, the more it glues.
// Settings glide so knobs never click.
// -----------------------------------------------------------------------------
namespace hs
{
struct PartToneSettings
{
    float hp = 0.0f;     // 0 = off .. 1 = 1.5 kHz
    float lp = 1.0f;     // 1 = off .. 0 = 300 Hz   (a Moog-style 24 dB ladder)
    float res = 0.15f;   // resonance for both filters (the ladder sings near the top)
    float gain = 0.5f;   // 0 .. 1, 0.5 = unity
    // 808 section (bass)
    float drive = 0.0f;        // 0..1: pushes the ladder's input like an overdriven Minimoog
    double wobbleBeats = 0.0;  // 0 = off, else the LFO period in beats (0.5 = 1/8 ...)
    float wobble = 0.0f;       // wobble depth 0..1 (the cutoff sweeps down and back up)
};

class PartTone
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        for (auto& c : ch) c = Ch {};
        env = 0.0f; envSlow = 0.0f; first = true;
        atk = 1.0f - std::exp (-1.0f / (0.010f * (float) sr));
        relFast = 1.0f - std::exp (-1.0f / (0.10f * (float) sr));
        relSlow = 1.0f - std::exp (-1.0f / (0.60f * (float) sr));
        glide = 1.0f - std::exp (-1.0f / (0.03f * (float) sr));
    }

    static float hpHz (float k) { return 20.0f * std::pow (75.0f, k); }    // 20 Hz .. 1.5 kHz
    static float lpHz (float k) { return 300.0f * std::pow (66.0f, k); }   // 300 Hz .. ~20 kHz
    float gainReductionDb() const noexcept { return grDb; }

    // beatPos = the DAW position (in beats) at the first sample, beatsPerSample = tempo
    void process (float* L, float* R, int n, const PartToneSettings& t, double beatPos = 0.0, double beatsPerSample = 0.0)
    {
        if (first) { sm = t; first = false; updateFilters (1.0f); }
        const bool wob = t.wobbleBeats > 0.0 && t.wobble > 0.001f;
        const bool active = t.hp > 0.002f || t.lp < 0.998f || std::abs (t.gain - 0.5f) > 0.002f || t.drive > 0.002f || wob
                         || sm.hp > 0.002f || sm.lp < 0.998f || std::abs (sm.gain - 0.5f) > 0.002f || sm.drive > 0.002f || sm.wobble > 0.002f;
        if (! active) { grDb = 0.0f; return; }
        for (int i = 0; i < n; ++i)
        {
            sm.hp += (t.hp - sm.hp) * glide; sm.lp += (t.lp - sm.lp) * glide; sm.gain += (t.gain - sm.gain) * glide;
            sm.res += (t.res - sm.res) * glide; sm.drive += (t.drive - sm.drive) * glide;
            sm.wobble += ((wob ? t.wobble : 0.0f) - sm.wobble) * glide;
            if ((i & 7) == 0)
            {
                // WOBBLE: the cutoff dips down and swings back up once per LFO period, locked to the beat
                float mod = 1.0f;
                if (sm.wobble > 0.001f && t.wobbleBeats > 0.0)
                {
                    const double ph = std::fmod ((beatPos + i * beatsPerSample) / t.wobbleBeats, 1.0);
                    const float lfo = 0.5f + 0.5f * (float) std::cos (juce::MathConstants<double>::twoPi * ph);   // 1 on the beat
                    mod = std::pow (2.0f, -6.0f * sm.wobble * (1.0f - lfo));                                    // up to 6 octaves down
                }
                updateFilters (mod);
            }
            float x[2] = { L[i], R[i] };
            const bool ladderOn = sm.lp < 0.998f || sm.wobble > 0.001f || sm.drive > 0.002f;
            for (int c = 0; c < 2; ++c)
            {
                if (sm.hp > 0.002f) x[c] = ch[(size_t) c].hp.hpf (x[c], hpC);
                if (ladderOn) x[c] = ch[(size_t) c].lad.process (x[c], ladG, sm.res, 1.0f + sm.drive * 6.0f);
            }
            const float g = sm.gain;
            if (g <= 0.5f)
            {
                const float lin = g <= 0.0f ? 0.0f : std::pow (10.0f, (g - 0.5f) * 2.0f * 24.0f / 20.0f);   // down to -24 dB, then off
                x[0] *= lin; x[1] *= lin;
                grDb = 0.0f;
            }
            else
            {
                // push: up to +18 dB into the bus comp
                const float push = (g - 0.5f) * 2.0f;
                const float in = std::pow (10.0f, push * 18.0f / 20.0f);
                x[0] *= in; x[1] *= in;
                const float level = std::max (std::abs (x[0]), std::abs (x[1]));
                // auto release: quick for transients, slow for sustained level (program dependent)
                env += (level - env) * (level > env ? atk : relFast);
                envSlow += (level - envSlow) * (level > envSlow ? atk * 0.3f : relSlow);
                const float det = std::max (env * 0.7f, envSlow);
                const float lvl = 20.0f * std::log10 (det + 1.0e-6f);
                const float thr = -10.0f, ratio = 4.0f, knee = 6.0f;
                float over = lvl - thr, red = 0.0f;
                if (over > knee * 0.5f) red = over - over / ratio;
                else if (over > -knee * 0.5f) { const float k = over + knee * 0.5f; red = (1.0f / ratio - 1.0f) * -0.5f * k * k / knee; }
                grDb = red;
                const float makeup = push * 5.0f;                                 // gets louder, but held in
                const float gl = std::pow (10.0f, (makeup - red) / 20.0f);
                for (auto& v : x)
                {
                    v *= gl;
                    // console saturation: soft, a touch asymmetric (even harmonics), more as you push
                    const float drv = 1.0f + push * 1.5f;
                    v = std::tanh (v * drv + 0.04f * push * v * v) / drv;
                }
            }
            L[i] = x[0]; R[i] = x[1];
        }
    }

private:
    struct Co { float a1 = 0, a2 = 0, a3 = 0, k = 1.414f; };
    struct Svf
    {
        float s1 = 0, s2 = 0;
        void tick (float v0, const Co& c, float& lp, float& bp)
        {
            const float v3 = v0 - s2, v1 = c.a1 * s1 + c.a2 * v3, v2 = s2 + c.a2 * s1 + c.a3 * v3;
            s1 = 2.0f * v1 - s1; s2 = 2.0f * v2 - s2; lp = v2; bp = v1;
        }
        float lpf (float v, const Co& c) { float lp, bp; tick (v, c, lp, bp); return lp; }
        float hpf (float v, const Co& c) { float lp, bp; tick (v, c, lp, bp); return v - c.k * bp - lp; }
    };
    // Moog-style transistor ladder: 4 one-pole stages with tanh saturation and resonance feedback
    struct Ladder
    {
        float s[4] {};
        float process (float x, float g, float res, float drive)
        {
            const float k = 3.9f * std::clamp (res, 0.0f, 1.0f);           // 4 = self-oscillation
            const float in = std::tanh (x * drive - k * s[3]);
            s[0] += g * (in - s[0]);
            s[1] += g * (std::tanh (s[0]) - s[1]);
            s[2] += g * (std::tanh (s[1]) - s[2]);
            s[3] += g * (std::tanh (s[2]) - s[3]);
            const float makeup = (1.0f + 0.6f * k) / std::max (1.0f, 0.6f * drive + 0.4f);   // keep the level steady
            return s[3] * makeup;
        }
    };
    struct Ch { Svf hp; Ladder lad; };

    static Co make (double sr, double hz, double q = 0.707)
    {
        Co c;
        const double g = std::tan (juce::MathConstants<double>::pi * std::min (hz, sr * 0.45) / sr);
        c.k = (float) (1.0 / q);
        c.a1 = (float) (1.0 / (1.0 + g * (g + c.k)));
        c.a2 = (float) g * c.a1;
        c.a3 = (float) g * c.a2;
        return c;
    }
    void updateFilters (float lpMod)
    {
        hpC = make (sr, hpHz (sm.hp), 0.707 + 2.0 * sm.res);
        // the wobble sweeps from the cutoff (or from 9 kHz when the LOW PASS is open)
        const double base = sm.lp >= 0.998f ? (sm.wobble > 0.001f ? 9000.0 : 20000.0) : (double) lpHz (sm.lp);
        const double fc = std::clamp (base * (double) lpMod, 30.0, sr * 0.45);
        ladG = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * fc / sr));
    }

    double sr = 44100.0;
    std::array<Ch, 2> ch {};
    Co hpC;
    float ladG = 1.0f;
    PartToneSettings sm;
    bool first = true;
    float env = 0, envSlow = 0, atk = 0.01f, relFast = 0.001f, relSlow = 0.0001f, glide = 0.001f, grDb = 0;
};
} // namespace hs
