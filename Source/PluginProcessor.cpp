#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <set>

using namespace hs;

namespace
{
constexpr int kChunk = 64;
constexpr double kHeld = 1.0e17;   // "sounding until the key is released"

const double kRateBeats[] = { 4.0, 2.0, 1.0, 0.5, 0.25, 0.125,
                              2.0 / 3.0, 1.0 / 3.0, 1.0 / 6.0,
                              1.5, 0.75, 1.0 / 12.0 };

const int kOutChannel[] = { 1, 2, 3, 10 };   // MIDI out: chords, melody, bass, drums

juce::StringArray toStringArray (const std::vector<std::string>& v)
{
    juce::StringArray a;
    for (auto& s : v) a.add (juce::String (s));
    return a;
}
} // namespace

// =============================================================================
juce::StringArray HooksmithProcessor::rateNames()
{
    return { "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/4T", "1/8T", "1/16T", "1/4D", "1/8D", "1/32T" };
}

juce::StringArray HooksmithProcessor::playModeNames()
{
    return { "Sync to DAW", "Hold a Key", "Chord Keys" };
}

juce::StringArray HooksmithProcessor::soundNames()
{
    return { "Plugin / Built-in", "Built-in", "MIDI Out Only" };
}

const std::vector<int>& HooksmithProcessor::patchList (int lane)
{
    using P = SketchSynth;
    static const std::vector<int> chords { P::EPiano, P::Piano, P::Pad, P::Strings, P::Pluck, P::Organ, P::SuperSaw, P::LoFiKeys };
    static const std::vector<int> melody { P::FMBell, P::SawLead, P::SquareLead, P::Pluck, P::Flute, P::EPiano, P::Piano, P::SuperSaw };
    static const std::vector<int> bass   { P::Bass808, P::SubBass, P::Reese, P::PluckBass, P::SawBass };
    static const std::vector<int> drums  { P::Kit };
    return lane == 0 ? chords : lane == 1 ? melody : lane == 2 ? bass : drums;
}

// The built-in sound each genre starts with (chords, melody, bass)
static int autoPatch (int genre, int lane)
{
    using P = SketchSynth;
    static const int table[18][3] = {
        { P::Piano,    P::Pluck,      P::SubBass },   // Pop
        { P::Pluck,    P::SawLead,    P::PluckBass }, // Dance Pop
        { P::Organ,    P::SquareLead, P::SawBass },   // Pop Rock
        { P::Strings,  P::Flute,      P::SubBass },   // Classical
        { P::Strings,  P::FMBell,     P::SubBass },   // Cinematic
        { P::Organ,    P::Pluck,      P::PluckBass }, // House / EDM
        { P::SuperSaw, P::SawLead,    P::Reese },     // Future Bass
        { P::SuperSaw, P::Pluck,      P::PluckBass }, // Trance
        { P::EPiano,   P::Flute,      P::SubBass },   // Boom Bap
        { P::Piano,    P::FMBell,     P::Bass808 },   // Trap
        { P::Strings,  P::Piano,      P::Bass808 },   // Drill
        { P::EPiano,   P::SquareLead, P::SubBass },   // R&B
        { P::EPiano,   P::Flute,      P::SubBass },   // Neo-Soul
        { P::LoFiKeys, P::EPiano,     P::SubBass },   // Lo-Fi
        { P::Organ,    P::Piano,      P::SubBass },   // Gospel
        { P::Pluck,    P::FMBell,     P::Bass808 },   // Afrobeats
        { P::Pluck,    P::SquareLead, P::SubBass },   // Reggaeton
        { P::Piano,    P::Pluck,      P::PluckBass }, // Country
    };
    if (lane == 3) return P::Kit;
    return table[juce::jlimit (0, 17, genre)][juce::jlimit (0, 2, lane)];
}

int HooksmithProcessor::builtInPatch (int lane) const
{
    if (lane >= 3) return SketchSynth::Kit;
    const int idx = (int) pLanePatch[(size_t) lane]->load();
    const auto& list = patchList (lane);
    if (idx <= 0 || idx > (int) list.size()) return autoPatch ((int) pGenre->load(), lane);
    return list[(size_t) idx - 1];
}

juce::String HooksmithProcessor::laneParam (int lane, const char* what)
{
    return "lane" + juce::String (lane + 1) + "_" + what;
}

juce::AudioProcessorValueTreeState::ParameterLayout HooksmithProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;
    auto choice = [&] (const char* id, const char* name, const StringArray& items, int def)
    { layout.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, items, def)); };
    auto flt = [&] (const char* id, const char* name, float lo, float hi, float def)
    { layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, NormalisableRange<float> (lo, hi), def)); };

    choice ("genre",   "Genre",   toStringArray (genreNames()), 0);
    choice ("section", "Section", toStringArray (sectionNames()), Hook);
    choice ("bars",    "Bars",    { "4 Bars", "8 Bars", "16 Bars" }, 1);
    choice ("key",     "Key",     toStringArray (keyNames()), 0);
    choice ("scale",   "Scale",   toStringArray (scaleNames()), 0);
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { "prog", 1 }, "Progression", 0, 24, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "spice", 1 }, "Spice", NormalisableRange<float> (0.0f, 1.0f), 0.4f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (
                                                           [] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; })));
    flt ("complexity", "Complexity", 0.0f, 1.0f, 0.5f);
    flt ("density",    "Density",    0.0f, 1.0f, 0.5f);
    flt ("groove",     "Swing",      0.0f, 1.0f, 0.5f);
    flt ("humanize",   "Humanize",   0.0f, 1.0f, 0.3f);

    // GROOVE module
    choice ("feel", "Groove Feel", toStringArray (feelNames()), 0);
    flt ("feelAmt",  "Groove Amount", 0.0f, 1.0f, 0.6f);
    flt ("pocket",   "Pocket (Push / Lay Back)", -1.0f, 1.0f, 0.0f);
    flt ("dynamics", "Dynamics", 0.0f, 1.0f, 0.5f);
    flt ("strum",    "Strum", 0.0f, 1.0f, 0.3f);

    choice ("mode", "Play Mode", playModeNames(), SyncMode);
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "play", 1 }, "Play (without DAW)", false));
    flt ("bpm", "Tempo (without DAW)", 60.0f, 180.0f, 120.0f);


    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "midiOut", 1 }, "MIDI Out", true));
    flt ("sketchVol", "Built-in Volume", 0.0f, 1.0f, 0.7f);

    // TAPE STOP (whole output)
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "tapeStop", 1 }, "Tape Stop", false));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "tapeLoop", 1 }, "Tape Stop at Loop End", false));
    flt ("tapeTime",  "Tape Stop Time", 0.1f, 3.0f, 0.8f);
    flt ("tapeStart", "Tape Start Time", 0.05f, 2.0f, 0.25f);

    for (int l = 0; l < kNumLanes; ++l)
    {
        const auto n = String (laneNames()[(size_t) l]) + " ";
        layout.add (std::make_unique<AudioParameterBool>   (ParameterID { laneParam (l, "on"), 1 }, n + "On", true));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { laneParam (l, "sound"), 1 }, n + "Sound", soundNames(), 0));
        layout.add (std::make_unique<AudioParameterInt>    (ParameterID { laneParam (l, "oct"), 1 }, n + "Octave", -2, 2, 0));
        layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { laneParam (l, "vol"), 1 }, n + "Volume", NormalisableRange<float> (0.0f, 1.0f), 0.8f));
        {
            StringArray names { "Auto" };
            for (int p : patchList (l)) names.add (String (SketchSynth::patchName (p)).toLowerCase().replace ("fm", "FM").replace ("lo-fi", "Lo-Fi"));
            for (auto& nm : names) nm = nm.substring (0, 1).toUpperCase() + nm.substring (1);
            layout.add (std::make_unique<AudioParameterChoice> (ParameterID { laneParam (l, "patch"), 1 }, n + "Built-in Sound", names, 0));
        }
        layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { laneParam (l, "len"), 1 }, n + "Note Length",
                                                            NormalisableRange<float> (0.1f, 2.0f, 0.0f, 0.6f), 1.0f,
                                                            AudioParameterFloatAttributes().withStringFromValueFunction (
                                                                [] (float v, int) { return String (juce::roundToInt (v * 100.0f)) + "%"; })));

        // COLOR strip for this part
        layout.add (std::make_unique<AudioParameterBool>  (ParameterID { laneParam (l, "colOn"), 1 }, n + "Color On", false));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { laneParam (l, "drive"), 1 },  n + "Drive",  NormalisableRange<float> (0.0f, 1.0f), 0.35f));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { laneParam (l, "wobble"), 1 }, n + "Wobble", NormalisableRange<float> (0.0f, 1.0f), 0.25f));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { laneParam (l, "noise"), 1 },  n + "Noise",  NormalisableRange<float> (0.0f, 1.0f), 0.15f));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { laneParam (l, "crush"), 1 },  n + "Crush",  NormalisableRange<float> (0.0f, 1.0f), 0.0f));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { laneParam (l, "tone"), 1 },   n + "Tone",   NormalisableRange<float> (0.0f, 1.0f), 0.25f));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { laneParam (l, "colMix"), 1 }, n + "Color Mix", NormalisableRange<float> (0.0f, 1.0f), 1.0f));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { laneParam (l, "machine"), 1 }, n + "Machine", machineNames(), 0));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { laneParam (l, "flavor"), 1 }, n + "Machine Flavor",
                                                           NormalisableRange<float> (0.0f, 1.0f), 0.6f,
                                                           AudioParameterFloatAttributes().withStringFromValueFunction (
                                                               [] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; })));
        for (auto [id, nm] : { std::pair<const char*, const char*> { "exag", "Machine Exaggerate" }, { "funk", "Machine Funk" } })
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { laneParam (l, id), 1 }, n + nm, NormalisableRange<float> (0.0f, 1.0f), 0.0f,
                                                               AudioParameterFloatAttributes().withStringFromValueFunction (
                                                                   [] (float v, int) { return String (roundToInt (v * 100.0f)) + "%"; })));

        if (l < kArpLanes)   // every melodic part has its own arpeggiator
        {
            static const int defRate[] = { 4, 4, 3 };
            layout.add (std::make_unique<AudioParameterBool>   (ParameterID { laneParam (l, "arpOn"), 1 },   n + "Arp On", false));
            layout.add (std::make_unique<AudioParameterChoice> (ParameterID { laneParam (l, "arpRate"), 1 }, n + "Arp Rate", rateNames(), defRate[l]));
            layout.add (std::make_unique<AudioParameterChoice> (ParameterID { laneParam (l, "arpMode"), 1 }, n + "Arp Mode", Arpeggiator::modeNames(), 0));
            layout.add (std::make_unique<AudioParameterInt>    (ParameterID { laneParam (l, "arpOct"), 1 },  n + "Arp Octaves", 1, 4, 1));
            layout.add (std::make_unique<AudioParameterFloat>  (ParameterID { laneParam (l, "arpGate"), 1 }, n + "Arp Gate", NormalisableRange<float> (0.05f, 1.0f), 0.6f));
        }
    }
    return layout;
}

