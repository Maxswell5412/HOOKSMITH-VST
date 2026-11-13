// Drives HOOKSMITH like a DAW would and checks what comes out.
#include "../Source/PluginProcessor.h"
#include <cstdio>
#include <cstdlib>
#include <set>

static int failures = 0;
#define CHECK(c, ...) do { if (!(c)) { ++failures; std::printf ("FAIL line %d: ", __LINE__); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

struct FakeHead : juce::AudioPlayHead
{
    double ppq = 0, bpm = 120; bool playing = true;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setBpm (bpm); p.setPpqPosition (ppq); p.setIsPlaying (playing);
        p.setTimeSignature (TimeSignature { 4, 4 });
        return p;
    }
};

static void setParam (HooksmithProcessor& p, const juce::String& id, float plainValue)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plainValue));
}

struct Ev { double sample; int ch, note, vel; bool on; };

static std::vector<Ev> run (HooksmithProcessor& p, FakeHead& head, int blocks, int block, double sr,
                            std::vector<std::pair<int, juce::MidiMessage>> input = {}, double* rms = nullptr)
{
    std::vector<Ev> out;
    juce::AudioBuffer<float> buf (2, block);
    double sumSq = 0; long n = 0;
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        for (auto& [blk, m] : input) if (blk == b) midi.addEvent (m, 0);
        p.processBlock (buf, midi);
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            if (m.isNoteOnOrOff())
                out.push_back ({ (double) b * block + meta.samplePosition, m.getChannel(), m.getNoteNumber(), m.getVelocity(), m.isNoteOn() });
        }
        for (int i = 0; i < block; ++i) { sumSq += buf.getSample (0, i) * buf.getSample (0, i); ++n; }
        head.ppq += block * head.bpm / (60.0 * sr);
    }
    if (rms) *rms = std::sqrt (sumSq / (double) n);
    return out;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 48000.0; const int block = 480;
    const double samplesPerBeat = sr * 60.0 / 120.0;

    // ------------------------------------------------------------ Sync mode timing
    {
        HooksmithProcessor p;
        FakeHead head;
        p.setPlayHead (&head);
        p.setPlayConfigDetails (0, 2, sr, block);
        p.prepareToPlay (sr, block);
        setParam (p, "genre", 9); setParam (p, "section", hs::Hook); setParam (p, "bars", 0); setParam (p, "key", 9);
        p.generateAll();
        const auto pat = p.getPattern();
        CHECK (pat.bars == 4, "4 bars");

        double rms = 0;
        const int blocks = (int) std::ceil (pat.lengthBeats * 2 * samplesPerBeat / block);   // two loops
        auto evs = run (p, head, blocks, block, sr, {}, &rms);
        CHECK (rms > 0.005, "built-in sounds are audible (rms %f)", rms);

        const int chans[] = { 1, 2, 3, 10 };
        for (int l = 0; l < hs::kNumLanes; ++l)
        {
            std::vector<double> expected;
            for (int loop = 0; loop < 2; ++loop)
                for (auto& n : pat.lanes[(size_t) l]) expected.push_back ((n.start + loop * pat.lengthBeats) * samplesPerBeat);
            std::vector<double> got;
            for (auto& e : evs) if (e.on && e.ch == chans[l]) got.push_back (e.sample);
            std::sort (expected.begin(), expected.end()); std::sort (got.begin(), got.end());
            // notes starting in the very last block may not have played yet
            while (! expected.empty() && expected.back() >= (double) blocks * block - 1) expected.pop_back();
            CHECK (got.size() == expected.size(), "lane %d: %zu notes out, %zu expected", l, got.size(), expected.size());
            double worst = 0;
            for (size_t i = 0; i < std::min (got.size(), expected.size()); ++i) worst = std::max (worst, std::abs (got[i] - expected[i]));
            CHECK (worst <= 1.0, "lane %d timing off by %.1f samples", l, worst);
            std::printf ("lane %d (%s): %zu notes, worst timing error %.2f samples\n", l, hs::laneNames()[(size_t) l].c_str(), got.size(), worst);
        }
        int onCount = 0, offCount = 0;
        for (auto& e : evs) (e.on ? onCount : offCount)++;
        head.playing = false;
        auto tail = run (p, head, 4, block, sr);
        for (auto& e : tail) (e.on ? onCount : offCount)++;
        CHECK (onCount == offCount, "every note gets a note-off (%d on, %d off)", onCount, offCount);

        // MIDI export
        const auto file = p.writeMidiFile (-1);
        juce::MidiFile mf; juce::FileInputStream in (file);
        CHECK (in.openedOk() && mf.readFrom (in), "midi file readable: %s", file.getFullPathName().toRawUTF8());
        int notes = 0; for (int t = 0; t < mf.getNumTracks(); ++t) for (auto* e : *mf.getTrack (t)) if (e->message.isNoteOn()) ++notes;
        size_t want = 0; for (auto& l : pat.lanes) want += l.size();
        CHECK ((size_t) notes == want, "exported %d notes, pattern has %zu", notes, want);
        const auto songFile = p.writeMidiFile (-2);
        CHECK (songFile.getSize() > file.getSize(), "song file bigger than one section");
        std::printf ("export: %s (%d notes)\n", file.getFileName().toRawUTF8(), notes);

        // State save / restore gives the exact same music
        juce::MemoryBlock state; p.getStateInformation (state);
        HooksmithProcessor q; q.setStateInformation (state.getData(), (int) state.getSize());
        const auto& qp = q.getPattern();
        bool same = qp.chords.size() == pat.chords.size() && qp.lanes[1].size() == pat.lanes[1].size();
        for (size_t i = 0; same && i < qp.chords.size(); ++i) same = qp.chords[i].name == pat.chords[i].name;
        CHECK (same, "restored project plays the same idea");
    }

    // ------------------------------------------------------------ Chord Keys mode
    {
        HooksmithProcessor p; FakeHead head; head.playing = false;
        p.setPlayHead (&head); p.setPlayConfigDetails (0, 2, sr, block); p.prepareToPlay (sr, block);
        setParam (p, "genre", 2); setParam (p, "scale", 1); setParam (p, "key", 0); setParam (p, "complexity", 0.0f);
        setParam (p, "mode", hs::ChordKeysMode);
        p.generateAll();
        auto evs = run (p, head, 20, block, sr, { { 2, juce::MidiMessage::noteOn (1, 67, (juce::uint8) 100) },
                                                  { 10, juce::MidiMessage::noteOff (1, 67) } });
        std::set<int> pcs; int ons = 0, offs = 0;
        for (auto& e : evs) { if (e.on) { ++ons; pcs.insert (e.note % 12); } else ++offs; }
        CHECK (pcs == std::set<int> ({ 7, 11, 2 }), "G key plays a G major chord in C major (%d notes)", ons);
        CHECK (ons == offs && ons >= 3, "chord released (%d on %d off)", ons, offs);
    }

    // ------------------------------------------------------------ Hold-a-key mode transposes
    {
        HooksmithProcessor p; FakeHead head; head.playing = false;
        p.setPlayHead (&head); p.setPlayConfigDetails (0, 2, sr, block); p.prepareToPlay (sr, block);
        setParam (p, "genre", 0); setParam (p, "bars", 0); setParam (p, "mode", hs::TriggerMode);
        setParam (p, "lane4_on", 0.0f);
        p.generateAll();
        const auto pat = p.getPattern();
        auto evs = run (p, head, (int) (samplesPerBeat * 4 / block), block, sr, { { 0, juce::MidiMessage::noteOn (1, 65, (juce::uint8) 100) } });
        int firstMelody = -1;
        for (auto& e : evs) if (e.on && e.ch == 2) { firstMelody = e.note; break; }
        CHECK (! pat.lanes[1].empty() && firstMelody == pat.lanes[1].front().pitch + 5, "holding F transposes up 5 (%d vs %d)",
               firstMelody, pat.lanes[1].empty() ? -1 : pat.lanes[1].front().pitch);
        for (auto& e : evs) CHECK (e.ch != 10, "drums lane switched off");
    }

    // ------------------------------------------------------------ Arpeggiator on the chords
    {
        HooksmithProcessor p; FakeHead head;
        p.setPlayHead (&head); p.setPlayConfigDetails (0, 2, sr, block); p.prepareToPlay (sr, block);
        setParam (p, "genre", 0); setParam (p, "bars", 0); setParam (p, "lane1_arpOn", 1.0f); setParam (p, "lane1_arpRate", 7); // 1/8T
        p.generateAll();
        auto evs = run (p, head, (int) (samplesPerBeat * 8 / block), block, sr);
        std::vector<double> t; for (auto& e : evs) if (e.on && e.ch == 1) t.push_back (e.sample);
        CHECK (t.size() >= 20, "arp plays (%zu notes)", t.size());
        double worst = 0;
        for (double x : t) { const double grid = samplesPerBeat / 3.0; const double k = std::round (x / grid); worst = std::max (worst, std::abs (x - k * grid)); }
        CHECK (worst <= 1.0, "arp triplets on the grid (worst %.1f samples)", worst);
        std::printf ("arp: %zu notes, worst grid error %.2f samples\n", t.size(), worst);
    }

    // ------------------------------------------------------------ Separate arps on melody and bass
    {
        HooksmithProcessor p; FakeHead head;
        p.setPlayHead (&head); p.setPlayConfigDetails (0, 2, sr, block); p.prepareToPlay (sr, block);
        setParam (p, "genre", 5); setParam (p, "bars", 0);
        setParam (p, "lane2_arpOn", 1.0f); setParam (p, "lane2_arpRate", 4); setParam (p, "lane2_arpOct", 2);  // melody 1/16, 2 oct
        setParam (p, "lane3_arpOn", 1.0f); setParam (p, "lane3_arpRate", 3);                                  // bass 1/8
        p.generateAll();
        auto evs = run (p, head, (int) (samplesPerBeat * 16 / block), block, sr);
        auto check = [&] (int ch, double grid, const char* name)
        {
            std::vector<double> t; for (auto& e : evs) if (e.on && e.ch == ch) t.push_back (e.sample);
            double worst = 0;
            for (double x : t) { const double k = std::round (x / grid); worst = std::max (worst, std::abs (x - k * grid)); }
            CHECK (t.size() >= 8 && worst <= 1.0, "%s arp: %zu notes, worst %.1f samples off the grid", name, t.size(), worst);
            std::printf ("%s arp: %zu notes, worst grid error %.2f samples\n", name, t.size(), worst);
        };
        check (2, samplesPerBeat / 4.0, "melody");
        check (3, samplesPerBeat / 2.0, "bass");
        int ons = 0, offs = 0; for (auto& e : evs) (e.on ? ons : offs)++;
        setParam (p, "lane2_arpOn", 0.0f); setParam (p, "lane3_arpOn", 0.0f); head.playing = false;
        for (auto& e : run (p, head, 8, block, sr)) (e.on ? ons : offs)++;
        CHECK (ons == offs, "switching the arps off leaves no hanging notes (%d on, %d off)", ons, offs);
    }

    // ------------------------------------------------------------ COLOR and TAPE STOP
    {
        auto render = [&] (HooksmithProcessor& p, FakeHead& head, int blocks, std::vector<float>& out)
        {
            juce::AudioBuffer<float> buf (2, block);
            for (int b = 0; b < blocks; ++b)
            {
                juce::MidiBuffer midi;
                p.processBlock (buf, midi);
                for (int i = 0; i < block; ++i) out.push_back (buf.getSample (0, i));
                head.ppq += block * head.bpm / (60.0 * sr);
            }
        };
        auto rmsOf = [] (const std::vector<float>& v, size_t from, size_t to)
        {
            double s = 0; for (size_t i = from; i < to; ++i) s += (double) v[i] * v[i];
            return std::sqrt (s / (double) std::max<size_t> (1, to - from));
        };
        const int blocks = (int) (samplesPerBeat * 8 / block);

        HooksmithProcessor clean; FakeHead h1;
        clean.setPlayHead (&h1); clean.setPlayConfigDetails (0, 2, sr, block); clean.prepareToPlay (sr, block);
        setParam (clean, "genre", 13); setParam (clean, "bars", 0); clean.generateAll();
        juce::MemoryBlock st; clean.getStateInformation (st);
        std::vector<float> a; render (clean, h1, blocks, a);

        HooksmithProcessor col; FakeHead h2;
        col.setStateInformation (st.getData(), (int) st.getSize());
        col.setPlayHead (&h2); col.setPlayConfigDetails (0, 2, sr, block); col.prepareToPlay (sr, block);
        for (int l = 0; l < 4; ++l)
        {
            setParam (col, HooksmithProcessor::laneParam (l, "colOn"), 1.0f);
            setParam (col, HooksmithProcessor::laneParam (l, "drive"), 0.9f);
            setParam (col, HooksmithProcessor::laneParam (l, "wobble"), 0.8f);
            setParam (col, HooksmithProcessor::laneParam (l, "noise"), 0.5f);
            setParam (col, HooksmithProcessor::laneParam (l, "crush"), 0.6f);
        }
        std::vector<float> b; render (col, h2, blocks, b);
        bool finite = true; double diff = 0;
        for (size_t i = 0; i < b.size(); ++i) { finite = finite && std::isfinite (b[i]) && std::abs (b[i]) < 4.0f; diff += std::abs (b[i] - a[i]); }
        CHECK (finite, "COLOR output stays finite and sane");
        CHECK (diff / (double) b.size() > 0.002, "COLOR changes the sound (avg diff %f)", diff / (double) b.size());
        std::printf ("color: clean rms %.4f, coloured rms %.4f\n", rmsOf (a, 0, a.size()), rmsOf (b, 0, b.size()));

        // Tape stop: 0.5 s to stop -> silence -> release -> sound again
        HooksmithProcessor t; FakeHead h3;
        t.setStateInformation (st.getData(), (int) st.getSize());
        t.setPlayHead (&h3); t.setPlayConfigDetails (0, 2, sr, block); t.prepareToPlay (sr, block);
        setParam (t, "tapeTime", 0.5f); setParam (t, "tapeStart", 0.2f);
        std::vector<float> c;
        render (t, h3, 50, c);                                   // 0.5 s playing
        setParam (t, "tapeStop", 1.0f);
        render (t, h3, 100, c);                                  // 1 s: slows over 0.5 s, then silent
        const size_t s0 = 50 * (size_t) block;
        const double before = rmsOf (c, 0, s0), stopped = rmsOf (c, s0 + (size_t) (0.6 * sr), c.size());
        CHECK (before > 0.003 && stopped < 1e-6, "tape stop goes silent (before %f, stopped %f)", before, stopped);
        setParam (t, "tapeStop", 0.0f);
        const size_t s1 = c.size();
        render (t, h3, 100, c);
        const double after = rmsOf (c, s1 + (size_t) (0.4 * sr), c.size());
        CHECK (after > 0.003, "tape starts again (rms %f)", after);
        std::printf ("tape stop: playing %.4f, stopped %.6f, restarted %.4f\n", before, stopped, after);
    }

    // ------------------------------------------------------------ CAPTURE from a chord plugin, then import a trigger file
    if (auto* fake = std::getenv ("HS_FAKECHORD"))
    {
        HooksmithProcessor p; FakeHead head;
        p.setPlayHead (&head); p.setPlayConfigDetails (0, 2, sr, block); p.prepareToPlay (sr, block);
        juce::String err;
        CHECK (p.loadInstrumentFromFile (hs::ChordsLane, juce::File (fake), err), "load chord plugin: %s", err.toRawUTF8());
        const auto status = p.captureFromChordsPlugin();
        std::printf ("capture: %s\n", status.toRawUTF8());
        CHECK (p.hasCapture(), "captured chords");
        const auto& bank = p.getBank();
        std::printf ("pads:"); for (auto& b : bank) std::printf (" %s", b.name.c_str()); std::printf ("\n");
        CHECK (bank[0].name == "Cmaj7" && (bank[1].name == "C#m" || bank[1].name == "Dbm"), "pads hold the captured chords (%s %s)", bank[0].name.c_str(), bank[1].name.c_str());

        // trigger file: one note per bar, like the Cthulhu pack MIDI (key in its name)
        juce::MidiMessageSequence seq;
        const int trig[] = { 57, 53, 48, 55 };
        for (int b = 0; b < 4; ++b)
        {
            seq.addEvent (juce::MidiMessage::noteOn (1, trig[b], (juce::uint8) 100), b * 4 * 960);
            seq.addEvent (juce::MidiMessage::noteOff (1, trig[b]), (b * 4 + 4) * 960 - 10);
        }
        seq.updateMatchedPairs();
        juce::MidiFile mf; mf.setTicksPerQuarterNote (960); mf.addTrack (seq);
        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("01 Preset (001 Am) - Test.mid");
        f.deleteFile(); { juce::FileOutputStream os (f); mf.writeTo (os); }
        const int i = p.importMidiFile (f, err);
        CHECK (i >= 0, "trigger file imported: %s", err.toRawUTF8());
        if (i >= 0)
        {
            p.selectImport (i);
            juce::String names; for (auto& ch : p.getPattern().chords) if (ch.start < 16) names << ch.name << " ";
            std::printf ("trigger file became: %s\n", names.toRawUTF8());
            CHECK (p.getPattern().chords.size() >= 4, "chords from triggers");
        }
        // Chord Keys mode plays the captured chord for the exact key
        p.selectImport (-1);
        setParam (p, "mode", hs::ChordKeysMode); head.playing = false;
        p.unloadInstrument (hs::ChordsLane);
        auto evs = run (p, head, 6, block, sr, { { 1, juce::MidiMessage::noteOn (1, 61, (juce::uint8) 100) } });
        std::set<int> got; for (auto& e : evs) if (e.on && e.ch == 1) got.insert (e.note);
        CHECK (got == std::set<int> ({ 61, 64, 68 }), "C#4 key plays the captured C#m chord (%zu notes)", got.size());
    }

    // ------------------------------------------------------------ Import MIDI chord packs
    if (auto* dir = std::getenv ("HS_IMPORT_DIR"))
    {
        HooksmithProcessor p; FakeHead head;
        p.setPlayHead (&head); p.setPlayConfigDetails (0, 2, sr, block); p.prepareToPlay (sr, block);
        setParam (p, "key", 0); setParam (p, "bars", 1);
        int ok = 0, keysRight = 0, withKey = 0;
        for (auto& f : juce::File (dir).findChildFiles (juce::File::findFiles, true, "*.mid"))
        {
            juce::String err;
            const int i = p.importMidiFile (f, err);
            if (i < 0) { std::printf ("  %-60s -> %s\n", f.getFileName().substring (0, 60).toRawUTF8(), err.substring (0, 70).toRawUTF8()); continue; }
            ++ok;
            p.selectImport (i);
            const auto& pat = p.getPattern();
            juce::String chords;
            for (auto& ch : pat.chords) if (ch.start < 16) chords << ch.name << " ";
            // original key, from analysing the file itself at its own key
            p.apvts.getParameter ("key")->setValueNotifyingHost (0.0f);
            std::printf ("  %-60s -> playing in C%s: %s\n", f.getFileName().substring (0, 60).toRawUTF8(), pat.minor ? "m" : "", chords.toRawUTF8());
            CHECK (! pat.lanes[0].empty() && ! pat.lanes[1].empty(), "imported chords play with a melody");
        }
        std::printf ("imported %d files\n", ok);
    }

    std::printf ("%d failures\n", failures);
    return failures == 0 ? 0 : 1;
}
