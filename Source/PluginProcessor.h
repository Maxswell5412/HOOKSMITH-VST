#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Music.h"
#include "Arpeggiator.h"
#include "SketchSynth.h"
#include "ColorFX.h"
#include "PartTone.h"
#include "Examples.h"

// -----------------------------------------------------------------------------
// HOOKSMITH — song idea generator with the PROGRESSOR chord engine.
// Writes chords, melody, bass and drums by genre and song section, plays them
// in time with the DAW (or from held keys), hosts one instrument plugin per
// lane, has built-in sketch sounds, an arpeggiator, MIDI out and MIDI export.
// It is itself an instrument plugin, so FLIPSIDE can load it too.
// -----------------------------------------------------------------------------
namespace hs
{
// One hosted instrument (Omnisphere, Serum, FLIPSIDE, ...)
struct HostSlot
{
    std::unique_ptr<juce::AudioPluginInstance> instance;
    juce::PluginDescription desc;
    juce::CriticalSection lock;
    juce::AudioBuffer<float> buffer;
};

// What the audio thread plays (built from a Pattern on the message thread)
struct PlayData
{
    struct Ev { double start, end; int pitch, vel; };
    double length = 16.0;
    std::array<std::vector<Ev>, kNumLanes> lanes;
    std::vector<Ev> sustainedChords;                        // feeds the arpeggiator
    std::array<std::array<int, 8>, 12> bankNotes {};        // Chord Keys voicings (C4 octave)
    std::array<int, 12> bankCount {};
    // Chords captured from a chord plugin (Cthulhu): every key plays its own chord
    bool useKeyMap = false;
    int padBase = 60;
    std::array<std::array<int, 8>, 128> keyNotes {};
    std::array<int, 128> keyCount {};
    // PROG KEYS: the progression's own chords (as voiced), white keys from C3 = chord 1, 2, 3...
    std::vector<std::vector<int>> progChords;
};

enum PlayMode { SyncMode = 0, TriggerMode, ChordKeysMode, ProgKeysMode };
enum LaneSound { SoundAuto = 0, SoundBuiltIn, SoundMidiOnly };
} // namespace hs