// =============================================================================
HooksmithProcessor::HooksmithProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    formatManager.addDefaultFormats();

    auto raw = [this] (const juce::String& id) { return apvts.getRawParameterValue (id); };
    pGenre = raw ("genre"); pSection = raw ("section"); pBars = raw ("bars"); pKey = raw ("key");
    pSpice = raw ("spice");
    pScale = raw ("scale"); pProg = raw ("prog"); pComplexity = raw ("complexity"); pDensity = raw ("density");
    pGroove = raw ("groove"); pHumanize = raw ("humanize"); pMode = raw ("mode"); pPlay = raw ("play");
    pBpm = raw ("bpm"); pMidiOut = raw ("midiOut"); pSketchVol = raw ("sketchVol");
    pFeel = raw ("feel"); pFeelAmt = raw ("feelAmt"); pPocket = raw ("pocket"); pDynamics = raw ("dynamics"); pStrum = raw ("strum");
    for (int l = 0; l < kArpLanes; ++l)
    {
        pArpOn[(size_t) l]   = raw (laneParam (l, "arpOn"));
        pArpRate[(size_t) l] = raw (laneParam (l, "arpRate"));
        pArpMode[(size_t) l] = raw (laneParam (l, "arpMode"));
        pArpOct[(size_t) l]  = raw (laneParam (l, "arpOct"));
        pArpGate[(size_t) l] = raw (laneParam (l, "arpGate"));
    }
    for (int l = 0; l < kNumLanes; ++l)
    {
        pLaneOn[(size_t) l]    = raw (laneParam (l, "on"));
        pLaneSound[(size_t) l] = raw (laneParam (l, "sound"));
        pLaneOct[(size_t) l]   = raw (laneParam (l, "oct"));
        pLaneVol[(size_t) l]   = raw (laneParam (l, "vol"));
        pLaneLen[(size_t) l]   = raw (laneParam (l, "len"));
        pLanePatch[(size_t) l] = raw (laneParam (l, "patch"));
        pColOn[(size_t) l]  = raw (laneParam (l, "colOn"));
        pDrive[(size_t) l]  = raw (laneParam (l, "drive"));
        pWobble[(size_t) l] = raw (laneParam (l, "wobble"));
        pNoise[(size_t) l]  = raw (laneParam (l, "noise"));
        pCrush[(size_t) l]  = raw (laneParam (l, "crush"));
        pTone[(size_t) l]   = raw (laneParam (l, "tone"));
        pColMix[(size_t) l] = raw (laneParam (l, "colMix"));
        pMachine[(size_t) l] = raw (laneParam (l, "machine"));
        pFlavor[(size_t) l]  = raw (laneParam (l, "flavor"));
        pExag[(size_t) l]    = raw (laneParam (l, "exag"));
        pFunk[(size_t) l]    = raw (laneParam (l, "funk"));
    }
    pTapeStop = raw ("tapeStop"); pTapeLoop = raw ("tapeLoop"); pTapeTime = raw ("tapeTime"); pTapeStart = raw ("tapeStart");

    // Fresh random seeds for a new instance, so every new HOOKSMITH starts with a new idea
    auto& r = juce::Random::getSystemRandom();
    for (auto& s : seeds) s = (juce::uint32) r.nextInt() | 1u;

    for (auto& lane : activeOff) lane.fill (-1.0);
    for (auto& b : laneMidi) b.ensureSize (2048);
    for (auto& b : arpOuts) b.ensureSize (512);
    outMidi.ensureSize (8192);

    rebuild();
    startTimerHz (20);
}

HooksmithProcessor::~HooksmithProcessor()
{
    stopTimer();
    for (auto& s : slots) s.instance.reset();
}

bool HooksmithProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

// =============================================================================
// Generating
// =============================================================================
hs::Settings HooksmithProcessor::currentSettings() const
{
    Settings s;
    s.genre = (int) pGenre->load();
    s.section = (int) pSection->load();
    static const int bars[] = { 4, 8, 16 };
    s.bars = bars[juce::jlimit (0, 2, (int) pBars->load())];
    s.key = (int) pKey->load();
    s.scale = (int) pScale->load();
    s.progression = (int) pProg->load();
    s.spice = std::round (pSpice->load() * 50.0f) / 50.0f;
    if (s.progression > (int) progressionNames (s.genre).size()) s.progression = 0;
    if (juce::isPositiveAndBelow (activeImport, (int) imports.size()))
        s.imported = imports[(size_t) activeImport];   // an imported MIDI file replaces the PROGRESSOR
    s.complexity = pComplexity->load();
    s.density = pDensity->load();
    s.groove = pGroove->load();
    s.humanize = pHumanize->load();
    s.feel = (int) pFeel->load();
    s.feelAmount = pFeelAmt->load();
    s.pocket = pPocket->load();
    s.dynamics = pDynamics->load();
    s.strum = pStrum->load();
    s.harmonySeed = seeds[0]; s.melodySeed = seeds[1]; s.bassSeed = seeds[2]; s.drumSeed = seeds[3];
    for (int l = 0; l < kNumLanes; ++l)
    {
        s.variant[(size_t) l] = takes[(size_t) l] % 3;
        s.noteLength[(size_t) l] = pLaneLen[(size_t) l]->load();
        const bool colourOn = pColOn[(size_t) l]->load() > 0.5f;
        s.machine[(size_t) l] = colourOn ? (int) pMachine[(size_t) l]->load() : 0;
        s.machineFlavor[(size_t) l] = std::round (pFlavor[(size_t) l]->load() * 50.0f) / 50.0f;   // 2% steps: no rebuild storm while turning
        s.machineExaggerate[(size_t) l] = std::round (pExag[(size_t) l]->load() * 50.0f) / 50.0f;
        s.machineFunk[(size_t) l] = std::round (pFunk[(size_t) l]->load() * 50.0f) / 50.0f;
    }
    return s;
}

