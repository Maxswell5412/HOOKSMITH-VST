#pragma once

#include "PluginProcessor.h"
#include "UIKit.h"

namespace hsui
{
const juce::Colour laneColour (int lane);

// Piano-roll "phosphor" screen showing the generated section.
class RollScreen : public juce::Component
{
public:
    explicit RollScreen (HooksmithProcessor& p) : proc (p) {}
    void paint (juce::Graphics&) override;
    void setPlayPos (double beats) { if (std::abs (beats - pos) > 1.0e-4) { pos = beats; repaint(); } }
private:
    HooksmithProcessor& proc;
    double pos = -1.0;
};

// One of the 12 chord pads (white keys = scale chords, black keys = borrowed colours)
class ChordPad : public juce::Component, public juce::SettableTooltipClient
{
public:
    ChordPad (HooksmithProcessor& p, int s, bool black) : proc (p), slot (s), isBlack (black) {}
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void setLit (bool l) { if (l != lit) { lit = l; repaint(); } }
private:
    HooksmithProcessor& proc;
    const int slot;
    const bool isBlack;
    bool lit = false, down = false;
};

// Drag this into the DAW to drop the MIDI. Click = save + show the file.
class DragTile : public juce::Component, public juce::SettableTooltipClient
{
public:
    DragTile (HooksmithProcessor& p, int lane, juce::String label, juce::Colour c);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    std::function<void (const juce::String&)> onStatus;
private:
    HooksmithProcessor& proc;
    const int lane;
    juce::String text;
    juce::Colour colour;
    bool dragging = false, pressed = false;
};

// Hold to stop the tape (or click with LATCH on)
class TapeButton : public ui::LedButton
{
public:
    explicit TapeButton (HooksmithProcessor& p) : ui::LedButton ("Tape Stop", ui::Col::red), proc (p) {}
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    bool latch = false;
private:
    void set (bool on);
    HooksmithProcessor& proc;
};

class LaneStrip : public juce::Component
{
public:
    LaneStrip (HooksmithProcessor&, int lane);
    ~LaneStrip() override;
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();
    void setArpBlink (bool lit);
    void mouseDown (const juce::MouseEvent&) override;
    DragTile drag;
    std::function<void()> onArpPressed;
    std::function<void (const juce::String&)> onStatus;

private:
    void chooseInstrument();
    void showWindow (bool show);

    HooksmithProcessor& proc;
    const int lane;
    ui::LedButton onButton;
    ui::LedButton loadButton { "Load", ui::Col::amber, false };
    ui::LedButton showButton { "Show", ui::Col::green };
    ui::LedButton ejectButton { "X", ui::Col::amber, false };
    ui::LedButton newButton { "Generate", ui::Col::amber, false };
    ui::LedButton lockButton { "Lock", ui::Col::red };
    ui::LedButton arpButton { "Arp", ui::Col::amber };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> arpA;
    juce::ComboBox sound, octave;
    ui::Knob vol, len;
    juce::Rectangle<int> lcdArea;
    juce::OwnedArray<ui::LedButton> slotButtons;   // melody + bass: 4 memory slots
    void slotClicked (int slot, bool menu);
    void refreshSlots();
    juce::String lcdText() const;
    juce::String shownLcd;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onA;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> soundA, octA;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> volA, lenA;
    std::unique_ptr<ui::HostedWindow> window;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::AudioPluginInstance* shown = nullptr;
    juce::String status;
};
} // namespace hsui