class HooksmithProcessor : public juce::AudioProcessor,
                           public juce::ChangeBroadcaster,
                           private juce::Timer
{
public:
    HooksmithProcessor();
    ~HooksmithProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                      { return true; }

    const juce::String getName() const override          { return "HOOKSMITH"; }
    bool acceptsMidi() const override                    { return true; }
    bool producesMidi() const override                   { return true; }
    bool isMidiEffect() const override                   { return false; }
    double getTailLengthSeconds() const override         { return 2.0; }

    int getNumPrograms() override                        { return 1; }
    int getCurrentProgram() override                     { return 0; }
    void setCurrentProgram (int) override                {}
    const juce::String getProgramName (int) override     { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- Generating -----------------------------------------------------------
    void generateAll();                 // new seeds for every unlocked lane
    void refreshNow()                   { timerCallback(); }   // apply parameter changes right away (tools/tests)
    void rerollLane (int lane);         // new seed for one lane
    void setLaneLocked (int lane, bool locked);
    bool isLaneLocked (int lane) const  { return locked[(size_t) lane]; }
    int  getLaneTake (int lane) const   { return takes[(size_t) lane]; }   // 0 new, 1-2 close takes

    // Memory slots: keep up to 4 versions of the melody and the bass line you like
    static constexpr int kSlots = 4;
    static bool laneHasSlots (int lane)  { return lane == hs::ChordsLane || lane == hs::MelodyLane || lane == hs::BassLane; }
    bool slotUsed (int lane, int slot) const;
    int  activeSlot (int lane) const;          // slot that matches what's playing, or -1
    void storeSlot (int lane, int slot);
    void recallSlot (int lane, int slot);
    void clearSlot (int lane, int slot);
    hs::Settings currentSettings() const;
    const hs::Pattern& getPattern() const       { return uiPattern; }   // message thread only
    const std::array<hs::PadChord, 12>& getBank() const { return uiBank; }
    juce::uint32 getPatternVersion() const      { return patternVersion; }
    juce::StringArray progressionMenu() const;   // "Auto" + named progressions of the current genre + imports

    // ---- Imported MIDI chord packs ----------------------------------------------
    int importMidiFile (const juce::File& file, juce::String& error);   // returns the import's index, or -1
    void selectImport (int index);
    void removeImport (int index);
    int getNumImports() const { return (int) imports.size(); }
    int getActiveImport() const { return activeImport; }

    // ---- LOAD EXAMPLE: your own MIDI or audio (WAV/AIFF/MP3...) for a part --------------
    // Chords: becomes an imported progression. Melody / bass: the part copies it; its GENERATE writes new versions.
    juce::String loadExample (int lane, const juce::File& file);   // "" = ok, else what went wrong
    void clearExample (int lane);
    juce::String exampleName (int lane) const;
    static bool isExampleFile (const juce::File& f) { return f.hasFileExtension ("mid;midi;wav;aif;aiff;flac;mp3;ogg;m4a"); }

    // ---- CAPTURE: record the chords a chord plugin (Cthulhu) plays on every key -----
    juce::String captureFromChordsPlugin();   // returns a status line
    void clearCapture();
    bool hasCapture() const;

    // ---- MIDI export (drag and drop) -------------------------------------------
    // lane = 0..3, or -1 for all lanes, -2 for the full song
    juce::File writeMidiFile (int lane);

    // ---- Chord pads -------------------------------------------------------------
    void padPressed (int slot, bool down);

    // ---- Hosted instruments (one per lane) --------------------------------------
    bool loadInstrumentFromFile (int lane, const juce::File& file, juce::String& error);
    bool loadInstrument (int lane, const juce::PluginDescription& desc, juce::String& error);
    void unloadInstrument (int lane);
    juce::AudioPluginInstance* getInstrument (int lane) const { return slots[(size_t) lane].instance.get(); }
    juce::String getInstrumentName (int lane) const;

    juce::AudioProcessorValueTreeState apvts;

    // Live info for the UI
    std::atomic<double> displayPos { -1.0 };        // beats into the pattern, -1 when stopped
    std::atomic<int> displayHeldSlot { -1 };        // chord pad being played
    static constexpr int kArpLanes = 3;                // chords, melody, bass
    std::array<std::atomic<juce::uint32>, kArpLanes> displayArpSteps {};
    std::atomic<double> displayBpm { 120.0 };

    static juce::StringArray rateNames();
    static juce::StringArray playModeNames();
    static juce::StringArray soundNames();
    static juce::String laneParam (int lane, const char* what);
    static juce::StringArray machineNames() { return { "Off", "MPC60", "MPC2000", "MPC3000", "SP-1200" }; }
    // Built-in sounds per part. Menu index 0 = Auto (the genre's sound).
    static const std::vector<int>& patchList (int lane);
    int builtInPatch (int lane) const;     // the patch the part plays now (Auto resolved)
    std::atomic<int> displayTape { 0 };   // 0 live, 1 stopping, 2 stopped, 3 starting
    float uiScale = -1.0f;   // editor size, saved with the project (-1 = fit the screen)

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void timerCallback() override;
    void rebuild();                      // compose + hand the result to the audio thread
    bool settingsChanged (const hs::Settings&) const;
    void prepareSlot (int lane);
    void killLane (int lane, int pos);
    void chordKeyOn (int note, int vel, int pos);
    void chordKeyOff (int note, int pos);
    void progKeyOn (int note, int vel, int pos);
public:
    static int progChordForKey (int note, int numChords);   // -1 = black key
    int heldProgChord() const { return displayHeldProg.load(); }
private:
    std::atomic<int> displayHeldProg { -1 };
    void scheduleLane (int lane, const hs::PlayData& d, double clock0, double clock1,
                       int len, double beatsPerSample, int transpose);
    void emitNote (int lane, bool on, int pitch, int vel, int pos);

    // Parameters
    std::atomic<float> *pGenre, *pSection, *pBars, *pKey, *pScale, *pProg, *pSpice, *pComplexity, *pDensity,
                       *pGroove, *pHumanize, *pMode, *pPlay, *pBpm, *pMidiOut, *pSketchVol,
                       *pFeel, *pFeelAmt, *pPocket, *pDynamics, *pStrum;
    std::array<std::atomic<float>*, kArpLanes> pArpOn {}, pArpRate {}, pArpMode {}, pArpOct {}, pArpGate {};
    std::array<std::atomic<float>*, hs::kNumLanes> pLaneOn {}, pLaneSound {}, pLaneOct {}, pLaneVol {}, pLaneLen {}, pLanePatch {}, pMachine {}, pFlavor {}, pExag {}, pFunk {},
                                                        pPartStrum {}, pPartHp {}, pPartLp {}, pPartGain {}, pPartRes {};
    std::atomic<float>* pMelHarm = nullptr; std::atomic<float>* pMelFlavor = nullptr;
    std::atomic<float>* pChanges = nullptr; std::atomic<float>* pVoicing = nullptr;
    std::atomic<float> *p808Drive = nullptr, *pWobRate = nullptr, *pWobDepth = nullptr, *pStutter = nullptr, *pGlide = nullptr;
    std::array<std::atomic<float>*, hs::kNumLanes> pColOn {}, pDrive {}, pWobble {}, pNoise {}, pCrush {}, pTone {}, pColMix {};
    std::atomic<float> *pTapeStop = nullptr, *pTapeLoop = nullptr, *pTapeTime = nullptr, *pTapeStart = nullptr;

    // Seeds (saved with the project)
    std::array<juce::uint32, hs::kNumLanes> seeds { 1, 2, 3, 4 };
    std::array<bool, hs::kNumLanes> locked {};
    std::array<int, hs::kNumLanes> takes { 2, 2, 2, 2 };   // 0 = new idea, 1 and 2 = close takes of it
    struct Memory { bool used = false; juce::uint32 seed = 0; int take = 0; };
    std::array<std::array<Memory, 4>, hs::kNumLanes> memories {};
    bool seedsDirty = true;
    void nextTake (const std::array<bool, hs::kNumLanes>& lanes);   // the GENERATE cycle
    hs::Settings lastSettings;
    bool haveLast = false;

    std::vector<std::shared_ptr<hs::ImportedProgression>> imports;   // saved with the project
    int activeImport = -1;
    std::array<std::shared_ptr<hs::PartExample>, hs::kNumLanes> examples {};
    bool readMidiNotes (const juce::File& file, std::vector<hs::NoteEv>& notes, double& lengthBeats, juce::String& error) const;
    std::array<std::vector<int>, 128> captured;   // per key, saved with the project

    // Message-thread copies for the UI
    hs::Pattern uiPattern;
    std::array<hs::PadChord, 12> uiBank;
    juce::uint32 patternVersion = 0;

    // Hand-over to the audio thread
    juce::SpinLock swapLock;
    std::unique_ptr<hs::PlayData> pending, current, retired;

    // Audio-thread state
    double sampleRateHz = 44100.0;
    bool prepared = false;
    double freeClock = 0.0;              // always running, in beats
    double internalClock = 0.0;          // used when the DAW isn't playing but PLAY is on
    bool wasRunning = false;
    double lastClock = -1.0;
    double triggerAnchor = 0.0;
    int triggerNote = -1;
    std::array<bool, 128> heldKeys {};
    std::array<std::array<double, 128>, hs::kNumLanes> activeOff {};   // < 0 = not sounding
    std::array<std::array<int, 8>, 128> keyChord {};                    // chord keys: notes per pressed key
    std::array<int, 128> keyChordCount {};
    std::array<int, 128> keyChordSlot {};
    std::array<bool, kArpLanes> arpRouting {};
    bool internalWasOn = false;
    int chunkStart = 0;
    int lastTranspose = 0;
    int lastMode = -1;
    std::array<int, hs::kNumLanes> lastUse { 2, 2, 2, 2 };
    std::array<bool, hs::kNumLanes> laneWasOn { true, true, true, true };

    std::array<juce::MidiBuffer, hs::kNumLanes> laneMidi;   // per chunk
    std::array<juce::MidiBuffer, kArpLanes> arpOuts;
    juce::MidiBuffer outMidi;
    std::array<hs::Arpeggiator, kArpLanes> arps;
    std::array<hs::SketchSynth, hs::kNumLanes> sketch;
    std::array<hs::ColorFX, hs::kNumLanes> color;
    std::array<hs::PartTone, hs::kNumLanes> partTone;
    hs::TapeStop tape;
    juce::AudioBuffer<float> sketchBuf;

    juce::AbstractFifo padFifo { 64 };
    std::array<int, 64> padEvents {};

    juce::AudioPluginFormatManager formatManager;
    std::array<hs::HostSlot, hs::kNumLanes> slots;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HooksmithProcessor)
};