bool HooksmithProcessor::settingsChanged (const Settings& s) const
{
    const auto& o = lastSettings;
    return s.genre != o.genre || s.section != o.section || s.bars != o.bars || s.key != o.key
        || s.scale != o.scale || s.progression != o.progression || s.spice != o.spice
        || std::abs (s.complexity - o.complexity) > 1.0e-4f || std::abs (s.density - o.density) > 1.0e-4f
        || std::abs (s.groove - o.groove) > 1.0e-4f || std::abs (s.humanize - o.humanize) > 1.0e-4f
        || s.feel != o.feel || std::abs (s.feelAmount - o.feelAmount) > 1.0e-4f || std::abs (s.pocket - o.pocket) > 1.0e-4f
        || std::abs (s.dynamics - o.dynamics) > 1.0e-4f || std::abs (s.strum - o.strum) > 1.0e-4f
        || s.harmonySeed != o.harmonySeed || s.melodySeed != o.melodySeed
        || s.bassSeed != o.bassSeed || s.drumSeed != o.drumSeed || s.imported != o.imported
        || s.variant != o.variant || s.noteLength != o.noteLength
        || s.machine != o.machine || s.machineFlavor != o.machineFlavor
        || s.machineExaggerate != o.machineExaggerate || s.machineFunk != o.machineFunk;
}

void HooksmithProcessor::timerCallback()
{
    const auto s = currentSettings();
    if (! haveLast || seedsDirty || settingsChanged (s))
        rebuild();

    // free whatever the audio thread finished with
    std::unique_ptr<PlayData> trash;
    {
        const juce::SpinLock::ScopedTryLockType tl (swapLock);
        if (tl.isLocked() && pending == nullptr) trash = std::move (retired);
    }
}

void HooksmithProcessor::rebuild()
{
    const auto s = currentSettings();
    uiPattern = compose (s);
    uiBank = chordBank (s);
    ++patternVersion;

    auto d = std::make_unique<PlayData>();
    d->length = uiPattern.lengthBeats;
    for (int l = 0; l < kNumLanes; ++l)
    {
        auto& v = d->lanes[(size_t) l];
        for (auto& n : uiPattern.lanes[(size_t) l])
            v.push_back ({ n.start, n.start + n.len, n.pitch, n.vel });
        std::sort (v.begin(), v.end(), [] (const PlayData::Ev& a, const PlayData::Ev& b) { return a.start < b.start; });
    }
    for (auto& ch : uiPattern.chords)
        for (int n : ch.notes)
            d->sustainedChords.push_back ({ ch.start, ch.start + ch.len - 0.02, n, 96 });
    std::sort (d->sustainedChords.begin(), d->sustainedChords.end(),
               [] (const PlayData::Ev& a, const PlayData::Ev& b) { return a.start < b.start; });

    // Captured chords (Cthulhu) go on the pads, unless an imported MIDI file is playing
    if (hasCapture() && s.imported == nullptr)
    {
        int bestBase = 60, bestCount = -1;
        for (int base = 24; base <= 96; base += 12)
        {
            int cnt = 0;
            for (int k = 0; k < 12; ++k) if (base + k < 128 && ! captured[(size_t) (base + k)].empty()) ++cnt;
            if (cnt > bestCount) { bestCount = cnt; bestBase = base; }
        }
        for (int k = 0; k < 12; ++k)
        {
            hs::PadChord pc;
            const auto& notes = captured[(size_t) (bestBase + k)];
            if (! notes.empty())
            {
                pc.name = nameChord (notes, &pc.rootPc);
                pc.roman = "KEY " + juce::MidiMessage::getMidiNoteName (bestBase + k, true, true, 3).toStdString();
                pc.fixedNotes = notes;
            }
            uiBank[(size_t) k] = pc;
        }
        d->useKeyMap = true;
        d->padBase = bestBase;
        for (int key = 0; key < 128; ++key)
        {
            const int cnt = juce::jmin (8, (int) captured[(size_t) key].size());
            d->keyCount[(size_t) key] = cnt;
            for (int i = 0; i < cnt; ++i) d->keyNotes[(size_t) key][(size_t) i] = captured[(size_t) key][(size_t) i];
        }
    }

    // Chord Keys voicings: every pad voiced near the home chord, so moving between them is smooth
    const auto home = voiceChord (uiBank[0].rootPc, uiBank[0].intervals, s.genre, {}, 62);
    for (int slot = 0; slot < 12; ++slot)
    {
        auto v = uiBank[(size_t) slot].fixedNotes;   // imported chords keep the pack's own voicing
        if (v.empty() && ! uiBank[(size_t) slot].intervals.empty())
            v = voiceChord (uiBank[(size_t) slot].rootPc, uiBank[(size_t) slot].intervals, s.genre, home, 62);
        const int count = juce::jmin (8, (int) v.size());
        d->bankCount[(size_t) slot] = count;
        for (int i = 0; i < count; ++i) d->bankNotes[(size_t) slot][(size_t) i] = v[(size_t) i];
    }

    std::unique_ptr<PlayData> trash;
    {
        const juce::SpinLock::ScopedLockType sl (swapLock);
        trash = std::move (pending);           // an unconsumed older pattern
        pending = std::move (d);
        if (current == nullptr) current = std::move (pending);
    }

    lastSettings = s;
    haveLast = true;
    seedsDirty = false;
    sendChangeMessage();
}

// How alike two patterns are on one part: 0 = nothing in common, 1 = the same.
static double laneSimilarity (const Pattern& a, const Pattern& b, int lane)
{
    auto keys = [lane] (const Pattern& p)
    {
        std::set<long long> k;
        for (auto& n : p.lanes[(size_t) lane])
            k.insert ((long long) std::llround (n.start * 4.0) * 1000 + (lane == DrumsLane ? n.pitch : n.pitch % 12));
        return k;
    };
    const auto ka = keys (a), kb = keys (b);
    if (ka.empty() && kb.empty()) return 1.0;
    size_t both = 0;
    for (auto k : ka) both += kb.count (k);
    double sim = (double) both / (double) (ka.size() + kb.size() - both);
    if (lane == ChordsLane)   // the chord sequence itself counts most
    {
        const size_t n = std::min (a.chords.size(), b.chords.size());
        size_t same = 0;
        for (size_t i = 0; i < n; ++i) same += a.chords[i].name == b.chords[i].name ? 1 : 0;
        sim = 0.35 * sim + 0.65 * (n > 0 ? (double) same / (double) std::max (a.chords.size(), b.chords.size()) : 1.0);
    }
    return sim;
}

// GENERATE cycle per part: a brand-new idea, then two close takes of it, then a new idea...
// New ideas are picked to be as different as possible from what is playing now (same key).
void HooksmithProcessor::nextTake (const std::array<bool, kNumLanes>& lanes)
{
    bool anyNew = false;
    std::array<bool, kNumLanes> fresh {};
    for (int l = 0; l < kNumLanes; ++l)
        if (lanes[(size_t) l])
        {
            // chords: every press is a brand-new progression (a "close take" of chords sounded like the same thing)
            takes[(size_t) l] = l == ChordsLane ? 0 : (takes[(size_t) l] + 1) % 3;
            fresh[(size_t) l] = takes[(size_t) l] == 0;
            anyNew = anyNew || fresh[(size_t) l];
        }
    // the chords' own GENERATE always writes new chords: leave a chosen progression / imported file for the PROGRESSOR
    bool onlyChords = lanes[ChordsLane];
    for (int l = 1; l < kNumLanes; ++l) onlyChords = onlyChords && ! lanes[(size_t) l];
    if (onlyChords)
    {
        activeImport = -1;
        if (auto* prm = apvts.getParameter ("prog"); prm != nullptr && pProg->load() > 0.5f) prm->setValueNotifyingHost (0.0f);
    }

    if (anyNew)
    {
        const Pattern now = uiPattern;
        auto& rnd = juce::Random::getSystemRandom();
        auto best = seeds;
        double bestScore = 1.0e9;
        for (int attempt = 0; attempt < 10; ++attempt)
        {
            auto trial = seeds;
            for (int l = 0; l < kNumLanes; ++l)
                if (fresh[(size_t) l]) trial[(size_t) l] = (juce::uint32) rnd.nextInt() | 1u;
            auto s = currentSettings();
            s.harmonySeed = trial[0]; s.melodySeed = trial[1]; s.bassSeed = trial[2]; s.drumSeed = trial[3];
            for (int l = 0; l < kNumLanes; ++l) if (fresh[(size_t) l]) s.variant[(size_t) l] = 0;
            const auto p = compose (s);
            double score = 0;
            for (int l = 0; l < kNumLanes; ++l)
                if (fresh[(size_t) l]) score += laneSimilarity (now, p, l);
            if (score < bestScore) { bestScore = score; best = trial; }
        }
        seeds = best;
    }
    seedsDirty = true;
    rebuild();
}