// The whole interface at its design size (1180 x 1096). The editor scales it to fit the screen.
class HooksmithPanel : public juce::Component,
                       public juce::DragAndDropContainer,
                       public juce::FileDragAndDropTarget,
                       private juce::ChangeListener,
                       private juce::Timer
{
public:
    void tick() { timerCallback(); }   // one UI update (video renderer)
    void mouseDown (const juce::MouseEvent&) override;   // right-click DICE = more dice
    explicit HooksmithPanel (HooksmithProcessor&);
    ~HooksmithPanel() override;
    static constexpr int designW = 1180, designH = 1096;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

private:
    void importFiles (const juce::Array<juce::File>& files);
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void renderBackground();
    void refreshProgressions();
    void setChoice (const juce::String& id, int index);
    void setStatus (const juce::String& s) { statusText = s; statusTicks = 90; repaint(); }

    HooksmithProcessor& proc;
    ui::Look look;
    juce::TooltipWindow tooltips { this, 500 };
    juce::Image background;

    juce::Rectangle<int> headerArea, songArea, screenArea, progArea, grooveArea, laneAreas[hs::kNumLanes], arpArea, exportArea, lcdArea,
                         colorArea, tapeArea, partArea, melGroup;

    // PART TONE (one per part, the panel shows the selected part) + MELODY harmony / flavor
    void selectPartLane (int lane);
    int partLane = 2;
    juce::OwnedArray<ui::LedButton> partTabs;
    ui::Knob pStrum, pHp, pLp, pRes, pGain;
    juce::ComboBox melHarm, melFlavor;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> pStrumA, pHpA, pLpA, pResA, pGainA;
    // 808 (bass tab)
    juce::ComboBox wobRate;
    ui::Knob wobDepth, stutterK, glideK, driveK;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> wobRateA;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> wobDepthA, stutterA, glideA, drive808A;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> melHarmA, melFlavorA;

    // COLOR (one per part, the panel shows the selected part)
    void selectColorLane (int lane);
    int colorLane = 0;
    juce::OwnedArray<ui::LedButton> colorTabs;
    ui::LedButton colorOnButton { "Color On", ui::Col::amber };
    ui::Knob drive, wobble, noise, crush, tone, colMix, machine, flavor, exag, funk;
    int shownMachine = -1;
    juce::Rectangle<int> machineGroup;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> colorOnA;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> driveA, wobbleA, noiseA, crushA, toneA, colMixA, machineA, flavorA, exagA, funkA;

    // TAPE STOP
    hsui::TapeButton tapeButton;
    ui::LedButton latchButton { "Latch", ui::Col::amber };
    ui::LedButton loopStopButton { "Loop End", ui::Col::amber };
    ui::Knob tapeTime, tapeStart;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> loopStopA;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> tapeTimeA, tapeStartA;

    // Header
    juce::ComboBox mode;
    ui::LedButton playButton { "Play", ui::Col::green };
    ui::LedButton generateButton { "Generate All", ui::Col::amber, false };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeA;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> playA;

    // Song
    juce::ComboBox genre, key, scale;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> genreA, keyA, scaleA;
    juce::OwnedArray<ui::LedButton> barButtons, sectionButtons;

    // Screen + PROGRESSOR
    hsui::RollScreen screen;
    juce::ComboBox progression;
    ui::LedButton progPrev { "<", ui::Col::amber, false }, progNext { ">", ui::Col::amber, false };
    ui::LedButton diceButton { "Dice", ui::Col::violet, false };
    juce::ComboBox changesBox, voicingBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> changesA, voicingA;
    juce::Rectangle<int> progReadout;
    void stepProgression (int dir);
    void rollDice (int what);   // 0 progression, 1 key, 2 key + scale, 3 surprise me
    ui::LedButton importButton { "Import MIDI", ui::Col::amber, false };
    ui::LedButton captureButton { "Capture", ui::Col::green };
    std::unique_ptr<juce::FileChooser> importChooser;
    bool dragHover = false;
    juce::OwnedArray<hsui::ChordPad> pads;
    ui::Knob complexity, density, spice;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> complexityA, densityA, spiceA;

    // GROOVE module
    juce::ComboBox feel;
    ui::Knob feelAmt, swing, pocket, dynamics, strum, humanize;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> feelA;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> feelAmtA, swingA, pocketA, dynamicsA, strumA, humanizeA;

    // Lanes
    std::unique_ptr<hsui::LaneStrip> lanes[hs::kNumLanes];

    // Arp (one per melodic part, the panel shows the selected part)
    void selectArpLane (int lane);
    int arpLane = 0;
    juce::OwnedArray<ui::LedButton> arpTabs;
    ui::LedButton arpOnButton { "Arp On", ui::Col::amber };
    juce::ComboBox arpRate, arpMode, arpOct;
    ui::Knob arpGate;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> arpOnA;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> arpRateA, arpModeA, arpOctA;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> arpGateA;
    std::array<juce::uint32, HooksmithProcessor::kArpLanes> lastArpSteps {};
    std::array<int, HooksmithProcessor::kArpLanes> arpBlink {};

    // Export / output
    hsui::DragTile dragAll, dragSong;
    ui::LedButton midiOutButton { "MIDI Out", ui::Col::green };
    ui::Knob sketchVol, bpm;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> midiOutA;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sketchVolA, bpmA;

    std::array<bool, hs::kNumLanes> colorShown {};
    juce::String statusText;
    int statusTicks = 0;
    int shownGenre = -1;
    int shownImports = 0;
    juce::uint32 shownVersion = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HooksmithPanel)
};

class HooksmithEditor : public juce::AudioProcessorEditor
{
public:
    explicit HooksmithEditor (HooksmithProcessor&);
    void resized() override;
private:
    HooksmithProcessor& proc;
    HooksmithPanel panel;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HooksmithEditor)
};
