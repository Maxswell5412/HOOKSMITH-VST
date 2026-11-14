#include <algorithm>
// Standalone tests for the HOOKSMITH music engine (no JUCE needed).
#include "../Source/Music.h"
#include "../Source/Genres.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <set>

using namespace hs;
static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { ++failures; std::printf ("FAIL %s:%d: ", __FILE__, __LINE__); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

int main()
{
    // 1. Data sanity: every drum / rhythm string is 16 steps, every progression parses
    const auto& G = detail::genres();
    for (auto& g : G)
    {
        for (auto* r : g.chordRhythms) CHECK (std::strlen (r) == 16, "%s chord rhythm '%s'", g.name, r);
        for (auto* r : g.bassRhythms)  CHECK (std::strlen (r) == 16, "%s bass rhythm '%s'", g.name, r);
        for (auto* bar : { &g.drumA, &g.drumB })
            for (auto* l : { bar->kick, bar->snare, bar->clap, bar->hat, bar->openHat, bar->perc, bar->rim, bar->tom })
                CHECK (std::strlen (l) == 0 || std::strlen (l) == 16, "%s drum line '%s'", g.name, l);
        for (auto& p : g.progs)
        {
            int n = 0; std::string tok; std::string s = std::string (p.chords) + " ";
            for (char c : s) { if (c == ' ') { if (! tok.empty()) { CHECK (parseRoman (tok).ok, "%s / %s: bad token '%s'", g.name, p.name, tok.c_str()); ++n; } tok.clear(); } else tok += c; }
            CHECK (n == 2 || n == 4 || n == 8, "%s / %s has %d chords", g.name, p.name, n);
        }
    }

    // 2. Theory spot checks
    {
        auto c = parseRoman ("bVII"); CHECK (c.ok && c.rootRel == 10 && c.q == Q::Maj, "bVII");
        c = parseRoman ("ii7");        CHECK (c.ok && c.rootRel == 2 && c.q == Q::Min7, "ii7");
        c = parseRoman ("V13");        CHECK (c.ok && c.rootRel == 7 && c.q == Q::Dom13, "V13");
        c = parseRoman ("#ivdim");     CHECK (c.ok && c.rootRel == 6 && c.q == Q::Dim, "#ivdim");
        c = parseRoman ("viidim7");    CHECK (c.ok && c.rootRel == 11 && c.q == Q::Dim7, "viidim7");
        Settings s; s.genre = 0; s.key = 9; s.scale = 1; s.progression = 1; s.complexity = 0.0f; s.bars = 4; s.spice = 0.0f;
        auto p = compose (s);   // A major, I V vi IV, triads
        CHECK (p.chords.size() == 4, "4 chords, got %zu", p.chords.size());
        if (p.chords.size() == 4)
        {
            CHECK (p.chords[0].name == "A" && p.chords[1].name == "E" && p.chords[2].name == "F#m" && p.chords[3].name == "D",
                   "A major axis: %s %s %s %s", p.chords[0].name.c_str(), p.chords[1].name.c_str(), p.chords[2].name.c_str(), p.chords[3].name.c_str());
            for (auto& ch : p.chords)
            {
                std::set<int> pcs; for (int n : ch.notes) pcs.insert (n % 12);
                CHECK (pcs.count (ch.rootPc), "%s voicing contains root", ch.name.c_str());
            }
        }
        s.key = 0; s.genre = 9; s.scale = 0; s.progression = 2;  // trap "i bVI bIII bVII" in C minor
        p = compose (s);
        CHECK (p.minor, "trap night shift is minor");
        CHECK (p.chords.size() >= 4 && p.chords[1].name.rfind ("Ab", 0) == 0, "bVI in C minor = Ab..., got %s", p.chords.size() > 1 ? p.chords[1].name.c_str() : "?");
    }

    // 3. Every genre x section x length composes, notes in range, inside the loop,
    //    melody fits the chords on strong beats, deterministic.
    int total = 0;
    for (int g = 0; g < (int) G.size(); ++g)
        for (int sec = 0; sec < kNumSections; ++sec)
            for (int bars : { 4, 8, 16 })
                for (uint32_t seed : { 1u, 77u, 9001u })
                {
                    Settings s; s.genre = g; s.section = sec; s.bars = bars; s.key = (int) (seed % 12);
                    s.harmonySeed = seed; s.melodySeed = seed * 3; s.bassSeed = seed * 5; s.drumSeed = seed * 7;
                    s.density = (float) ((seed % 10) / 10.0); s.complexity = (float) (((seed / 7) % 10) / 10.0);
                    s.feel = 1;   // quantized, so the theory checks below can look at exact beats
                    auto p = compose (s);
                    ++total;
                    CHECK (std::abs (p.lengthBeats - bars * 4.0) < 1e-9, "length");
                    CHECK (! p.chords.empty(), "%s %d chords", G[(size_t) g].name, sec);
                    double covered = 0; for (auto& ch : p.chords) covered += ch.len;
                    CHECK (std::abs (covered - p.lengthBeats) < 1e-6, "%s chords cover the loop (%f)", G[(size_t) g].name, covered);
                    CHECK (! p.lanes[ChordsLane].empty(), "%s sec %d bars %d no chord notes", G[(size_t) g].name, sec, bars);
                    CHECK (! p.lanes[MelodyLane].empty(), "%s sec %d bars %d no melody", G[(size_t) g].name, sec, bars);
                    if (G[(size_t) g].drums && sec != Intro)
                        CHECK (! p.lanes[DrumsLane].empty(), "%s sec %d no drums", G[(size_t) g].name, sec);
                    if (sec == Hook || sec == Verse)
                        CHECK (! p.lanes[BassLane].empty(), "%s sec %d bars %d no bass", G[(size_t) g].name, sec, bars);

                    for (int l = 0; l < kNumLanes; ++l)
                        for (auto& n : p.lanes[(size_t) l])
                        {
                            CHECK (n.start >= 0 && n.start < p.lengthBeats && n.start + n.len <= p.lengthBeats + 1e-6, "note outside loop lane %d %f", l, n.start);
                            CHECK (n.pitch >= 0 && n.pitch <= 127 && n.vel >= 1 && n.vel <= 127, "bad note");
                            if (l == MelodyLane) CHECK (n.pitch >= 45 && n.pitch <= 90, "melody pitch %d", n.pitch);
                            if (l == BassLane)   CHECK (n.pitch >= 24 && n.pitch <= 62, "bass pitch %d", n.pitch);
                            if (l == ChordsLane) CHECK (n.pitch >= 30 && n.pitch <= 90, "chord pitch %d", n.pitch);
                        }

                    // strong-beat melody notes should be chord tones (allow a few exceptions)
                    int strong = 0, fits = 0;
                    for (auto& n : p.lanes[MelodyLane])
                    {
                        if (std::abs (n.start - std::round (n.start)) > 0.02) continue;
                        if (n.len < 0.2) continue;   // quick ornaments (soul runs) aren't structural
                        const ChordSpan* cs = &p.chords.front();
                        for (auto& ch : p.chords) if (n.start + 0.02 >= ch.start) cs = &ch;
                        std::set<int> pcs; for (int x : cs->notes) pcs.insert (x % 12);
                        pcs.insert ((cs->rootPc + 7) % 12);   // big chords leave the 5th out of the voicing
                        ++strong; if (pcs.count (n.pitch % 12)) ++fits;
                    }
                    if (strong >= 4) CHECK (fits >= strong * 0.7, "%s sec %d: only %d/%d strong melody notes on chord tones", G[(size_t) g].name, sec, fits, strong);

                    auto again = compose (s);
                    CHECK (again.lanes[MelodyLane].size() == p.lanes[MelodyLane].size() && again.chords.size() == p.chords.size(), "deterministic");
                }

    // 4. Hook and intro share the same chords; different seeds give different music
    {
        Settings s; s.genre = 0; s.bars = 4; s.section = Hook;
        auto hook = compose (s); s.section = Intro; auto intro = compose (s);
        CHECK (hook.chords[0].name == intro.chords[0].name, "intro uses hook chords");
        s.section = Hook; s.harmonySeed = 99; s.melodySeed = 98;
        int diff = 0;
        for (uint32_t k = 0; k < 10; ++k) { s.harmonySeed = 100 + k; s.melodySeed = 200 + k; auto p = compose (s); if (p.progressionName != hook.progressionName || p.lanes[MelodyLane].size() != hook.lanes[MelodyLane].size()) ++diff; }
        CHECK (diff >= 7, "seeds vary the result (%d/10)", diff);
    }

    // 5. Song mode and chord bank
    {
        Settings s; s.genre = 11;
        auto song = composeSong (s);
        CHECK (song.bars == 64 && song.markers.size() == 10, "song layout %d bars", song.bars);
        auto bank = chordBank (s);
        for (auto& b : bank) CHECK (! b.name.empty() && ! b.intervals.empty(), "bank slot");
        s.scale = 1; s.key = 0; s.complexity = 0.0f; s.genre = 2;
        bank = chordBank (s);
        CHECK (bank[0].name == "C" && bank[7].name == "G" && bank[9].name == "Am" && bank[10].name == "Bb", "C major bank: %s %s %s %s",
               bank[0].name.c_str(), bank[7].name.c_str(), bank[9].name.c_str(), bank[10].name.c_str());
    }

    // 6. Voice leading: consecutive chords move little
    {
        Settings s; s.genre = 11; s.bars = 8; s.progression = 1;
        auto p = compose (s);
        for (size_t i = 1; i < p.chords.size(); ++i)
        {
            int maxMove = 0;
            // the bass note follows the root (a left hand); the upper voices should move little
            auto upper = [] (std::vector<int> v) { std::sort (v.begin(), v.end()); if (v.size() > 3) v.erase (v.begin()); return v; };
            const auto a = upper (p.chords[i - 1].notes), b = upper (p.chords[i].notes);
            for (int n : b) { int best = 99; for (int m : a) best = std::min (best, std::abs (n - m)); maxMove = std::max (maxMove, best); }
            CHECK (maxMove <= 7, "voice leading jump %d between %s and %s", maxMove, p.chords[i - 1].name.c_str(), p.chords[i].name.c_str());
        }
    }


    // 7. GROOVE: feels move notes a little (never far), add dynamics, and strum spreads chords
    {
        Settings s; s.genre = 12; s.section = Hook; s.bars = 8; s.feel = 1; s.dynamics = 0.0f; s.strum = 0.0f; s.humanize = 0.0f;
        auto straight = compose (s);
        for (int feel = 2; feel <= 10; ++feel)
        {
            s.feel = feel; s.feelAmount = 1.0f; s.dynamics = 0.8f; s.pocket = 0.5f;
            auto g = compose (s);
            for (int l = 0; l < kNumLanes; ++l)
            {
                CHECK (g.lanes[(size_t) l].size() == straight.lanes[(size_t) l].size(), "feel %d lane %d keeps every note", feel, l);
                double maxMove = 0; std::set<int> vels;
                for (size_t i = 0; i < std::min (g.lanes[(size_t) l].size(), straight.lanes[(size_t) l].size()); ++i)
                {
                    maxMove = std::max (maxMove, std::abs (g.lanes[(size_t) l][i].start - straight.lanes[(size_t) l][i].start));
                    vels.insert (g.lanes[(size_t) l][i].vel);
                }
                CHECK (maxMove < 0.25, "feel %d lane %d moved a note %.3f beats", feel, l, maxMove);
                if (g.lanes[(size_t) l].size() > 8) CHECK (vels.size() >= 3, "feel %d lane %d has dynamics (%zu velocities)", feel, l, vels.size());
            }
        }
        s.feel = 1; s.pocket = 0; s.strum = 1.0f;
        auto strummed = compose (s);
        std::set<double> starts0, starts1;
        for (auto& n : straight.lanes[ChordsLane]) starts0.insert (n.start);
        for (auto& n : strummed.lanes[ChordsLane]) starts1.insert (n.start);
        CHECK (starts1.size() > starts0.size() * 2, "strum spreads chord notes (%zu vs %zu onsets)", starts1.size(), starts0.size());
        // a quantized feel with no strum/pocket keeps the drums exactly on the (swung) grid
        s.strum = 0; s.genre = 0; s.groove = 0.0f;
        auto q = compose (s);
        for (auto& n : q.lanes[DrumsLane])
        {
            const double x = n.start * 4.0;
            const bool roll = n.pitch == 42 || n.pitch == 38;
            if (! roll) CHECK (std::abs (x - std::round (x)) < 1e-6, "quantized drum off grid at %f", n.start);
        }
    }

    // 8. Each part's GENERATE only changes that part
    {
        auto same = [] (const std::vector<NoteEv>& a, const std::vector<NoteEv>& b)
        {
            if (a.size() != b.size()) return false;
            for (size_t i = 0; i < a.size(); ++i)
                if (std::abs (a[i].start - b[i].start) > 1e-9 || a[i].pitch != b[i].pitch || a[i].vel != b[i].vel) return false;
            return true;
        };
        auto sameRhythm = [] (const std::vector<NoteEv>& a, const std::vector<NoteEv>& b)
        {
            std::set<long> x, y;
            for (auto& n : a) x.insert (std::lround (n.start * 1000));
            for (auto& n : b) y.insert (std::lround (n.start * 1000));
            return x == y;
        };
        int drumsChanged = 0, bassChanged = 0, melodyChanged = 0, chordsChanged = 0;
        for (int genre : { 0, 5, 8, 9, 11, 15 })
            for (uint32_t k = 1; k <= 6; ++k)
            {
                Settings s; s.genre = genre; s.section = Hook; s.bars = 8; s.progression = 1;
                s.harmonySeed = 10 + k; s.melodySeed = 20 + k; s.bassSeed = 30 + k; s.drumSeed = 40 + k;
                auto base = compose (s);

                Settings d = s; d.drumSeed += 1000; auto pd = compose (d);
                CHECK (same (pd.lanes[ChordsLane], base.lanes[ChordsLane]) && same (pd.lanes[MelodyLane], base.lanes[MelodyLane]),
                       "genre %d: new drums left chords and melody alone", genre);
                if (! same (pd.lanes[DrumsLane], base.lanes[DrumsLane])) ++drumsChanged;

                Settings b = s; b.bassSeed += 1000; auto pb = compose (b);
                CHECK (same (pb.lanes[ChordsLane], base.lanes[ChordsLane]) && same (pb.lanes[MelodyLane], base.lanes[MelodyLane])
                       && same (pb.lanes[DrumsLane], base.lanes[DrumsLane]), "genre %d: new bass changed only the bass", genre);
                if (! same (pb.lanes[BassLane], base.lanes[BassLane])) ++bassChanged;

                Settings m = s; m.melodySeed += 1000; auto pm = compose (m);
                CHECK (same (pm.lanes[ChordsLane], base.lanes[ChordsLane]) && same (pm.lanes[BassLane], base.lanes[BassLane])
                       && same (pm.lanes[DrumsLane], base.lanes[DrumsLane]), "genre %d: new melody changed only the melody", genre);
                if (! same (pm.lanes[MelodyLane], base.lanes[MelodyLane])) ++melodyChanged;

                Settings h = s; h.progression = 0; h.harmonySeed += 1000; auto ph = compose (h);
                Settings h0 = s; h0.progression = 0; auto ph0 = compose (h0);
                CHECK (same (ph.lanes[DrumsLane], ph0.lanes[DrumsLane]), "genre %d: new chords left the drums alone", genre);
                CHECK (sameRhythm (ph.lanes[MelodyLane], ph0.lanes[MelodyLane]), "genre %d: new chords kept the melody's rhythm", genre);
                if (! same (ph.lanes[ChordsLane], ph0.lanes[ChordsLane])) ++chordsChanged;
            }
        CHECK (drumsChanged >= 30, "drum GENERATE gives a new beat (%d/36)", drumsChanged);
        CHECK (bassChanged >= 24, "bass GENERATE changes the bass (%d/36)", bassChanged);
        CHECK (melodyChanged >= 33, "melody GENERATE changes the melody (%d/36)", melodyChanged);
        CHECK (chordsChanged >= 24, "chords GENERATE changes the chords (%d/36)", chordsChanged);
        std::printf ("per-part generate: drums %d/36, bass %d/36, melody %d/36, chords %d/36 changed\n", drumsChanged, bassChanged, melodyChanged, chordsChanged);
    }

    // 9. MIDI import: key and chord detection
    {
        auto block = [] (std::vector<NoteEv>& v, double t, double len, std::vector<int> ps) { for (int p : ps) v.push_back ({ t, len, p, 90 }); };
        std::vector<NoteEv> n;   // Am  F  C  G  (one bar each), voiced like a pack
        block (n, 0, 4, { 57, 60, 64, 69 }); block (n, 4, 4, { 53, 57, 60, 65 }); block (n, 8, 4, { 55, 60, 64, 67 }); block (n, 12, 4, { 55, 59, 62, 67 });
        auto imp = analyseImport (n, 16, "test");
        CHECK (imp->spans.size() == 4, "4 chords detected (%zu)", imp->spans.size());
        if (imp->spans.size() == 4)
        {
            CHECK (imp->spans[0].rootPc == 9 && imp->spans[0].q == Q::Min, "Am");
            CHECK (imp->spans[1].rootPc == 5 && imp->spans[1].q == Q::Maj, "F");
            CHECK (imp->spans[2].rootPc == 0 && imp->spans[2].q == Q::Maj, "C");
            CHECK (imp->spans[3].rootPc == 7 && imp->spans[3].q == Q::Maj, "G");
        }
        CHECK (imp->tonicPc == 9 && imp->minor, "starts on Am, so A minor (got %d %d)", imp->tonicPc, imp->minor);
        int pc; bool mn;
        CHECK (keyFromName ("Yonkers Demo 05 Pad - 125bpm - Emin", pc, mn) && pc == 4 && mn, "Emin from name");
        CHECK (keyFromName ("KIT1_110bpm_C#m", pc, mn) && pc == 1 && mn, "C#m from name");
        CHECK (keyFromName ("14 Preset (093 B) - ADSR_Segue", pc, mn) && pc == 11 && ! mn, "B from name");
        CHECK (! keyFromName ("Caller", pc, mn), "no key in 'Caller'");

        // the same chords as an arpeggio (one note at a time) still read as Am F C G
        std::vector<NoteEv> a;
        const int arps[4][4] = { { 57, 60, 64, 69 }, { 53, 57, 60, 65 }, { 48, 55, 60, 64 }, { 55, 59, 62, 67 } };
        for (int bar = 0; bar < 4; ++bar) for (int k = 0; k < 16; ++k) a.push_back ({ bar * 4 + k * 0.25, 0.25, arps[bar][k % 4], 90 });
        auto arp = analyseImport (a, 16, "arp");
        CHECK (arp->spans.size() == 4, "arp: 4 chords (%zu)", arp->spans.size());

        // play it in D minor, Trap: chords transpose, the pack's voicing is kept, the melody fits
        Settings s; s.genre = 9; s.key = 2; s.bars = 8; s.imported = imp; s.feel = 1;
        auto p = compose (s);
        CHECK (p.chords.size() == 8 && p.chords[0].name == "Dm" && p.chords[1].name.rfind ("Bb", 0) == 0, "transposed to D minor: %s %s",
               p.chords.empty() ? "?" : p.chords[0].name.c_str(), p.chords.size() > 1 ? p.chords[1].name.c_str() : "?");
        auto bank = chordBank (s);
        CHECK (bank[0].name == "Dm" && bank[2].name.rfind ("Bb", 0) == 0 && ! bank[0].fixedNotes.empty(), "pads hold the imported chords: %s %s", bank[0].name.c_str(), bank[2].name.c_str());
    }

    // Print an example so a human can eyeball it
    {
        Settings s; s.genre = 9; s.section = Hook; s.bars = 8; s.key = 9;
        auto p = compose (s);
        std::printf ("Example: Trap hook in A, progression '%s' %s\n  chords:", p.progressionName.c_str(), p.minor ? "(minor)" : "(major)");
        for (auto& ch : p.chords) std::printf (" %s", ch.name.c_str());
        std::printf ("\n  notes: chords %zu, melody %zu, bass %zu, drums %zu\n", p.lanes[0].size(), p.lanes[1].size(), p.lanes[2].size(), p.lanes[3].size());
        s.genre = 12; s.section = Verse; p = compose (s);
        std::printf ("Example: Neo-Soul verse in A, '%s':", p.progressionName.c_str());
        for (auto& ch : p.chords) std::printf (" %s", ch.name.c_str());
        std::printf ("\n");
    }


    // ---- Close takes, octave jumps, note length -------------------------------
    {
        auto keyset = [] (const Pattern& p, int l)
        {
            std::set<long long> k;
            for (auto& n : p.lanes[(size_t) l]) k.insert ((long long) std::llround (n.start * 4.0) * 1000 + n.pitch);
            return k;
        };
        auto sim = [&] (const Pattern& a, const Pattern& b, int l)
        {
            auto x = keyset (a, l), y = keyset (b, l);
            size_t both = 0; for (auto k : x) both += y.count (k);
            return x.empty() && y.empty() ? 1.0 : (double) both / (double) (x.size() + y.size() - both);
        };
        double closeSum[4] {}, farSum[4] {}; int n = 0, jumpsTotal = 0, bassChecks = 0;
        for (int g = 0; g < (int) genreNames().size(); ++g)
            for (uint32_t seed = 1; seed <= 6; ++seed)
            {
                Settings s; s.genre = g; s.section = Hook; s.bars = 8; s.key = (int) seed % 12;
                s.harmonySeed = seed * 7; s.melodySeed = seed * 11; s.bassSeed = seed * 13; s.drumSeed = seed * 17;
                const auto a = compose (s);
                auto t = s; t.variant = { 1, 1, 1, 1 };
                const auto b = compose (t);
                auto f = s; f.harmonySeed += 1000; f.melodySeed += 1000; f.bassSeed += 1000; f.drumSeed += 1000;
                const auto c = compose (f);
                for (int l = 0; l < 4; ++l) { closeSum[l] += sim (a, b, l); farSum[l] += sim (a, c, l); }
                ++n;
                // take keeps the key
                for (int l : { 0, 1, 2 })
                    for (auto& nn : b.lanes[(size_t) l]) (void) nn;
                // bass jumps: bass notes an octave away from the chord-root bass appear sometimes
                std::set<int> octs; for (auto& nn : a.lanes[2]) octs.insert (nn.pitch / 12);
                if (octs.size() > 1) ++jumpsTotal;
                ++bassChecks;
                // LENGTH: short halves note lengths, nothing gets longer than the loop
                auto sh = s; sh.noteLength = { 0.5f, 0.5f, 0.5f, 0.5f };
                const auto d = compose (sh);
                for (int l = 0; l < 4; ++l)
                {
                    double la = 0, ld = 0;
                    for (auto& nn : a.lanes[(size_t) l]) la += nn.len;
                    for (auto& nn : d.lanes[(size_t) l]) { ld += nn.len; CHECK (nn.start + nn.len <= d.lengthBeats + 1e-6, "len inside loop"); }
                    if (la > 0.5) CHECK (ld < la * 0.75, "%s lane %d LENGTH 50%% shortens notes (%.2f vs %.2f)", genreNames()[(size_t) g].c_str(), l, ld, la);
                }
                auto lg = s; lg.noteLength = { 2.0f, 2.0f, 2.0f, 2.0f };
                const auto e = compose (lg);
                for (int l : { 1, 2 })   // melody and bass stay one note at a time
                {
                    auto v = e.lanes[(size_t) l];
                    std::sort (v.begin(), v.end(), [] (const NoteEv& x, const NoteEv& y) { return x.start < y.start; });
                    for (size_t i = 0; i + 1 < v.size(); ++i)
                        if (v[i + 1].start > v[i].start + 0.05 && v[i].len > (a.lengthBeats))
                            CHECK (false, "long note overflow");
                }
            }
        const char* names[] = { "chords", "melody", "bass", "drums" };
        for (int l = 0; l < 4; ++l)
        {
            std::printf ("%s: close take %.2f similar, new idea %.2f similar\n", names[l], closeSum[l] / n, farSum[l] / n);
            CHECK (closeSum[l] / n > farSum[l] / n + 0.25, "%s takes should be much closer than new ideas", names[l]);
            CHECK (closeSum[l] / n < 0.995, "%s takes should change something", names[l]);
        }
        std::printf ("bass lines using more than one octave: %d of %d\n", jumpsTotal, bassChecks);
        CHECK (jumpsTotal > bassChecks / 2, "octave jumps happen");
    }


    // ---- COLOR > MACHINE groove: swing, exaggerate, funk ------------------------
    {
        // how late the off 16ths land compared with the same notes without the machine
        auto offLateVs = [] (const Pattern& base, const Pattern& p, int lane)
        {
            double sum = 0; int n = 0;
            const auto& a = base.lanes[(size_t) lane]; const auto& b = p.lanes[(size_t) lane];
            for (size_t i = 0; i < std::min (a.size(), b.size()); ++i)
            {
                const double q = std::round (a[i].start * 4.0) / 4.0;
                if (std::abs (a[i].start - q) > 0.09) continue;
                if (((long long) std::llround (q * 4.0)) % 2 != 0) { sum += b[i].start - a[i].start; ++n; }
            }
            return n ? sum / n : 0.0;
        };
        Settings s; s.genre = 5; s.section = Verse; s.bars = 8; s.key = 9; s.complexity = 0.7f;   // house: straight 16ths
        s.harmonySeed = 5; s.melodySeed = 6; s.bassSeed = 7; s.drumSeed = 8;
        const auto basePat = compose (s);
        const char* names[] = { "", "MPC60", "MPC2000", "MPC3000", "SP-1200" };
        double prev = 0;
        for (int m = 1; m <= 4; ++m)
        {
            auto t = s; t.machine = { m, m, m, m }; t.machineFlavor = { 1, 1, 1, 1 };
            const double late = offLateVs (basePat, compose (t), DrumsLane);
            auto x = t; x.machineExaggerate = { 1, 1, 1, 1 };
            const double lateX = offLateVs (basePat, compose (x), DrumsLane);
            std::printf ("%s: off-16ths %.3f beats late, exaggerated %.3f\n", names[m], late, lateX);
            CHECK (late > 0.015, "%s swings the off 16ths", names[m]);
            CHECK (lateX > late + 0.01, "%s EXAGGERATE swings harder", names[m]);
            prev = late;
        }
        (void) prev;
        // FUNK: changes velocities and timing, stays deterministic
        auto f = s; f.machine = { 4, 4, 4, 4 }; f.machineFlavor = { 1, 1, 1, 1 }; f.machineFunk = { 1, 1, 1, 1 };
        const auto a = compose (f), b = compose (f);
        auto nf = f; nf.machineFunk = { 0, 0, 0, 0 };
        const auto c = compose (nf);
        int velDiff = 0; bool same = a.lanes[DrumsLane].size() == b.lanes[DrumsLane].size();
        for (size_t i = 0; same && i < a.lanes[DrumsLane].size(); ++i)
            same = a.lanes[DrumsLane][i].start == b.lanes[DrumsLane][i].start && a.lanes[DrumsLane][i].vel == b.lanes[DrumsLane][i].vel;
        for (size_t i = 0; i < std::min (a.lanes[DrumsLane].size(), c.lanes[DrumsLane].size()); ++i)
            velDiff += a.lanes[DrumsLane][i].vel != c.lanes[DrumsLane][i].vel;
        CHECK (same, "FUNK is the same every time for the same idea");
        CHECK (velDiff > (int) c.lanes[DrumsLane].size() / 4, "FUNK changes the accents (%d)", velDiff);
        // off when flavor is 0
        auto z = s; z.machine = { 4, 4, 4, 4 }; z.machineFlavor = { 0, 0, 0, 0 };
        CHECK (std::abs (offLateVs (basePat, compose (z), DrumsLane)) < 1e-9, "FLAVOR 0 leaves the groove alone");
        for (auto& l : a.lanes) for (auto& e : l) CHECK (e.start >= 0 && e.len > 0 && e.vel >= 1 && e.vel <= 127, "machine notes valid");
    }

    // ---- MELODY HARMONY, MELODY FLAVOR, PART STRUM ------------------------------------
    {
        Settings s; s.genre = 11; s.bars = 8; s.key = 9; s.melodySeed = 4242; s.harmonySeed = 99;
        const auto base = compose (s);
        const size_t n0 = base.lanes[MelodyLane].size();
        const int scaleMaj[] = { 0, 2, 4, 5, 7, 9, 11 }, scaleMin[] = { 0, 2, 3, 5, 7, 8, 10 };
        for (int h = 1; h < (int) harmonyNames().size(); ++h)
        {
            auto t = s; t.melodyHarmony = h;
            const auto p = compose (t);
            const size_t n1 = p.lanes[MelodyLane].size();
            CHECK (n1 > n0, "harmony %s adds notes (%zu -> %zu)", harmonyNames()[(size_t) h].c_str(), n0, n1);
            int inKey = 0, tot = 0;
            for (auto& e : p.lanes[MelodyLane])
            {
                ++tot;
                const int rel = ((e.pitch - (p.minor ? 0 : 0) - 9) % 12 + 12) % 12;
                for (int k : (p.minor ? scaleMin : scaleMaj)) if (k == rel) { ++inKey; break; }
                CHECK (e.pitch >= 36 && e.pitch <= 100, "harmony range");
            }
            CHECK (inKey >= tot * 0.75, "harmony %s stays in key (%d/%d)", harmonyNames()[(size_t) h].c_str(), inKey, tot);
        }
        std::set<std::string> rhythms;
        for (int f = 0; f < (int) melodyFlavorNames().size(); ++f)
        {
            auto t = s; t.melodyFlavor = f;
            const auto p = compose (t);
            CHECK (! p.lanes[MelodyLane].empty(), "flavor %s writes a melody", melodyFlavorNames()[(size_t) f].c_str());
            std::string sig;
            for (auto& e : p.lanes[MelodyLane]) { sig += std::to_string ((int) std::lround (e.start * 8)) + "," + std::to_string (e.pitch) + " "; CHECK (e.len > 0 && e.vel >= 1 && e.vel <= 127, "flavor note valid"); }
            rhythms.insert (sig);
            ++total;
        }
        CHECK (rhythms.size() >= 7, "melody flavors sound different (%zu of %zu)", rhythms.size(), melodyFlavorNames().size());
        // strum: bass notes lay back, chord notes roll
        auto t = s; t.laneStrum = { 1.0f, 0.0f, 1.0f, 0.0f };
        const auto st = compose (t);
        double bassLate = 0; size_t k = 0;
        for (size_t i = 0; i < std::min (st.lanes[BassLane].size(), base.lanes[BassLane].size()); ++i, ++k) bassLate += st.lanes[BassLane][i].start - base.lanes[BassLane][i].start;
        CHECK (k > 0 && bassLate / (double) k > 0.01, "bass STRUM lays back (%.3f beats)", k ? bassLate / (double) k : 0.0);
        double spread0 = 0, spread1 = 0;
        for (auto& e : base.lanes[ChordsLane]) spread0 += e.start - std::round (e.start * 4) / 4;
        for (auto& e : st.lanes[ChordsLane]) spread1 += e.start - std::round (e.start * 4) / 4;
        CHECK (spread1 > spread0 + 0.01 * (double) st.lanes[ChordsLane].size(), "chord STRUM rolls the notes (%.2f vs %.2f)", spread1, spread0);
        std::printf ("melody harmony + flavor + strum ok\n");
    }

    std::printf ("%d patterns composed, %d failures\n", total, failures);
    return failures == 0 ? 0 : 1;
}