// ---------------------------------------------------------------- memory slots
bool HooksmithProcessor::slotUsed (int lane, int slot) const
{
    return laneHasSlots (lane) && juce::isPositiveAndBelow (slot, kSlots) && memories[(size_t) lane][(size_t) slot].used;
}

int HooksmithProcessor::activeSlot (int lane) const
{
    if (! laneHasSlots (lane)) return -1;
    for (int i = 0; i < kSlots; ++i)
    {
        const auto& sl = memories[(size_t) lane][(size_t) i];
        if (sl.used && sl.seed == seeds[(size_t) lane] && sl.take == takes[(size_t) lane] % 3) return i;
    }
    return -1;
}

void HooksmithProcessor::storeSlot (int lane, int slot)
{
    if (! laneHasSlots (lane) || ! juce::isPositiveAndBelow (slot, kSlots)) return;
    memories[(size_t) lane][(size_t) slot] = { true, seeds[(size_t) lane], takes[(size_t) lane] % 3 };
}

void HooksmithProcessor::recallSlot (int lane, int slot)
{
    if (! slotUsed (lane, slot)) return;
    const auto& sl = memories[(size_t) lane][(size_t) slot];
    seeds[(size_t) lane] = sl.seed;
    takes[(size_t) lane] = sl.take;
    seedsDirty = true;
    rebuild();
}

void HooksmithProcessor::clearSlot (int lane, int slot)
{
    if (laneHasSlots (lane) && juce::isPositiveAndBelow (slot, kSlots)) memories[(size_t) lane][(size_t) slot] = {};
}

void HooksmithProcessor::generateAll()
{
    std::array<bool, kNumLanes> lanes {};
    for (int l = 0; l < kNumLanes; ++l) lanes[(size_t) l] = ! locked[(size_t) l];
    nextTake (lanes);
}

void HooksmithProcessor::rerollLane (int lane)
{
    if (! juce::isPositiveAndBelow (lane, (int) kNumLanes)) return;
    std::array<bool, kNumLanes> lanes {};
    lanes[(size_t) lane] = true;
    nextTake (lanes);
}

void HooksmithProcessor::setLaneLocked (int lane, bool l)
{
    if (juce::isPositiveAndBelow (lane, (int) kNumLanes)) locked[(size_t) lane] = l;
    sendChangeMessage();
}

juce::StringArray HooksmithProcessor::progressionMenu() const
{
    juce::StringArray a { "AUTO  -  PROGRESSOR picks per section" };
    for (auto& n : progressionNames ((int) pGenre->load())) a.add (juce::String (n));
    for (auto& imp : imports) a.add ("IMPORTED:  " + juce::String (imp->name));
    return a;
}

int HooksmithProcessor::importMidiFile (const juce::File& file, juce::String& error)
{
    juce::FileInputStream in (file);
    juce::MidiFile mf;
    if (! in.openedOk() || ! mf.readFrom (in))
    {
        error = "Couldn't read " + file.getFileName();
        return -1;
    }
    const int tpq = mf.getTimeFormat();
    if (tpq <= 0) { error = "Unsupported MIDI timing in " + file.getFileName(); return -1; }

    std::vector<NoteEv> notes;
    double lastTick = 0;
    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        juce::MidiMessageSequence seq (*mf.getTrack (t));
        seq.updateMatchedPairs();
        for (auto* e : seq)
        {
            lastTick = juce::jmax (lastTick, e->message.getTimeStamp());
            if (! e->message.isNoteOn() || e->message.getChannel() == 10) continue;
            const double on = e->message.getTimeStamp();
            const double off = e->noteOffObject != nullptr ? e->noteOffObject->message.getTimeStamp() : on + tpq;
            notes.push_back ({ on / tpq, juce::jmax (0.05, (off - on) / tpq), e->message.getNoteNumber(), e->message.getVelocity() });
        }
    }
    if (notes.empty()) { error = file.getFileName() + " has no notes"; return -1; }

    // One note at a time = a trigger file for a chord plugin (Cthulhu packs). Turn each trigger into its captured chord.
    int maxPoly = 0;
    for (auto& a : notes)
    {
        int poly = 0;
        for (auto& b : notes) if (b.start <= a.start + 0.02 && b.start + b.len > a.start + 0.02) ++poly;
        maxPoly = juce::jmax (maxPoly, poly);
    }
    juce::String suffix;
    if (maxPoly <= 1)
    {
        if (! hasCapture())
        {
            error = file.getFileName() + " is a trigger file (one note per chord). Load the pack's Cthulhu preset on the CHORDS part, press CAPTURE, then import it again";
            return -1;
        }
        std::vector<NoteEv> chords;
        for (auto& n : notes)
        {
            const auto& c = captured[(size_t) juce::jlimit (0, 127, n.pitch)];
            for (int p : c) chords.push_back ({ n.start, n.len, p, n.vel });
        }
        if (chords.empty()) { error = "The captured chords don't cover the keys used in " + file.getFileName(); return -1; }
        notes.swap (chords);
        suffix = " (Cthulhu)";
    }

    const auto nm = file.getFileNameWithoutExtension().toStdString();
    int hintPc = -1; bool hintMinor = false;
    const bool hasHint = keyFromName (nm, hintPc, hintMinor);
    auto imp = analyseImport (notes, lastTick / tpq, nm + suffix.toStdString(), hasHint ? hintPc : -1, hasHint ? (hintMinor ? 1 : 0) : -1);
    if (imp->spans.empty()) { error = "No chords found in " + file.getFileName(); return -1; }
    if (imports.size() >= 40) imports.erase (imports.begin());
    imports.push_back (imp);
    return (int) imports.size() - 1;
}

bool HooksmithProcessor::hasCapture() const
{
    for (auto& c : captured) if (! c.empty()) return true;
    return false;
}

void HooksmithProcessor::clearCapture()
{
    for (auto& c : captured) c.clear();
    seedsDirty = true;
    rebuild();
}

