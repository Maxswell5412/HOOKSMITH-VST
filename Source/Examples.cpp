#include "Examples.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <string>
#include <cmath>
#include <complex>
#include <numeric>

namespace hs
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

// --- a gentle low-pass, then keep every d-th sample (~11 kHz is plenty for pitch and chroma)
std::vector<float> decimate (const float* x, int n, double sr, double& outSr)
{
    const int d = std::max (1, (int) std::lround (sr / 11025.0));
    outSr = sr / d;
    std::vector<float> y;
    y.reserve ((size_t) (n / d + 1));
    const double fc = 0.42 * outSr * 0.5;
    const float a = (float) (1.0 - std::exp (-2.0 * kPi * fc / sr));
    float s1 = 0, s2 = 0, s3 = 0;
    for (int i = 0; i < n; ++i)
    {
        s1 += a * (x[i] - s1); s2 += a * (s1 - s2); s3 += a * (s2 - s3);
        if (i % d == 0) y.push_back (s3);
    }
    return y;
}

void fft (std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * kPi / (double) len;
        const std::complex<double> wl (std::cos (ang), std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w (1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

double median (std::vector<double> v)
{
    if (v.empty()) return 0;
    std::nth_element (v.begin(), v.begin() + (long) v.size() / 2, v.end());
    return v[v.size() / 2];
}

std::string lower (std::string s) { for (auto& c : s) c = (char) std::tolower ((unsigned char) c); return s; }
} // namespace

double bpmFromName (const std::string& name)
{
    const auto s = lower (name);
    for (size_t i = 0; i < s.size(); ++i)
    {
        if (! std::isdigit ((unsigned char) s[i]) || (i > 0 && std::isdigit ((unsigned char) s[i - 1]))) continue;
        size_t j = i;
        while (j < s.size() && (std::isdigit ((unsigned char) s[j]) || s[j] == '.')) ++j;
        const double v = std::atof (s.substr (i, j - i).c_str());
        size_t k = j;
        while (k < s.size() && (s[k] == ' ' || s[k] == '_' || s[k] == '-')) ++k;
        const bool after = s.compare (k, 3, "bpm") == 0;
        size_t b = i;
        while (b > 0 && (s[b - 1] == ' ' || s[b - 1] == '_' || s[b - 1] == '-')) --b;
        const bool before = b >= 3 && s.compare (b - 3, 3, "bpm") == 0;
        if ((after || before) && v >= 50 && v <= 220) return v;
    }
    return 0;
}

double settleTempo (double seconds, double bpmHint, double& lengthBeats)
{
    lengthBeats = 16;
    if (seconds <= 0.05) return bpmHint > 0 ? bpmHint : 120.0;
    if (bpmHint > 0)
    {
        const double bars = seconds * bpmHint / 240.0;
        const double rb = std::max (1.0, std::round (bars));
        if (std::abs (bars - rb) < 0.15 * rb || bars < 1.0)
        {
            lengthBeats = rb * 4.0;
            return bars < 1.0 ? bpmHint : rb * 240.0 / seconds;   // the loop fits exactly
        }
    }
    // no usable hint: whole bars (1, 2, 4, 8, 16) at a believable tempo, closest to the hint (or 110)
    const double want = bpmHint > 0 ? bpmHint : 110.0;
    double best = 120.0, bestErr = 1.0e9, bestLen = 16.0;
    for (double bars : { 1.0, 2.0, 4.0, 8.0, 16.0, 32.0 })
    {
        const double bpm = bars * 240.0 / seconds;
        if (bpm < 60.0 || bpm > 190.0) continue;
        const double err = std::abs (std::log (bpm / want));
        if (err < bestErr) { bestErr = err; best = bpm; bestLen = bars * 4.0; }
    }
    lengthBeats = bestLen;
    return best;
}

std::vector<NoteEv> pitchTrack (const float* mono, int numSamples, double sampleRate, double bpm, bool bassRange)
{
    double fs = 0;
    const auto x = decimate (mono, numSamples, sampleRate, fs);
    const double minF = bassRange ? 25.0 : 70.0, maxF = bassRange ? 450.0 : 1500.0;
    const int maxLag = (int) std::ceil (fs / minF), minLag = std::max (2, (int) std::floor (fs / maxF));
    const int W = maxLag;
    const int hop = std::max (1, (int) std::lround (fs * 0.005));
    const int n = (int) x.size();
    if (n < W + maxLag + 2) return {};

    struct Fr { double t, midi, rms; };
    std::vector<Fr> fr;
    double peakRms = 0;
    std::vector<double> d ((size_t) maxLag + 1), cm ((size_t) maxLag + 1);
    for (int s0 = 0; s0 + W + maxLag < n; s0 += hop)
    {
        double e = 0;
        for (int j = 0; j < W; ++j) e += (double) x[(size_t) (s0 + j)] * x[(size_t) (s0 + j)];
        const double rms = std::sqrt (e / W);
        peakRms = std::max (peakRms, rms);
        double midi = -1;
        if (rms > 1.0e-5)
        {
            // YIN: difference function, cumulative mean normalised, first dip under the threshold
            for (int tau = 1; tau <= maxLag; ++tau)
            {
                double sum = 0;
                const float* a = x.data() + s0;
                const float* b = a + tau;
                for (int j = 0; j < W; ++j) { const double dd = (double) a[j] - b[j]; sum += dd * dd; }
                d[(size_t) tau] = sum;
            }
            double run = 0;
            cm[0] = 1;
            for (int tau = 1; tau <= maxLag; ++tau) { run += d[(size_t) tau]; cm[(size_t) tau] = run > 0 ? d[(size_t) tau] * tau / run : 1; }
            int best = -1;
            for (int tau = minLag; tau < maxLag; ++tau)
                if (cm[(size_t) tau] < 0.15)
                {
                    while (tau + 1 < maxLag && cm[(size_t) tau + 1] < cm[(size_t) tau]) ++tau;
                    best = tau; break;
                }
            if (best < 0)
            {
                int gm = minLag;
                for (int tau = minLag; tau < maxLag; ++tau) if (cm[(size_t) tau] < cm[(size_t) gm]) gm = tau;
                if (cm[(size_t) gm] < 0.35) best = gm;
            }
            if (best > minLag && best < maxLag)
            {
                const double y0 = cm[(size_t) best - 1], y1 = cm[(size_t) best], y2 = cm[(size_t) best + 1];
                const double den = y0 - 2 * y1 + y2;
                const double tau = best + (std::abs (den) > 1e-12 ? 0.5 * (y0 - y2) / den : 0.0);
                const double f = fs / tau;
                if (f >= minF * 0.9 && f <= maxF * 1.1) midi = 69.0 + 12.0 * std::log2 (f / 440.0);
            }
        }
        fr.push_back ({ (s0 + W * 0.5) / fs, midi, rms });
    }
    if (fr.empty() || peakRms <= 0) return {};

    // --- frames -> notes: a note starts when something is pitched, ends on silence, a new pitch or a new hit
    const double gate = peakRms * 0.04;
    std::vector<NoteEv> out;
    const double bps = bpm / 60.0;
    size_t i = 0;
    while (i < fr.size())
    {
        if (fr[i].midi < 0 || fr[i].rms < gate) { ++i; continue; }
        const size_t a = i;
        std::vector<double> ms { fr[i].midi };
        double loud = fr[i].rms;
        size_t j = i + 1, off = 0;
        for (; j < fr.size(); ++j)
        {
            const bool voiced = fr[j].midi >= 0 && fr[j].rms >= gate;
            // a fresh hit: the level jumps well above the last 30 ms
            double prev = 0; int cnt = 0;
            for (size_t k = j >= 6 ? j - 6 : 0; k < j; ++k) { prev += fr[k].rms; ++cnt; }
            prev = cnt ? prev / cnt : 0;
            const bool onset = j - a > 8 && fr[j].rms > prev * 1.9 && fr[j].rms > gate * 2;
            const double med = median (ms);
            const bool moved = voiced && j - a > 8 && std::abs (fr[j].midi - med) > 0.8;
            if (! voiced) { if (++off > 3) break; continue; }
            off = 0;
            if (onset) break;
            if (moved)
            {
                // a real change only if it holds for a few frames (not a wobble)
                int hold = 0;
                for (size_t k = j; k < std::min (fr.size(), j + 5); ++k)
                    if (fr[k].midi >= 0 && std::abs (fr[k].midi - med) > 0.8) ++hold;
                if (hold >= 4) break;
                continue;
            }
            ms.push_back (fr[j].midi);
            loud = std::max (loud, fr[j].rms);
        }
        const double t0 = fr[a].t, t1 = fr[std::min (j, fr.size() - 1)].t;
        if (t1 - t0 >= 0.06 && ms.size() >= 3)
        {
            // the pitch after the attack (an 808's punch starts sharp)
            std::vector<double> body (ms.begin() + (long) std::min (ms.size() - 1, ms.size() / 5), ms.end());
            NoteEv e;
            e.pitch = (int) std::lround (median (body));
            e.start = std::round (t0 * bps * 4.0) / 4.0;
            e.len = std::max (0.125, std::round ((t1 - t0) * bps * 8.0) / 8.0);
            e.vel = std::clamp ((int) std::lround (50 + 70 * std::sqrt (loud / peakRms)), 1, 127);
            out.push_back (e);
        }
        i = std::max (j, a + 1);
    }
    // same start after rounding: keep the louder; fix octave slips against the line's own median
    std::sort (out.begin(), out.end(), [] (const NoteEv& p, const NoteEv& q) { return p.start < q.start || (p.start == q.start && p.vel > q.vel); });
    std::vector<NoteEv> clean;
    for (auto& e : out) if (clean.empty() || std::abs (clean.back().start - e.start) > 1.0e-6) clean.push_back (e);
    if (! clean.empty())
    {
        std::vector<double> ps; for (auto& e : clean) ps.push_back (e.pitch);
        const double med = median (ps);
        for (auto& e : clean)
        {
            while (e.pitch - med > 14) e.pitch -= 12;
            while (med - e.pitch > 14) e.pitch += 12;
            e.pitch = std::clamp (e.pitch, bassRange ? 20 : 36, bassRange ? 76 : 100);
        }
        for (size_t k = 0; k + 1 < clean.size(); ++k)
            clean[k].len = std::min (clean[k].len, clean[k + 1].start - clean[k].start);
    }
    return clean;
}

std::vector<NoteEv> chordTrack (const float* mono, int numSamples, double sampleRate, double bpm)
{
    double fs = 0;
    const auto x = decimate (mono, numSamples, sampleRate, fs);
    const int N = 4096, hop = 1024;
    const int n = (int) x.size();
    if (n < N) return {};
    const double spb = fs * 60.0 / bpm;   // samples per beat
    const int beats = std::max (1, (int) std::floor (n / spb + 0.5));
    std::vector<std::array<double, 12>> chroma ((size_t) beats), bassC ((size_t) beats);
    std::vector<double> energy ((size_t) beats, 0.0);
    std::vector<int> counts ((size_t) beats, 0);
    std::vector<std::complex<double>> buf ((size_t) N);
    for (int s0 = 0; s0 + N <= n; s0 += hop)
    {
        for (int k = 0; k < N; ++k)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * k / (N - 1));
            buf[(size_t) k] = { x[(size_t) (s0 + k)] * w, 0.0 };
        }
        fft (buf);
        const int b = std::min (beats - 1, (int) ((s0 + N * 0.5) / spb));
        for (int k = 1; k < N / 2; ++k)
        {
            const double f = k * fs / N;
            if (f < 40.0 || f > 2100.0) continue;
            const double mag = std::abs (buf[(size_t) k]);
            const int pc = ((int) std::lround (69.0 + 12.0 * std::log2 (f / 440.0)) % 12 + 12) % 12;
            const double v = std::log1p (mag * 10.0);
            if (f >= 65.0) chroma[(size_t) b][(size_t) pc] += v;
            if (f < 220.0) bassC[(size_t) b][(size_t) pc] += v;
            energy[(size_t) b] += mag * mag;
        }
        ++counts[(size_t) b];
    }
    const double peakE = *std::max_element (energy.begin(), energy.end());
    struct Tpl { int q; std::vector<int> iv; };
    static const Tpl tpls[] = { { (int) Q::Maj, { 0, 4, 7 } }, { (int) Q::Min, { 0, 3, 7 } }, { (int) Q::Dom7, { 0, 4, 7, 10 } },
                                { (int) Q::Min7, { 0, 3, 7, 10 } }, { (int) Q::Maj7, { 0, 4, 7, 11 } } };
    std::vector<int> root ((size_t) beats, -1), qual ((size_t) beats, 0);
    for (int b = 0; b < beats; ++b)
    {
        if (counts[(size_t) b] == 0 || energy[(size_t) b] < peakE * 0.02) continue;
        auto c = chroma[(size_t) b];
        const double mean = std::accumulate (c.begin(), c.end(), 0.0) / 12.0;
        double norm = 0; for (auto& v : c) { v = std::max (0.0, v - mean * 0.6); norm += v * v; }
        norm = std::sqrt (norm);
        if (norm <= 0) continue;
        const double bmax = std::max (1e-9, *std::max_element (bassC[(size_t) b].begin(), bassC[(size_t) b].end()));
        double best = -1;
        for (int r = 0; r < 12; ++r)
            for (size_t t = 0; t < 5; ++t)
            {
                double dot = 0;
                for (int iv : tpls[t].iv) dot += c[(size_t) ((r + iv) % 12)];
                const double score = dot / (norm * std::sqrt ((double) tpls[t].iv.size())) + 0.18 * bassC[(size_t) b][(size_t) r] / bmax
                                   - (tpls[t].iv.size() > 3 ? 0.03 : 0.0);   // a 7th must earn its place
                if (score > best) { best = score; root[(size_t) b] = r; qual[(size_t) b] = (int) t; }
            }
    }
    // smooth: a one-beat blip between two equal chords is the same chord
    for (int b = 1; b + 1 < beats; ++b)
        if (root[(size_t) b - 1] == root[(size_t) b + 1] && qual[(size_t) b - 1] == qual[(size_t) b + 1]
            && (root[(size_t) b] != root[(size_t) b - 1] || qual[(size_t) b] != qual[(size_t) b - 1]))
        { root[(size_t) b] = root[(size_t) b - 1]; qual[(size_t) b] = qual[(size_t) b - 1]; }
    std::vector<NoteEv> out;
    int b = 0;
    while (b < beats)
    {
        if (root[(size_t) b] < 0) { ++b; continue; }
        int e = b + 1;
        while (e < beats && root[(size_t) e] == root[(size_t) b] && qual[(size_t) e] == qual[(size_t) b]) ++e;
        const int r = root[(size_t) b];
        const int base = 48 + r;
        for (int iv : tpls[(size_t) qual[(size_t) b]].iv)
            out.push_back ({ (double) b, (double) (e - b), base + iv + (iv == 0 ? 0 : 12), 90 });
        b = e;
    }
    return out;
}

int estimateKey (const std::vector<NoteEv>& notes, bool& minor)
{
    static const double maj[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double mnr[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };
    double h[12] {};
    for (auto& n : notes) h[((n.pitch % 12) + 12) % 12] += std::max (0.1, n.len);
    auto corr = [&] (const double* prof, int r)
    {
        double ma = 0, mb = 0;
        for (int i = 0; i < 12; ++i) { ma += h[(i + r) % 12]; mb += prof[i]; }
        ma /= 12; mb /= 12;
        double num = 0, da = 0, db = 0;
        for (int i = 0; i < 12; ++i)
        {
            const double a = h[(i + r) % 12] - ma, b = prof[i] - mb;
            num += a * b; da += a * a; db += b * b;
        }
        return da > 0 && db > 0 ? num / std::sqrt (da * db) : -1.0;
    };
    double best = -2; int key = 0; minor = false;
    for (int r = 0; r < 12; ++r)
    {
        const double a = corr (maj, r), b = corr (mnr, r);
        if (a > best) { best = a; key = r; minor = false; }
        if (b > best) { best = b; key = r; minor = true; }
    }
    return key;
}
} // namespace hs