juce::String HooksmithProcessor::captureFromChordsPlugin()
{
    auto& slot = slots[ChordsLane];
    if (slot.instance == nullptr)
        return "LOAD CTHULHU (OR ANY CHORD PLUGIN) ON THE CHORDS PART FIRST";

    struct Head : juce::AudioPlayHead
    {
        double ppq = 0, bpm = 120;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p; p.setBpm (bpm); p.setPpqPosition (ppq); p.setIsPlaying (true);
            p.setTimeSignature (TimeSignature { 4, 4 });
            return p;
        }
    } head;

    const double sr = sampleRateHz > 0 ? sampleRateHz : 44100.0;
    const int holdBlocks = (int) (0.35 * sr / kChunk);   // hold each key long enough for an arp to play all its notes
    std::array<std::vector<int>, 128> got;
    int found = 0;
    const juce::String name = slot.instance->getName();

    suspendProcessing (true);
    {
        const juce::ScopedLock sl (slot.lock);
        auto* inst = slot.instance.get();
        if (! prepared) { inst->setRateAndBufferSizeDetails (sr, kChunk); inst->prepareToPlay (sr, kChunk); }
        juce::AudioBuffer<float> buf (juce::jmax (2, juce::jmax (inst->getTotalNumInputChannels(), inst->getTotalNumOutputChannels())), kChunk);
        inst->setPlayHead (&head);
        juce::MidiBuffer m;
        auto run = [&] (int blocks, std::set<int>* collect)
        {
            for (int b = 0; b < blocks; ++b)
            {
                buf.clear();
                inst->processBlock (buf, m);
                if (collect != nullptr)
                    for (const auto meta : m)
                        if (meta.getMessage().isNoteOn()) collect->insert (meta.getMessage().getNoteNumber());
                m.clear();
                head.ppq += kChunk * head.bpm / (60.0 * sr);
            }
        };
        for (int key = 24; key <= 96; ++key)
        {
            std::set<int> notes;
            m.clear();
            m.addEvent (juce::MidiMessage::noteOn (1, key, (juce::uint8) 100), 0);
            run (holdBlocks, &notes);
            m.addEvent (juce::MidiMessage::noteOff (1, key), 0);
            run (24, nullptr);
            m.addEvent (juce::MidiMessage::allNotesOff (1), 0);
            run (4, nullptr);
            if (notes.size() >= 2)    // a single note back = no chord on this key
            {
                got[(size_t) key].assign (notes.begin(), notes.end());
                if (got[(size_t) key].size() > 8) got[(size_t) key].resize (8);
                ++found;
            }
        }
        inst->reset();
        inst->setPlayHead (getPlayHead());
    }
    suspendProcessing (false);

    if (found == 0)
        return "NO CHORDS CAME OUT OF " + name.toUpperCase() + ". LOAD A CHORD PRESET IN IT FIRST";
    captured = got;
    seedsDirty = true;
    rebuild();
    return "CAPTURED " + juce::String (found) + " CHORDS FROM " + name.toUpperCase();
}

void HooksmithProcessor::selectImport (int index)
{
    activeImport = juce::isPositiveAndBelow (index, (int) imports.size()) ? index : -1;
    seedsDirty = true;
    rebuild();
}

void HooksmithProcessor::removeImport (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) imports.size())) return;
    imports.erase (imports.begin() + index);
    if (activeImport == index) activeImport = -1;
    else if (activeImport > index) --activeImport;
    seedsDirty = true;
    rebuild();
}

// =============================================================================
// Playback
// =============================================================================
void HooksmithProcessor::prepareToPlay (double sampleRate, int)
{
    sampleRateHz = sampleRate;
    prepared = true;
    sketch[0].prepare (sampleRate, SketchSynth::Keys);
    sketch[1].prepare (sampleRate, SketchSynth::Lead);
    sketch[2].prepare (sampleRate, SketchSynth::Bass);
    sketch[3].prepare (sampleRate, SketchSynth::Drums);
    sketchBuf.setSize (2, kChunk);
    for (int l = 0; l < kNumLanes; ++l) color[(size_t) l].prepare (sampleRate, (juce::uint32) (l * 7919 + 17));
    tape.prepare (sampleRate);

    for (int l = 0; l < kNumLanes; ++l)
    {
        const juce::ScopedLock sl (slots[(size_t) l].lock);
        prepareSlot (l);
    }
    for (auto& lane : activeOff) lane.fill (-1.0);
    wasRunning = false;
}

void HooksmithProcessor::releaseResources()
{
    prepared = false;
    for (auto& s : slots)
    {
        const juce::ScopedLock sl (s.lock);
        if (s.instance != nullptr) s.instance->releaseResources();
    }
}

void HooksmithProcessor::prepareSlot (int lane)
{
    auto& s = slots[(size_t) lane];
    const int ch = juce::jmax (2, s.instance != nullptr ? juce::jmax (s.instance->getTotalNumInputChannels(),
                                                                     s.instance->getTotalNumOutputChannels()) : 2);
    s.buffer.setSize (ch, kChunk, false, true, false);
    if (s.instance != nullptr && prepared)
    {
        s.instance->setRateAndBufferSizeDetails (sampleRateHz, kChunk);
        s.instance->prepareToPlay (sampleRateHz, kChunk);
    }
}

void HooksmithProcessor::padPressed (int slot, bool down)
{
    if (! juce::isPositiveAndBelow (slot, 12)) return;
    const auto scope = padFifo.write (1);
    const int value = down ? slot + 1 : -(slot + 1);
    if (scope.blockSize1 > 0) padEvents[(size_t) scope.startIndex1] = value;
    else if (scope.blockSize2 > 0) padEvents[(size_t) scope.startIndex2] = value;
}

// Sends a note to the lane's sound (instrument / built-in / arp) and to MIDI out.
void HooksmithProcessor::emitNote (int lane, bool on, int pitch, int vel, int pos)
{
    pitch = juce::jlimit (0, 127, pitch);
    const auto msg = on ? juce::MidiMessage::noteOn (1, pitch, (juce::uint8) juce::jlimit (1, 127, vel))
                        : juce::MidiMessage::noteOff (1, pitch);

    if (lane < kArpLanes && arpRouting[(size_t) lane])
    {
        arps[(size_t) lane].handle (msg);   // this part's arp plays it
        return;
    }
    laneMidi[(size_t) lane].addEvent (msg, pos);
    if (pMidiOut->load() > 0.5f || (int) pLaneSound[(size_t) lane]->load() == SoundMidiOnly)
    {
        auto out = msg;
        out.setChannel (kOutChannel[lane]);
        outMidi.addEvent (out, chunkStart + pos);
    }
}

void HooksmithProcessor::killLane (int lane, int pos)
{
    for (int p = 0; p < 128; ++p)
        if (activeOff[(size_t) lane][(size_t) p] >= 0.0)
        {
            emitNote (lane, false, p, 0, pos);
            activeOff[(size_t) lane][(size_t) p] = -1.0;
        }
    if (lane == ChordsLane)
    {
        keyChordCount.fill (0);
        displayHeldSlot.store (-1);
    }
}

void HooksmithProcessor::chordKeyOn (int note, int vel, int pos)
{
    const auto* d = current.get();
    if (d == nullptr) return;
    const int slot = note % 12;
    const bool fromMap = d->useKeyMap && d->keyCount[(size_t) note] > 0;   // captured Cthulhu chord for this exact key
    const int shift = fromMap ? 12 * (int) pLaneOct[0]->load()
                              : (note / 12 - 5) * 12 + 12 * (int) pLaneOct[0]->load();
    const int n = fromMap ? d->keyCount[(size_t) note] : d->bankCount[(size_t) slot];

    chordKeyOff (note, pos);
    int count = 0;
    for (int i = 0; i < n; ++i)
    {
        const int src = fromMap ? d->keyNotes[(size_t) note][(size_t) i] : d->bankNotes[(size_t) slot][(size_t) i];
        const int p = juce::jlimit (0, 127, src + shift);
        if (activeOff[0][(size_t) p] >= 0.0) emitNote (ChordsLane, false, p, 0, pos);
        emitNote (ChordsLane, true, p, vel, pos);
        activeOff[0][(size_t) p] = kHeld;
        keyChord[(size_t) note][(size_t) count++] = p;
    }
    keyChordCount[(size_t) note] = count;
    displayHeldSlot.store (slot);
}

void HooksmithProcessor::chordKeyOff (int note, int pos)
{
    for (int i = 0; i < keyChordCount[(size_t) note]; ++i)
    {
        const int p = keyChord[(size_t) note][(size_t) i];
        if (activeOff[0][(size_t) p] >= kHeld * 0.5)
        {
            emitNote (ChordsLane, false, p, 0, pos);
            activeOff[0][(size_t) p] = -1.0;
        }
    }
    if (keyChordCount[(size_t) note] > 0 && displayHeldSlot.load() == note % 12)
        displayHeldSlot.store (-1);
    keyChordCount[(size_t) note] = 0;
}

void HooksmithProcessor::scheduleLane (int lane, const PlayData& d, double c0, double c1, int len,
                                       double bps, int transpose)
{
    auto& offs = activeOff[(size_t) lane];
    auto posOf = [&] (double t) { return juce::jlimit (0, len - 1, (int) ((t - c0) / bps)); };

    for (int p = 0; p < 128; ++p)
        if (offs[(size_t) p] >= 0.0 && offs[(size_t) p] < kHeld * 0.5 && offs[(size_t) p] < c1)
        {
            emitNote (lane, false, p, 0, posOf (offs[(size_t) p]));
            offs[(size_t) p] = -1.0;
        }

    const auto& evs = (lane == ChordsLane && arpRouting[0]) ? d.sustainedChords : d.lanes[(size_t) lane];
    if (evs.empty() || d.length <= 0.0) return;

    const int shift = lane == DrumsLane ? 0 : transpose + 12 * (int) pLaneOct[(size_t) lane]->load();
    const auto k0 = (juce::int64) std::floor (c0 / d.length);
    const auto k1 = (juce::int64) std::floor (c1 / d.length);
    for (auto k = k0; k <= k1; ++k)
    {
        const double base = (double) k * d.length;
        const double l0 = c0 - base, l1 = c1 - base;
        auto it = std::lower_bound (evs.begin(), evs.end(), l0 - 1.0e-9,
                                    [] (const PlayData::Ev& e, double t) { return e.start < t; });
        for (; it != evs.end() && it->start < l1 - 1.0e-9; ++it)
        {
            const int pitch = juce::jlimit (0, 127, it->pitch + shift);
            const int pos = posOf (base + it->start);
            if (offs[(size_t) pitch] >= 0.0) emitNote (lane, false, pitch, 0, pos);
            emitNote (lane, true, pitch, it->vel, pos);
            offs[(size_t) pitch] = base + it->end;
        }
    }
}

void HooksmithProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    buffer.clear();
    outMidi.clear();

    {
        const juce::SpinLock::ScopedTryLockType tl (swapLock);
        if (tl.isLocked() && pending != nullptr && retired == nullptr)
        {
            retired = std::move (current);
            current = std::move (pending);
        }
    }
    const PlayData* d = current.get();

    // ---- Tempo / transport ----------------------------------------------------
    double bpm = pBpm->load(), hostPpq = 0.0;
    bool hostPlaying = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            hostPlaying = pos->getIsPlaying();
            if (auto p = pos->getPpqPosition()) hostPpq = *p;
        }
    bpm = juce::jlimit (20.0, 400.0, bpm);
    displayBpm.store (bpm);
    const double bps = bpm / (60.0 * sampleRateHz);

    const int mode = juce::jlimit (0, 2, (int) pMode->load());
    const bool internalPlay = pPlay->load() > 0.5f;
    if (internalPlay && ! internalWasOn) internalClock = 0.0;
    internalWasOn = internalPlay;


    // Lane sound routing
    std::array<int, kNumLanes> use {};   // 0 none, 1 plugin, 2 built-in
    for (int l = 0; l < kNumLanes; ++l)
    {
        const int snd = (int) pLaneSound[(size_t) l]->load();
        const bool hasPlugin = slots[(size_t) l].instance != nullptr;
        use[(size_t) l] = snd == SoundMidiOnly ? 0 : (snd == SoundAuto && hasPlugin) ? 1 : 2;
    }

    const double sketchGain = pSketchVol->load();
    float* outL = buffer.getWritePointer (0);
    float* outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1) : nullptr;

    for (int start = 0; start < numSamples; start += kChunk)
    {
        const int len = juce::jmin (kChunk, numSamples - start);
        chunkStart = start;
        for (auto& b : laneMidi) b.clear();
        for (auto& b : arpOuts) b.clear();

        // ---- State changes (always handled at the top of a chunk) ---------------
        if (mode != lastMode)
        {
            for (int l = 0; l < kNumLanes; ++l) killLane (l, 0);
            triggerNote = -1;
            heldKeys.fill (false);
            lastMode = mode;
        }
        for (int l = 0; l < kArpLanes; ++l)
        {
            const bool on = pArpOn[(size_t) l]->load() > 0.5f;
            if (on != arpRouting[(size_t) l])
            {
                killLane (l, 0);                         // using the old routing
                arps[(size_t) l].stop (arpOuts[(size_t) l], 0);
                arps[(size_t) l].clearHeld();
                arpRouting[(size_t) l] = on;
            }
        }
        for (int l = 0; l < kNumLanes; ++l)
        {
            const bool on = pLaneOn[(size_t) l]->load() > 0.5f;
            if (laneWasOn[(size_t) l] && ! on) killLane (l, 0);
            laneWasOn[(size_t) l] = on;
            if (use[(size_t) l] != lastUse[(size_t) l])
            {
                killLane (l, 0);
                laneMidi[(size_t) l].addEvent (juce::MidiMessage::allNotesOff (1), 0);
            }
        }

        // ---- Chord pads from the UI ----------------------------------------------
        if (start == 0)
        {
            const int ready = padFifo.getNumReady();
            if (ready > 0)
            {
                const auto scope = padFifo.read (ready);
                const int base = (d != nullptr && d->useKeyMap) ? d->padBase : 60;
                auto handle = [&] (int v) { if (v > 0) chordKeyOn (base + v - 1, 100, 0); else chordKeyOff (base - v - 1, 0); };
                for (int i = 0; i < scope.blockSize1; ++i) handle (padEvents[(size_t) (scope.startIndex1 + i)]);
                for (int i = 0; i < scope.blockSize2; ++i) handle (padEvents[(size_t) (scope.startIndex2 + i)]);
            }
        }

        // ---- Incoming MIDI ---------------------------------------------------------
        for (const auto meta : midi)
        {
            if (meta.samplePosition < start || meta.samplePosition >= start + len) continue;
            const auto m = meta.getMessage();
            const int pos = meta.samplePosition - start;

            if (mode == ChordKeysMode)
            {
                if (m.isNoteOn())       chordKeyOn (m.getNoteNumber(), m.getVelocity(), pos);
                else if (m.isNoteOff()) chordKeyOff (m.getNoteNumber(), pos);
                else                    laneMidi[0].addEvent (m, pos);
            }
            else if (mode == TriggerMode)
            {
                if (m.isNoteOn())
                {
                    heldKeys[(size_t) m.getNoteNumber()] = true;
                    if (triggerNote < 0) triggerAnchor = freeClock + pos * bps;   // pattern starts from bar 1
                    triggerNote = m.getNoteNumber();
                }
                else if (m.isNoteOff())
                {
                    heldKeys[(size_t) m.getNoteNumber()] = false;
                    if (m.getNoteNumber() == triggerNote)
                    {
                        triggerNote = -1;
                        for (int n = 127; n >= 0; --n) if (heldKeys[(size_t) n]) { triggerNote = n; break; }
                    }
                }
                else laneMidi[0].addEvent (m, pos);
            }
            else
            {
                // Sync mode: play your instrument live along with the pattern
                if (m.isNoteOnOrOff())
                {
                    if (arpRouting[0]) arps[0].handle (m);
                    else            laneMidi[0].addEvent (m, pos);
                }
                else laneMidi[0].addEvent (m, pos);
            }
        }

        // ---- Pattern ---------------------------------------------------------------
        bool running = false;
        double c0 = 0.0;
        int transpose = 0;
        if (mode == SyncMode)
        {
            running = hostPlaying || internalPlay;
            c0 = (hostPlaying ? hostPpq : internalClock) + start * bps;
        }
        else if (mode == TriggerMode)
        {
            running = triggerNote >= 0;
            c0 = freeClock + start * bps - triggerAnchor;
            transpose = triggerNote - 60;
        }
        const double c1 = c0 + len * bps;

        if (running && d != nullptr)
        {
            if (wasRunning && std::abs (c0 - lastClock) > 0.05)   // loop / jump in the DAW
                for (int l = 0; l < kNumLanes; ++l) killLane (l, 0);
            if (transpose != lastTranspose)
                for (int l = 0; l < kNumLanes; ++l) if (l != DrumsLane) killLane (l, 0);
            lastTranspose = transpose;

            for (int l = 0; l < kNumLanes; ++l)
                if (laneWasOn[(size_t) l])
                    scheduleLane (l, *d, c0, c1, len, bps, transpose);

            lastClock = c1;
            wasRunning = true;
            displayPos.store (std::fmod (std::fmod (c0, d->length) + d->length, d->length));
        }
        else
        {
            if (wasRunning)
                for (int l = 0; l < kNumLanes; ++l) killLane (l, 0);
            wasRunning = false;
            if (start == 0) displayPos.store (-1.0);
        }

        // ---- Arpeggiator -------------------------------------------------------------
        for (int l = 0; l < kArpLanes; ++l)
        {
            auto& ao = arpOuts[(size_t) l];
            if (arpRouting[(size_t) l])
            {
                const double a0 = running ? c0 : freeClock + start * bps;
                const int rateIdx = juce::jlimit (0, (int) std::size (kRateBeats) - 1, (int) pArpRate[(size_t) l]->load());
                arps[(size_t) l].process (ao, a0, len, bps, kRateBeats[rateIdx], (int) pArpMode[(size_t) l]->load(),
                                          (int) pArpOct[(size_t) l]->load(), pArpGate[(size_t) l]->load());
            }
            for (const auto meta : ao)   // also carries the note-offs when an arp is switched off
            {
                const auto m = meta.getMessage();
                laneMidi[(size_t) l].addEvent (m, meta.samplePosition);
                if (pMidiOut->load() > 0.5f || (int) pLaneSound[(size_t) l]->load() == SoundMidiOnly)
                {
                    auto out = m; out.setChannel (kOutChannel[l]);
                    outMidi.addEvent (out, start + meta.samplePosition);
                }
            }
        }

        // ---- Sound: each part renders into its own buffer, gets its COLOR, then is mixed --
        for (int l = 0; l < kNumLanes; ++l)
        {
            const int u = lastUse[(size_t) l] != use[(size_t) l] ? lastUse[(size_t) l] : use[(size_t) l];
            const float vol = pLaneVol[(size_t) l]->load();
            sketchBuf.clear();
            float* bl = sketchBuf.getWritePointer (0);
            float* br = sketchBuf.getWritePointer (1);
            bool hasAudio = false;

            if (u == 1)
            {
                auto& slot = slots[(size_t) l];
                const juce::ScopedTryLock tl (slot.lock);
                if (tl.isLocked() && slot.instance != nullptr)
                {
                    juce::AudioBuffer<float> view (slot.buffer.getArrayOfWritePointers(), slot.buffer.getNumChannels(), len);
                    view.clear();
                    slot.instance->setPlayHead (getPlayHead());
                    slot.instance->processBlock (view, laneMidi[(size_t) l]);
                    const int outs = slot.instance->getTotalNumOutputChannels();
                    if (outs > 0)
                    {
                        juce::FloatVectorOperations::copy (bl, view.getReadPointer (0), len);
                        juce::FloatVectorOperations::copy (br, view.getReadPointer (juce::jmin (1, outs - 1)), len);
                        hasAudio = true;
                    }
                }
            }
            else if (u == 2)
            {
                sketch[(size_t) l].setPatch (builtInPatch (l));
                sketch[(size_t) l].render (laneMidi[(size_t) l], bl, br, len, (float) sketchGain);
                hasAudio = true;
            }
            else
            {
                sketch[(size_t) l].allNotesOff();
            }
            lastUse[(size_t) l] = use[(size_t) l];

            if (hasAudio)
            {
                ColorSettings cs;
                cs.on     = pColOn[(size_t) l]->load() > 0.5f;
                cs.drive  = pDrive[(size_t) l]->load();
                cs.wobble = pWobble[(size_t) l]->load();
                cs.noise  = pNoise[(size_t) l]->load();
                cs.crush  = pCrush[(size_t) l]->load();
                cs.tone   = pTone[(size_t) l]->load();
                cs.mix    = pColMix[(size_t) l]->load();
                cs.machine = (int) pMachine[(size_t) l]->load();
                cs.flavor  = pFlavor[(size_t) l]->load();
                cs.exaggerate = pExag[(size_t) l]->load();
                cs.funk       = pFunk[(size_t) l]->load();
                color[(size_t) l].process (bl, br, len, cs);

                juce::FloatVectorOperations::addWithMultiply (outL + start, bl, vol, len);
                if (outR != nullptr) juce::FloatVectorOperations::addWithMultiply (outR + start, br, vol, len);
                else                 juce::FloatVectorOperations::addWithMultiply (outL + start, br, vol, len);
            }
        }

        // ---- TAPE STOP on the whole output --------------------------------------------
        {
            bool wantStop = pTapeStop->load() > 0.5f;
            const float stopSec = pTapeTime->load();
            if (pTapeLoop->load() > 0.5f && running && d != nullptr)
            {
                // reach a full stop exactly at the end of every loop, start again on the "one"
                const double stopBeats = juce::jlimit (0.25, 4.0, stopSec * bpm / 60.0);
                const double posInLoop = std::fmod (std::fmod (c0, d->length) + d->length, d->length);
                if (posInLoop >= d->length - stopBeats) wantStop = true;
            }
            tape.process (outL + start, outR != nullptr ? outR + start : nullptr, len, wantStop, stopSec, pTapeStart->load());
            displayTape.store ((int) tape.getState());
        }

        freeClock += len * bps;
        if (internalPlay && ! hostPlaying) internalClock += len * bps;
    }

    for (int l = 0; l < kArpLanes; ++l) displayArpSteps[(size_t) l].store (arps[(size_t) l].getStepsPlayed());
    midi.swapWith (outMidi);
}

// =============================================================================
// Hosting
// =============================================================================
bool HooksmithProcessor::loadInstrumentFromFile (int lane, const juce::File& file, juce::String& error)
{
    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* format : formatManager.getFormats())
        if (format->fileMightContainThisPluginType (file.getFullPathName()))
            format->findAllTypesForFile (found, file.getFullPathName());

    if (found.isEmpty())
    {
        error = "No VST3/AU plugin found in " + file.getFileName();
        return false;
    }
    auto* chosen = found.getFirst();
    for (auto* desc : found)
        if (desc->isInstrument) { chosen = desc; break; }
    return loadInstrument (lane, *chosen, error);
}

bool HooksmithProcessor::loadInstrument (int lane, const juce::PluginDescription& desc, juce::String& error)
{
    jassert (juce::MessageManager::getInstance()->isThisTheMessageThread());
    if (! juce::isPositiveAndBelow (lane, (int) kNumLanes)) return false;

    if (desc.fileOrIdentifier.containsIgnoreCase ("HOOKSMITH"))
    {
        error = "HOOKSMITH can't load itself";
        return false;
    }

    auto instance = formatManager.createPluginInstance (desc, sampleRateHz, kChunk, error);
    if (instance == nullptr)
    {
        if (error.isEmpty()) error = "Couldn't open " + desc.name;
        return false;
    }
    if (instance->getBusCount (false) > 0)
    {
        auto layout = instance->getBusesLayout();
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::stereo();
        if (instance->checkBusesLayoutSupported (layout))
            instance->setBusesLayout (layout);
    }

    std::unique_ptr<juce::AudioPluginInstance> old;
    auto& slot = slots[(size_t) lane];
    suspendProcessing (true);
    {
        const juce::ScopedLock sl (slot.lock);
        old = std::move (slot.instance);
        slot.instance = std::move (instance);
        slot.desc = desc;
        prepareSlot (lane);
    }
    suspendProcessing (false);
    if (old != nullptr) old->releaseResources();
    old.reset();
    sendChangeMessage();
    return true;
}

void HooksmithProcessor::unloadInstrument (int lane)
{
    if (! juce::isPositiveAndBelow (lane, (int) kNumLanes)) return;
    std::unique_ptr<juce::AudioPluginInstance> old;
    auto& slot = slots[(size_t) lane];
    suspendProcessing (true);
    {
        const juce::ScopedLock sl (slot.lock);
        old = std::move (slot.instance);
        slot.desc = {};
    }
    suspendProcessing (false);
    old.reset();
    sendChangeMessage();
}

juce::String HooksmithProcessor::getInstrumentName (int lane) const
{
    auto* i = getInstrument (lane);
    return i != nullptr ? i->getName() : juce::String();
}

// =============================================================================
// MIDI files for drag and drop
// =============================================================================
juce::File HooksmithProcessor::writeMidiFile (int lane)
{
    const auto s = currentSettings();
    const Pattern song = lane == -2 ? composeSong (s) : Pattern();
    const Pattern& p = lane == -2 ? song : uiPattern;

    constexpr int ppq = 960;
    const double bpm = displayBpm.load();

    auto makeTrack = [&] (int l, bool withTempo)
    {
        juce::MidiMessageSequence seq;
        if (withTempo)
        {
            seq.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / bpm)), 0);
            seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0);
        }
        seq.addEvent (juce::MidiMessage::textMetaEvent (3, "HOOKSMITH " + juce::String (laneNames()[(size_t) l])), 0);
        const int shift = l == DrumsLane ? 0 : 12 * (int) pLaneOct[(size_t) l]->load();
        const int ch = kOutChannel[l];
        for (auto& n : p.lanes[(size_t) l])
        {
            const int pitch = juce::jlimit (0, 127, n.pitch + shift);
            seq.addEvent (juce::MidiMessage::noteOn (ch, pitch, (juce::uint8) n.vel), n.start * ppq);
            seq.addEvent (juce::MidiMessage::noteOff (ch, pitch), (n.start + n.len) * ppq);
        }
        seq.addEvent (juce::MidiMessage::endOfTrack(), p.lengthBeats * ppq);
        seq.updateMatchedPairs();
        return seq;
    };

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (ppq);
    if (lane >= 0)
    {
        mf.addTrack (makeTrack (lane, true));
    }
    else
    {
        juce::MidiMessageSequence tempo;
        tempo.addEvent (juce::MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / bpm)), 0);
        tempo.addEvent (juce::MidiMessage::timeSignatureMetaEvent (4, 4), 0);
        mf.addTrack (tempo);
        for (int l = 0; l < kNumLanes; ++l)
            if (! p.lanes[(size_t) l].empty()) mf.addTrack (makeTrack (l, false));
    }

    const juce::String what = lane == -2 ? "Full Song" : lane == -1 ? "All" : juce::String (laneNames()[(size_t) lane]);
    const juce::String section = lane == -2 ? "Song" : juce::String (sectionNames()[(size_t) s.section]);
    const juce::String name = "HOOKSMITH " + juce::String (genreNames()[(size_t) s.genre]) + " " + section + " "
                            + juce::String (keyNames()[(size_t) s.key]) + (p.minor ? "m" : "") + " " + what;

    auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("HOOKSMITH");
    dir.createDirectory();
    auto file = dir.getChildFile (juce::File::createLegalFileName (name.replace ("/", "-")) + ".mid");
    file.deleteFile();
    if (auto stream = file.createOutputStream())
    {
        mf.writeTo (*stream);
        stream->flush();
    }
    return file;
}

// =============================================================================
// State
// =============================================================================
void HooksmithProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement root ("HooksmithState");
    if (auto params = apvts.copyState().createXml())
        root.addChildElement (params.release());

    root.setAttribute ("uiScale", (double) uiScale);
    root.setAttribute ("activeImport", activeImport);
    if (hasCapture())
    {
        juce::String txt;
        for (int k = 0; k < 128; ++k)
            if (! captured[(size_t) k].empty())
            {
                txt << k << ":";
                for (int p : captured[(size_t) k]) txt << p << ".";
                txt << ";";
            }
        root.setAttribute ("captured", txt);
    }
    auto* sx = root.createNewChildElement ("Seeds");
    for (int l = 0; l < kNumLanes; ++l)
    {
        sx->setAttribute ("s" + juce::String (l), juce::String ((juce::int64) seeds[(size_t) l]));
        sx->setAttribute ("lock" + juce::String (l), locked[(size_t) l]);
        sx->setAttribute ("take" + juce::String (l), takes[(size_t) l]);
        for (int k = 0; k < kSlots; ++k)
            if (const auto& sl = memories[(size_t) l][(size_t) k]; sl.used)
                sx->setAttribute ("slot" + juce::String (l) + "_" + juce::String (k), juce::String ((juce::int64) sl.seed) + ":" + juce::String (sl.take));
    }

    if (! imports.empty())
    {
        auto* ix = root.createNewChildElement ("Imports");
        for (auto& imp : imports)
        {
            auto* e = ix->createNewChildElement ("Import");
            e->setAttribute ("name", juce::String (imp->name));
            e->setAttribute ("length", imp->lengthBeats);
            juce::String txt;
            for (auto& n : imp->notes)
                txt << juce::String (n.start, 4) << "," << juce::String (n.len, 4) << "," << n.pitch << "," << n.vel << ";";
            e->setAttribute ("notes", txt);
        }
    }

    for (int l = 0; l < kNumLanes; ++l)
    {
        auto& slot = slots[(size_t) l];
        if (slot.instance == nullptr) continue;
        auto* inst = root.createNewChildElement ("Instrument");
        inst->setAttribute ("lane", l);
        inst->addChildElement (slot.desc.createXml().release());
        juce::MemoryBlock st;
        slot.instance->getStateInformation (st);
        inst->setAttribute ("state", st.toBase64Encoding());
    }
    copyXmlToBinary (root, destData);
}

void HooksmithProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto root = getXmlFromBinary (data, sizeInBytes);
    if (root == nullptr || ! root->hasTagName ("HooksmithState")) return;

    if (auto* params = root->getChildByName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*params));
    uiScale = (float) root->getDoubleAttribute ("uiScale", -1.0);
    activeImport = root->getIntAttribute ("activeImport", -1);
    for (auto& c : captured) c.clear();
    for (auto& tok : juce::StringArray::fromTokens (root->getStringAttribute ("captured"), ";", ""))
    {
        const int key = tok.upToFirstOccurrenceOf (":", false, false).getIntValue();
        if (! juce::isPositiveAndBelow (key, 128)) continue;
        for (auto& p : juce::StringArray::fromTokens (tok.fromFirstOccurrenceOf (":", false, false), ".", ""))
            if (p.isNotEmpty()) captured[(size_t) key].push_back (p.getIntValue());
    }

    if (auto* sx = root->getChildByName ("Seeds"))
        for (int l = 0; l < kNumLanes; ++l)
        {
            seeds[(size_t) l] = (juce::uint32) sx->getStringAttribute ("s" + juce::String (l), juce::String (l + 1)).getLargeIntValue();
            locked[(size_t) l] = sx->getBoolAttribute ("lock" + juce::String (l), false);
            takes[(size_t) l] = juce::jlimit (0, 2, sx->getIntAttribute ("take" + juce::String (l), 0));
            for (int k = 0; k < kSlots; ++k)
            {
                const auto v = sx->getStringAttribute ("slot" + juce::String (l) + "_" + juce::String (k));
                memories[(size_t) l][(size_t) k] = v.isEmpty() ? Memory {}
                    : Memory { true, (juce::uint32) v.upToFirstOccurrenceOf (":", false, false).getLargeIntValue(),
                             juce::jlimit (0, 2, v.fromFirstOccurrenceOf (":", false, false).getIntValue()) };
            }
        }

    imports.clear();
    if (auto* ix = root->getChildByName ("Imports"))
        for (auto* e : ix->getChildWithTagNameIterator ("Import"))
        {
            std::vector<NoteEv> notes;
            for (auto& tok : juce::StringArray::fromTokens (e->getStringAttribute ("notes"), ";", ""))
            {
                auto f = juce::StringArray::fromTokens (tok, ",", "");
                if (f.size() == 4) notes.push_back ({ f[0].getDoubleValue(), f[1].getDoubleValue(), f[2].getIntValue(), f[3].getIntValue() });
            }
            if (! notes.empty())
                imports.push_back (analyseImport (notes, e->getDoubleAttribute ("length", 16.0), e->getStringAttribute ("name").toStdString()));
        }

    for (auto* inst : root->getChildWithTagNameIterator ("Instrument"))
    {
        const int lane = inst->getIntAttribute ("lane", -1);
        juce::PluginDescription desc;
        if (auto* dx = inst->getChildElement (0); dx != nullptr && desc.loadFromXml (*dx))
        {
            juce::String error;
            if (loadInstrument (lane, desc, error))
            {
                juce::MemoryBlock st;
                if (st.fromBase64Encoding (inst->getStringAttribute ("state")))
                    slots[(size_t) lane].instance->setStateInformation (st.getData(), (int) st.getSize());
            }
        }
    }
    seedsDirty = true;
    rebuild();
}

// =============================================================================
juce::AudioProcessorEditor* HooksmithProcessor::createEditor()
{
    return new HooksmithEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new HooksmithProcessor();
}
