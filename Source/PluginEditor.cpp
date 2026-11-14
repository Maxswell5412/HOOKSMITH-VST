#include "PluginEditor.h"

using namespace ui;
using namespace hsui;

namespace
{
constexpr int kW = HooksmithPanel::designW, kH = HooksmithPanel::designH;

using SA = juce::AudioProcessorValueTreeState::SliderAttachment;
using CA = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
using BA = juce::AudioProcessorValueTreeState::ButtonAttachment;

void addItems (juce::ComboBox& box, const std::vector<std::string>& items)
{
    int id = 1;
    for (auto& s : items) box.addItem (juce::String (s), id++);
}
} // namespace

const juce::Colour hsui::laneColour (int lane)
{
    switch (lane)
    {
        case hs::ChordsLane: return Col::amber;
        case hs::MelodyLane: return Col::cyan;
        case hs::BassLane:   return Col::green;
        default:             return Col::violet;
    }
}

// =============================================================================
// RollScreen
// =============================================================================
void RollScreen::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    lcdBox (g, r);
    const auto& p = proc.getPattern();
    if (p.lengthBeats <= 0) return;

    const auto area = r.reduced (10.0f, 6.0f);
    const auto chordStrip = area.withHeight (24.0f);
    const auto drumArea = area.withTop (area.getBottom() - 58.0f).withTrimmedBottom (16.0f);
    const auto noteArea = area.withTrimmedTop (30.0f).withBottom (drumArea.getY() - 6.0f);
    const auto xOf = [&] (double beat) { return area.getX() + (float) (beat / p.lengthBeats) * area.getWidth(); };

    // grid
    for (int beat = 0; beat <= (int) p.lengthBeats; ++beat)
    {
        const bool bar = beat % 4 == 0;
        g.setColour (Col::amber.withAlpha (bar ? 0.16f : 0.05f));
        g.drawVerticalLine ((int) xOf (beat), noteArea.getY(), drumArea.getBottom());
        if (bar && beat < (int) p.lengthBeats && (p.bars <= 8 || (beat / 4) % 2 == 0))
        {
            g.setFont (Fonts::mono (10.0f));
            g.setColour (Col::amber.withAlpha (0.35f));
            g.drawText (juce::String (beat / 4 + 1), juce::Rectangle<float> (xOf (beat) + 3.0f, drumArea.getBottom() + 1.0f, 30.0f, 13.0f),
                        juce::Justification::centredLeft);
        }
    }

    // chord names
    for (auto& ch : p.chords)
    {
        const float x0 = xOf (ch.start), x1 = xOf (ch.start + ch.len);
        auto cell = juce::Rectangle<float> (x0, chordStrip.getY(), x1 - x0, chordStrip.getHeight()).reduced (1.5f, 1.0f);
        const bool now = pos >= ch.start && pos < ch.start + ch.len;
        g.setColour (Col::amber.withAlpha (now ? 0.28f : 0.08f));
        g.fillRoundedRectangle (cell, 3.0f);
        const float fh = cell.getWidth() > 60 ? 15.0f : cell.getWidth() > 36 ? 12.0f : 9.5f;
        glowText (g, ch.name, cell, Fonts::mono (fh), now ? juce::Colour (0xffffd9a0) : Col::amber, juce::Justification::centred);
    }

    // notes
    int lo = 127, hi = 0;
    for (int l = 0; l < 3; ++l)
        for (auto& n : p.lanes[(size_t) l]) { lo = juce::jmin (lo, n.pitch); hi = juce::jmax (hi, n.pitch); }
    if (lo > hi) { lo = 48; hi = 72; }
    lo -= 1; hi += 1;
    const float rowH = noteArea.getHeight() / (float) (hi - lo + 1);
    const auto yOf = [&] (int pitch) { return noteArea.getBottom() - (float) (pitch - lo + 1) * rowH; };

    const int order[] = { hs::ChordsLane, hs::BassLane, hs::MelodyLane };
    for (int l : order)
    {
        const auto col = laneColour (l);
        for (auto& n : p.lanes[(size_t) l])
        {
            auto rr = juce::Rectangle<float> (xOf (n.start), yOf (n.pitch), juce::jmax (2.0f, xOf (n.start + n.len) - xOf (n.start) - 1.0f),
                                              juce::jmax (2.0f, rowH - 0.6f));
            const bool playing = pos >= n.start && pos < n.start + n.len;
            const float a = l == hs::ChordsLane ? 0.45f : 0.9f;
            if (playing) { g.setColour (col.withAlpha (0.35f)); g.fillRoundedRectangle (rr.expanded (2.0f), 2.5f); }
            g.setColour (col.withAlpha (playing ? 1.0f : a));
            g.fillRoundedRectangle (rr, 1.5f);
        }
    }

    // drums: 4 rows (kick / snare+clap / hats / toms+cymbals)
    const auto& drums = p.lanes[hs::DrumsLane];
    const float dRow = drumArea.getHeight() / 4.0f;
    g.setColour (Col::violet.withAlpha (0.08f));
    g.fillRect (drumArea);
    for (auto& n : drums)
    {
        int row = 3;
        if (n.pitch == 36) row = 0;
        else if (n.pitch == 37 || n.pitch == 38 || n.pitch == 39) row = 1;
        else if (n.pitch == 42 || n.pitch == 46 || n.pitch == 70) row = 2;
        const float x = xOf (n.start);
        const float hgt = (dRow - 2.0f) * (0.4f + 0.6f * (float) n.vel / 127.0f);
        const bool playing = pos >= n.start && pos < n.start + 0.12;
        g.setColour (Col::violet.withAlpha (playing ? 1.0f : 0.75f));
        g.fillRect (x, drumArea.getY() + row * dRow + (dRow - hgt) * 0.5f, 2.0f, hgt);
    }
    if (drums.empty())
    {
        g.setFont (Fonts::mono (11.0f));
        g.setColour (Col::violet.withAlpha (0.5f));
        g.drawText ("NO DRUMS IN THIS SECTION / GENRE", drumArea, juce::Justification::centred);
    }

    // playhead
    if (pos >= 0.0)
    {
        const float x = xOf (pos);
        g.setColour (Col::amber.withAlpha (0.2f));
        g.fillRect (x - 3.0f, area.getY(), 6.0f, area.getHeight());
        g.setColour (juce::Colour (0xffffe0b0));
        g.fillRect (x - 0.75f, area.getY(), 1.5f, area.getHeight());
    }

    // scanlines
    g.setColour (juce::Colours::black.withAlpha (0.18f));
    for (float y = r.getY() + 2.0f; y < r.getBottom(); y += 3.0f)
        g.drawHorizontalLine ((int) y, r.getX() + 2.0f, r.getRight() - 2.0f);
}

// =============================================================================
// ChordPad
// =============================================================================
void ChordPad::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.5f);
    const auto& pc = proc.getBank()[(size_t) slot];
    const bool on = lit || down;

    g.setColour (juce::Colours::black.withAlpha (0.6f));
    g.fillRoundedRectangle (r.translated (0.0f, on ? 0.5f : 2.0f), 5.0f);
    const auto body = r.translated (0.0f, on ? 1.0f : 0.0f);
    const juce::Colour top = isBlack ? juce::Colour (0xff23252a) : juce::Colour (0xff4a4e56);
    const juce::Colour bot = isBlack ? juce::Colour (0xff111215) : juce::Colour (0xff2a2c31);
    g.setGradientFill (juce::ColourGradient (on ? top.brighter (0.15f) : top, body.getTopLeft(), bot, body.getBottomLeft(), false));
    g.fillRoundedRectangle (body, 5.0f);
    if (on)
    {
        g.setColour (Col::amber.withAlpha (0.22f));
        g.fillRoundedRectangle (body, 5.0f);
        g.setColour (Col::amber);
        g.drawRoundedRectangle (body.reduced (0.5f), 5.0f, 1.5f);
    }
    else
    {
        g.setColour (juce::Colours::white.withAlpha (0.1f));
        g.drawHorizontalLine ((int) body.getY() + 1, body.getX() + 5.0f, body.getRight() - 5.0f);
        g.setColour (juce::Colours::black.withAlpha (0.8f));
        g.drawRoundedRectangle (body, 5.0f, 1.0f);
    }

    const float nameH = juce::jmin (16.0f, body.getWidth() / juce::jmax (3.0f, (float) pc.name.size() * 0.55f));
    auto txt = body.reduced (2.0f, 4.0f);
    g.setFont (Fonts::label (nameH));
    g.setColour (on ? juce::Colour (0xffffe3b8) : Col::engrave);
    g.drawFittedText (pc.name, txt.removeFromTop (txt.getHeight() * 0.62f).toNearestInt(), juce::Justification::centredBottom, 1, 0.7f);
    g.setFont (Fonts::mono (10.5f));
    g.setColour (on ? Col::amber : Col::dim);
    g.drawText (pc.roman, txt, juce::Justification::centredTop, false);
}

void ChordPad::mouseDown (const juce::MouseEvent&) { down = true; proc.padPressed (slot, true); repaint(); }
void ChordPad::mouseUp (const juce::MouseEvent&)   { down = false; proc.padPressed (slot, false); repaint(); }

// =============================================================================
// DragTile
// =============================================================================
DragTile::DragTile (HooksmithProcessor& p, int l, juce::String label, juce::Colour c)
    : proc (p), lane (l), text (std::move (label)), colour (c)
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    setTooltip ("Drag into Ableton (or any DAW) to drop this as a MIDI clip.  Click to save it to Music/HOOKSMITH.");
}

void DragTile::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.5f);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRoundedRectangle (r.translated (0, 2), 5.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3d44), r.getTopLeft(), juce::Colour (0xff202227), r.getBottomLeft(), false));
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (colour.withAlpha (pressed ? 0.9f : 0.55f));
    g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, pressed ? 1.8f : 1.0f);

    // little MIDI clip icon
    auto icon = r.reduced (8.0f).removeFromTop (r.getHeight() * 0.42f);
    icon = icon.withSizeKeepingCentre (juce::jmin (icon.getWidth(), 44.0f), icon.getHeight());
    g.setColour (colour.withAlpha (0.18f));
    g.fillRoundedRectangle (icon, 2.0f);
    g.setColour (colour);
    const float bw = icon.getWidth() / 8.0f;
    const int heights[] = { 2, 4, 3, 5, 1, 4, 2, 3 };
    for (int i = 0; i < 8; ++i)
        g.fillRect (icon.getX() + i * bw + 1.0f, icon.getBottom() - icon.getHeight() * heights[i] / 6.0f, bw * 0.75f, 2.0f);

    g.setFont (Fonts::label (13.0f));
    g.setColour (Col::engrave);
    g.drawText (text, r.withTrimmedTop (r.getHeight() * 0.48f), juce::Justification::centred, false);
}

void DragTile::mouseDown (const juce::MouseEvent&) { dragging = false; pressed = true; repaint(); }

void DragTile::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 6) return;
    dragging = true;
    const auto file = proc.writeMidiFile (lane);
    if (file.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

void DragTile::mouseUp (const juce::MouseEvent&)
{
    pressed = false;
    repaint();
    if (dragging) return;
    const auto file = proc.writeMidiFile (lane);
    if (file.existsAsFile())
    {
        file.revealToUser();
        if (onStatus) onStatus ("SAVED " + file.getFileName().toUpperCase());
    }
}

// =============================================================================
// TapeButton
// =============================================================================
void TapeButton::set (bool on)
{
    if (auto* prm = proc.apvts.getParameter ("tapeStop"))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (on ? 1.0f : 0.0f);
        prm->endChangeGesture();
    }
}

void TapeButton::mouseDown (const juce::MouseEvent&)
{
    const bool now = proc.apvts.getRawParameterValue ("tapeStop")->load() > 0.5f;
    set (latch ? ! now : true);
}

void TapeButton::mouseUp (const juce::MouseEvent&)
{
    if (! latch) set (false);
}

// =============================================================================
// LaneStrip
// =============================================================================
LaneStrip::LaneStrip (HooksmithProcessor& p, int l)
    : drag (p, l, "DRAG MIDI", laneColour (l)), proc (p), lane (l),
      onButton (juce::String (hs::laneNames()[(size_t) l]), laneColour (l))
{
    onButton.setClickingTogglesState (true);
    onButton.setTooltip ("Turn this part on or off");
    loadButton.setTooltip ("Load your own instrument (VST3 / AU) for this part - Omnisphere, Serum, FLIPSIDE...");
    showButton.setTooltip ("Open the instrument's window");
    ejectButton.setTooltip ("Remove the instrument (goes back to the built-in sound)");
    newButton.setTooltip ("Write a new version of just this part (works even when it's locked)");
    newButton.getProperties().set ("accent", true);
    lockButton.setTooltip ("Lock: the big GENERATE at the top won't change this part");
    arpButton.setTooltip ("Arpeggiate this part. Set its rate and mode in the ARPEGGIATOR panel");
    arpButton.setClickingTogglesState (true);
    lockButton.setClickingTogglesState (true);
    sound.setTooltip ("Where the notes go: your plugin (or the built-in sound if none is loaded), always built-in, or MIDI out only");
    octave.setTooltip ("Move the part up or down an octave");
    vol.setTooltip ("Volume of this part");
    len.setTooltip ("Note length, like a gate: left = short and choppy, middle = as written, right = long and legato");
    if (HooksmithProcessor::laneHasSlots (lane))
        for (int k = 0; k < HooksmithProcessor::kSlots; ++k)
        {
            auto* b = slotButtons.add (new ui::LedButton (juce::String (k + 1), laneColour (lane), false));
            b->getProperties().set ("slot", true);
            b->setTooltip ("Memory " + juce::String (k + 1) + ": click an empty slot to keep this "
                           + juce::String (hs::laneNames()[(size_t) lane]).toLowerCase()
                           + ", click a saved one to bring it back. Right-click to replace or clear.");
            b->onClick = [this, k] { slotClicked (k, juce::ModifierKeys::currentModifiers.isPopupMenu()); };
            addAndMakeVisible (b);
        }

    loadButton.onClick  = [this] { chooseInstrument(); };
    showButton.onClick  = [this] { showWindow (window == nullptr); };
    ejectButton.onClick = [this] { showWindow (false); proc.unloadInstrument (lane); };
    newButton.onClick   = [this]
    {
        proc.rerollLane (lane);
        const int t = proc.getLaneTake (lane);
        if (onStatus) onStatus (juce::String (hs::laneNames()[(size_t) lane]).toUpperCase()
                                + (t == 0 ? ": NEW IDEA" : ": TAKE " + juce::String (t + 1) + " OF 3 (CLOSE)"));
    };
    lockButton.onClick  = [this] { proc.setLaneLocked (lane, lockButton.getToggleState()); };
    drag.onStatus = [this] (const juce::String& s) { if (onStatus) onStatus (s); };

    sound.addItemList (HooksmithProcessor::soundNames(), 1);
    octave.addItemList ({ "OCT -2", "OCT -1", "OCT 0", "OCT +1", "OCT +2" }, 1);

    for (juce::Component* c : { (juce::Component*) &onButton, (juce::Component*) &loadButton, (juce::Component*) &showButton,
                                (juce::Component*) &ejectButton, (juce::Component*) &newButton, (juce::Component*) &lockButton,
                                (juce::Component*) &arpButton,
                                (juce::Component*) &sound, (juce::Component*) &octave, (juce::Component*) &vol, (juce::Component*) &len, (juce::Component*) &drag })
        addAndMakeVisible (c);

    onA   = std::make_unique<BA> (proc.apvts, HooksmithProcessor::laneParam (lane, "on"), onButton);
    soundA = std::make_unique<CA> (proc.apvts, HooksmithProcessor::laneParam (lane, "sound"), sound);
    octA  = std::make_unique<CA> (proc.apvts, HooksmithProcessor::laneParam (lane, "oct"), octave);
    volA  = std::make_unique<SA> (proc.apvts, HooksmithProcessor::laneParam (lane, "vol"), vol);
    lenA  = std::make_unique<SA> (proc.apvts, HooksmithProcessor::laneParam (lane, "len"), len);
    len.setDoubleClickReturnValue (true, 1.0);
    if (lane == hs::DrumsLane) { octave.setEnabled (false); arpButton.setVisible (false); }
    else
    {
        arpA = std::make_unique<BA> (proc.apvts, HooksmithProcessor::laneParam (lane, "arpOn"), arpButton);
        // Switch the ARPEGGIATOR panel later, never inside the click: the panel rebuilds its
        // parameter links, and doing that mid-click freed a link still in use (Ableton crash).
        arpButton.onClick = [this]
        {
            juce::Component::SafePointer<LaneStrip> safe (this);
            juce::MessageManager::callAsync ([safe] { if (safe != nullptr && safe->onArpPressed) safe->onArpPressed(); });
        };
    }
    refresh();
}

LaneStrip::~LaneStrip() { window.reset(); }

void LaneStrip::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    auto top = r.removeFromTop (28);
    onButton.setBounds (top.removeFromLeft (112));
    top.removeFromLeft (8);
    newButton.setBounds (top);
    r.removeFromTop (6);
    lcdArea = r.removeFromTop (26);
    if (! slotButtons.isEmpty())
    {
        auto sr = lcdArea.removeFromRight (4 * 23 + 3 * 3);
        lcdArea.removeFromRight (5);
        for (auto* b : slotButtons) { b->setBounds (sr.removeFromLeft (23)); sr.removeFromLeft (3); }
    }
    r.removeFromTop (6);
    auto row = r.removeFromTop (28);
    loadButton.setBounds (row.removeFromLeft (row.getWidth() - 110));
    row.removeFromLeft (6);
    showButton.setBounds (row.removeFromLeft (66));
    row.removeFromLeft (6);
    ejectButton.setBounds (row);
    r.removeFromTop (8);
    sound.setBounds (r.removeFromTop (26));
    r.removeFromTop (8);
    auto bottom = r;
    auto left = bottom.removeFromLeft (94);
    octave.setBounds (left.removeFromTop (26));
    left.removeFromTop (8);
    lockButton.setBounds (left.removeFromLeft (48).withHeight (28));
    left.removeFromLeft (4);
    arpButton.setBounds (left.withHeight (28));
    drag.setBounds (bottom.removeFromRight (64));
    bottom.removeFromLeft (2);
    const int kw = bottom.getWidth() / 2;
    len.setBounds (bottom.removeFromLeft (kw).withSizeKeepingCentre (42, 42).translated (0, -6));
    vol.setBounds (bottom.withSizeKeepingCentre (42, 42).translated (0, -6));
}

void LaneStrip::paint (juce::Graphics& g)
{
    // instrument LCD
    lcdBox (g, lcdArea.toFloat());
    auto* inst = proc.getInstrument (lane);
    juce::String text = status.isNotEmpty() ? status : lcdText();
    auto lcdText = lcdArea.reduced (8, 0);
    if (proc.apvts.getRawParameterValue (HooksmithProcessor::laneParam (lane, "colOn"))->load() > 0.5f)
    {
        g.setFont (Fonts::mono (11.0f));
        g.setColour (Col::amber);
        g.drawText ("COLOR", lcdText.removeFromRight (44), juce::Justification::centredRight);
    }
    g.setFont (Fonts::mono (14.0f));
    g.setColour ((inst != nullptr ? laneColour (lane) : Col::dim.brighter (0.2f)));
    g.drawFittedText (text, lcdText, juce::Justification::centredLeft, 1, 0.8f);

    engraved (g, "LENGTH", len.getBounds().toFloat().withY ((float) len.getBottom() - 1.0f).withHeight (14.0f).expanded (12.0f, 0.0f),
              Fonts::label (11.0f), juce::Justification::centred, Col::dim.brighter (0.3f));
    engraved (g, "VOL", vol.getBounds().toFloat().withY ((float) vol.getBottom() - 1.0f).withHeight (14.0f).expanded (10.0f, 0.0f),
              Fonts::label (11.5f), juce::Justification::centred, Col::dim.brighter (0.3f));
}

juce::String LaneStrip::lcdText() const
{
    if (auto* inst = proc.getInstrument (lane)) return inst->getName().toUpperCase();
    if (lane == hs::DrumsLane) return "BUILT-IN KIT";
    const bool autoPick = proc.apvts.getRawParameterValue (HooksmithProcessor::laneParam (lane, "patch"))->load() < 0.5f;
    return (autoPick ? "AUTO: " : "") + juce::String (hs::SketchSynth::patchName (proc.builtInPatch (lane)));
}

// Click the display to pick a built-in sound
void LaneStrip::mouseDown (const juce::MouseEvent& e)
{
    if (! lcdArea.contains (e.getPosition()) || lane == hs::DrumsLane) return;
    auto* prm = proc.apvts.getParameter (HooksmithProcessor::laneParam (lane, "patch"));
    if (prm == nullptr) return;
    juce::PopupMenu m;
    const int cur = (int) proc.apvts.getRawParameterValue (HooksmithProcessor::laneParam (lane, "patch"))->load();
    m.addSectionHeader ("BUILT-IN SOUND");
    m.addItem (1, "Auto (sound of the genre)", true, cur == 0);
    const auto& list = HooksmithProcessor::patchList (lane);
    for (int i = 0; i < (int) list.size(); ++i)
        m.addItem (i + 2, juce::String (hs::SketchSynth::patchName (list[(size_t) i])), true, cur == i + 1);
    juce::Component::SafePointer<LaneStrip> safe (this);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withTargetScreenArea (localAreaToGlobal (lcdArea)),
                     [safe, prm] (int r)
                     {
                         if (safe == nullptr || r <= 0) return;
                         prm->beginChangeGesture();
                         prm->setValueNotifyingHost (prm->convertTo0to1 ((float) (r - 1)));
                         prm->endChangeGesture();
                         safe->repaint();
                     });
}

void LaneStrip::slotClicked (int slot, bool menu)
{
    const auto part = juce::String (hs::laneNames()[(size_t) lane]).toUpperCase();
    auto say = [this] (const juce::String& t) { if (onStatus) onStatus (t); };
    if (menu && proc.slotUsed (lane, slot))
    {
        juce::PopupMenu m;
        m.addItem (1, "Replace with what's playing");
        m.addItem (2, "Clear memory " + juce::String (slot + 1));
        juce::Component::SafePointer<LaneStrip> safe (this);
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (slotButtons[slot]),
                         [safe, slot, part] (int r)
                         {
                             if (safe == nullptr || r <= 0) return;
                             if (r == 1) { safe->proc.storeSlot (safe->lane, slot); if (safe->onStatus) safe->onStatus (part + ": SAVED TO MEMORY " + juce::String (slot + 1)); }
                             if (r == 2) { safe->proc.clearSlot (safe->lane, slot); if (safe->onStatus) safe->onStatus (part + ": MEMORY " + juce::String (slot + 1) + " CLEARED"); }
                             safe->refreshSlots();
                         });
        refreshSlots();
        return;
    }
    if (proc.slotUsed (lane, slot))
    {
        proc.recallSlot (lane, slot);
        say (part + ": MEMORY " + juce::String (slot + 1));
    }
    else
    {
        proc.storeSlot (lane, slot);
        say (part + ": SAVED TO MEMORY " + juce::String (slot + 1));
    }
    refreshSlots();
}

void LaneStrip::refreshSlots()
{
    const int active = proc.activeSlot (lane);
    for (int k = 0; k < slotButtons.size(); ++k)
    {
        auto* b = slotButtons[k];
        const bool used = proc.slotUsed (lane, k);
        const bool isActive = k == active;
        if (b->getToggleState() != used || (bool) b->getProperties()["slotActive"] != isActive)
        {
            b->setToggleState (used, juce::dontSendNotification);
            b->getProperties().set ("slotActive", isActive);
            b->repaint();
        }
    }
}

void LaneStrip::setArpBlink (bool lit)
{
    refreshSlots();
    if (const auto t = lcdText(); t != shownLcd) { shownLcd = t; repaint(); }
    const auto c = lit ? juce::Colour (0xffffe0b0) : laneColour (lane);
    if ((juce::int64) c.getARGB() != (juce::int64) arpButton.getProperties()["ledColour"])
    {
        arpButton.getProperties().set ("ledColour", (juce::int64) c.getARGB());
        arpButton.repaint();
    }
}

void LaneStrip::refresh()
{
    const bool has = proc.getInstrument (lane) != nullptr;
    showButton.setEnabled (has);
    ejectButton.setEnabled (has);
    lockButton.setToggleState (proc.isLaneLocked (lane), juce::dontSendNotification);
    if (window != nullptr && proc.getInstrument (lane) != shown) showWindow (false);
    repaint();
}

void LaneStrip::chooseInstrument()
{
   #if JUCE_MAC
    const juce::File start ("/Library/Audio/Plug-Ins");
   #elif JUCE_WINDOWS
    const juce::File start ("C:\\Program Files\\Common Files\\VST3");
   #else
    const auto start = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile (".vst3");
   #endif
    chooser = std::make_unique<juce::FileChooser> ("Pick an instrument for " + juce::String (hs::laneNames()[(size_t) lane]) + " (.vst3 or .component)",
                                                   start, "*.vst3;*.component");
    const auto browserFlags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                            | juce::FileBrowserComponent::canSelectDirectories;
    chooser->launchAsync (browserFlags, [this] (const juce::FileChooser& fc)
    {
        const auto file = fc.getResult();
        if (file == juce::File()) return;
        showWindow (false);
        status = "LOADING...";
        repaint();
        juce::String error;
        if (proc.loadInstrumentFromFile (lane, file, error)) { status = {}; showWindow (true); }
        else status = error.toUpperCase();
        refresh();
    });
}

void LaneStrip::showWindow (bool show)
{
    if (! show || proc.getInstrument (lane) == nullptr)
    {
        window.reset();
        shown = nullptr;
        showButton.setToggleState (false, juce::dontSendNotification);
        return;
    }
    if (window == nullptr)
    {
        shown = proc.getInstrument (lane);
        window = std::make_unique<HostedWindow> (*shown, [this]
        {
            juce::Component::SafePointer<LaneStrip> safe (this);
            juce::MessageManager::callAsync ([safe]
            { if (safe != nullptr) safe->showWindow (false); });
        });
    }
    window->toFront (true);
    showButton.setToggleState (true, juce::dontSendNotification);
}

// =============================================================================
// Editor
// =============================================================================
HooksmithPanel::HooksmithPanel (HooksmithProcessor& p)
    : proc (p), screen (p), tapeButton (p),
      dragAll (p, -1, "DRAG ALL", Col::amber), dragSong (p, -2, "DRAG SONG", Col::amber)
{
    setLookAndFeel (&look);
    tooltips.setLookAndFeel (&look);

    // Header
    mode.addItemList (HooksmithProcessor::playModeNames(), 1);
    mode.setTooltip ("SYNC: plays with your DAW.  HOLD A KEY: plays while you hold a note (transposes).  CHORD KEYS: every key plays a chord from the bank");
    playButton.setClickingTogglesState (true);
    playButton.setTooltip ("Play without pressing play in the DAW (uses the TEMPO knob if the DAW gives no tempo)");
    generateButton.getProperties().set ("accent", true);
    generateButton.setTooltip ("Write a brand new idea for every part that isn't locked. Each part also has its own GENERATE");
    generateButton.onClick = [this]
    {
        proc.generateAll();
        int lane = 0;
        while (lane < hs::kNumLanes - 1 && proc.isLaneLocked (lane)) ++lane;
        const int t = proc.getLaneTake (lane);
        setStatus (t == 0 ? "NEW IDEA - NEXT 2 GENERATES ARE CLOSE TAKES" : "TAKE " + juce::String (t + 1) + " OF 3 - CLOSE TO THE IDEA");
    };
    for (juce::Component* c : { (juce::Component*) &mode, (juce::Component*) &playButton, (juce::Component*) &generateButton })
        addAndMakeVisible (c);
    modeA = std::make_unique<CA> (proc.apvts, "mode", mode);
    playA = std::make_unique<BA> (proc.apvts, "play", playButton);

    // Song
    addItems (genre, hs::genreNames());
    addItems (key, hs::keyNames());
    addItems (scale, hs::scaleNames());
    genre.setTooltip ("Genre: sets the chords, rhythms, melody style, bass and drums");
    key.setTooltip ("Key");
    scale.setTooltip ("Scale. AUTO lets the genre choose major or minor");
    for (auto* c : { &genre, &key, &scale }) addAndMakeVisible (*c);
    genreA = std::make_unique<CA> (proc.apvts, "genre", genre);
    keyA   = std::make_unique<CA> (proc.apvts, "key", key);
    scaleA = std::make_unique<CA> (proc.apvts, "scale", scale);

    for (int i = 0; i < 3; ++i)
    {
        auto* b = barButtons.add (new LedButton (juce::String (4 << i)));
        b->setTooltip (juce::String (4 << i) + " bar loop");
        b->onClick = [this, i] { setChoice ("bars", i); };
        addAndMakeVisible (b);
    }
    const char* secShort[] = { "Intro", "Verse", "Pre", "Hook", "Bridge", "Outro" };
    for (int i = 0; i < hs::kNumSections; ++i)
    {
        auto* b = sectionButtons.add (new LedButton (secShort[i]));
        b->setTooltip ("Write the " + juce::String (hs::sectionNames()[(size_t) i]) + ".  Intro and Outro use the Hook's chords, so the song fits together");
        b->onClick = [this, i] { setChoice ("section", i); };
        addAndMakeVisible (b);
    }

    // Screen + PROGRESSOR
    addAndMakeVisible (screen);
    progression.setTooltip ("Pick a named progression, or AUTO to let the PROGRESSOR choose one for each section");
    progression.onChange = [this]
    {
        const int idx = juce::jmax (0, progression.getSelectedItemIndex());
        const int named = (int) hs::progressionNames ((int) proc.apvts.getRawParameterValue ("genre")->load()).size();
        if (idx > named) { proc.selectImport (idx - named - 1); return; }
        if (proc.getActiveImport() >= 0) proc.selectImport (-1);
        if (auto* prm = proc.apvts.getParameter ("prog"))
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (prm->convertTo0to1 ((float) idx));
            prm->endChangeGesture();
        }
    };
    addAndMakeVisible (progression);
    importButton.setTooltip ("Import chord MIDI files (Cthulhu chord packs, sample packs, your own ideas). HOOKSMITH finds the key and chords, "
                             "plays them in your key and genre, and puts them on the chord pads. You can also drag .mid files onto HOOKSMITH");
    importButton.onClick = [this]
    {
        importChooser = std::make_unique<juce::FileChooser> ("Import chord MIDI files",
                                                             juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Downloads"),
                                                             "*.mid;*.midi");
        importChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                        | juce::FileBrowserComponent::canSelectMultipleItems,
                                    [this] (const juce::FileChooser& fc) { importFiles (fc.getResults()); });
    };
    addAndMakeVisible (importButton);
    captureButton.setTooltip ("CAPTURE: load Cthulhu (or any chord plugin) with a chord preset on the CHORDS part, then press this. "
                              "HOOKSMITH records the chord on every key, puts them on the pads, and turns that pack's trigger MIDI files into real progressions");
    captureButton.onClick = [this]
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Capture chords from the CHORDS plugin (" + (proc.getInstrumentName (hs::ChordsLane).isEmpty() ? juce::String ("nothing loaded")
                                                                                                                         : proc.getInstrumentName (hs::ChordsLane)) + ")");
        menu.addItem (2, "Clear captured chords", proc.hasCapture());
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&captureButton), [this] (int r)
        {
            if (r == 1)
            {
                setStatus ("CAPTURING...");
                juce::Component::SafePointer<HooksmithPanel> safe (this);
                juce::MessageManager::callAsync ([safe]
                {
                    if (safe != nullptr) safe->setStatus (safe->proc.captureFromChordsPlugin());
                });
            }
            else if (r == 2) { proc.clearCapture(); setStatus ("CAPTURED CHORDS CLEARED"); }
        });
    };
    addAndMakeVisible (captureButton);

    static const bool black[] = { false, true, false, true, false, false, true, false, true, false, true, false };
    for (int s = 0; s < 12; ++s)
    {
        auto* pad = pads.add (new ChordPad (proc, s, black[s]));
        pad->setTooltip ("Click to hear this chord.  In CHORD KEYS mode, any " + juce::String (hs::noteName (s, black[s])) + " key plays it");
        addAndMakeVisible (pad);
    }
    // black pads on top of white ones
    for (int s : { 1, 3, 6, 8, 10 }) pads[s]->toFront (false);

    complexity.setTooltip ("COMPLEXITY: richer chords (7ths, 9ths), more variation");
    density.setTooltip ("DENSITY: how many notes and hits");
    spice.setTooltip ("SPICE: passing chords, early chord changes, sus that resolves, wide voicings with a singing top note");
    for (auto* k : { &complexity, &density, &spice }) addAndMakeVisible (*k);
    complexityA = std::make_unique<SA> (proc.apvts, "complexity", complexity);
    densityA    = std::make_unique<SA> (proc.apvts, "density", density);
    spiceA      = std::make_unique<SA> (proc.apvts, "spice", spice);
    spice.setDoubleClickReturnValue (true, 0.4);

    // GROOVE module
    addItems (feel, hs::feelNames());
    feel.setTooltip ("FEEL: how it sits in time. Laid Back and Dilla sit behind the beat, Pushed rushes, Shuffle is triplet swing, "
                     "Bounce is trap, Afro is 3-3-2, Live Drummer drifts, Rubato breathes. Genre Default picks one for you");
    feelAmt.setTooltip ("AMOUNT: how strongly the feel is applied");
    swing.setTooltip ("SWING: middle = the genre's own swing, left = straight, right = extra swing");
    pocket.setTooltip ("POCKET: left = snare and melody push ahead of the beat, right = they lay back behind it. The kick stays put");
    pocket.setBipolar (true);
    dynamics.setTooltip ("DYNAMICS: accents and phrasing. Strong beats and high notes louder, ghost notes softer");
    strum.setTooltip ("STRUM: plays the notes of each chord slightly apart, like a hand on keys or a guitar strum");
    humanize.setTooltip ("HUMANIZE: tiny random timing and velocity changes");
    addAndMakeVisible (feel);
    for (auto* k : { &feelAmt, &swing, &pocket, &dynamics, &strum, &humanize }) addAndMakeVisible (*k);
    feelA     = std::make_unique<CA> (proc.apvts, "feel", feel);
    feelAmtA  = std::make_unique<SA> (proc.apvts, "feelAmt", feelAmt);
    swingA    = std::make_unique<SA> (proc.apvts, "groove", swing);
    pocketA   = std::make_unique<SA> (proc.apvts, "pocket", pocket);
    dynamicsA = std::make_unique<SA> (proc.apvts, "dynamics", dynamics);
    strumA    = std::make_unique<SA> (proc.apvts, "strum", strum);
    humanizeA = std::make_unique<SA> (proc.apvts, "humanize", humanize);

    for (int l = 0; l < hs::kNumLanes; ++l)
    {
        lanes[l] = std::make_unique<LaneStrip> (proc, l);
        lanes[l]->onStatus = [this] (const juce::String& s) { setStatus (s); };
        lanes[l]->onArpPressed = [this, l] { selectArpLane (l); };
        addAndMakeVisible (*lanes[l]);
    }

    // Arp
    arpOnButton.setClickingTogglesState (true);
    arpOnButton.setTooltip ("Turn this part's arpeggiator on or off");
    for (int l = 0; l < HooksmithProcessor::kArpLanes; ++l)
    {
        auto* t = arpTabs.add (new LedButton (juce::String (hs::laneNames()[(size_t) l]), laneColour (l)));
        t->setTooltip ("Show the " + juce::String (hs::laneNames()[(size_t) l]).toLowerCase() + " arpeggiator");
        t->onClick = [this, l]
        {
            juce::Component::SafePointer<HooksmithPanel> safe (this);
            juce::MessageManager::callAsync ([safe, l] { if (safe != nullptr) safe->selectArpLane (l); });
        };
        addAndMakeVisible (t);
    }
    arpRate.addItemList (HooksmithProcessor::rateNames(), 1);
    arpMode.addItemList (hs::Arpeggiator::modeNames(), 1);
    arpOct.addItemList ({ "1 OCT", "2 OCT", "3 OCT", "4 OCT" }, 1);
    arpRate.setTooltip ("Note speed: T = triplet, D = dotted");
    arpGate.setTooltip ("Note length");
    for (juce::Component* c : { (juce::Component*) &arpOnButton, (juce::Component*) &arpRate, (juce::Component*) &arpMode,
                                (juce::Component*) &arpOct, (juce::Component*) &arpGate })
        addAndMakeVisible (c);
    selectArpLane (0);

    // Export / output
    dragAll.setTooltip ("Drag all parts into your DAW (one track per part).  Click to save to Music/HOOKSMITH");
    dragSong.setTooltip ("Drag a FULL SONG: Intro, Verse, Pre, Hook, Verse, Pre, Hook, Bridge, Hook, Outro (64 bars)");
    dragAll.onStatus = dragSong.onStatus = [this] (const juce::String& s) { setStatus (s); };
    midiOutButton.setClickingTogglesState (true);
    midiOutButton.setTooltip ("Send the notes out as MIDI too (chords ch1, melody ch2, bass ch3, drums ch10)");
    sketchVol.setTooltip ("Volume of the built-in sounds");
    bpm.setTooltip ("Tempo used when the DAW doesn't give one (standalone)");
    for (juce::Component* c : { (juce::Component*) &dragAll, (juce::Component*) &dragSong, (juce::Component*) &midiOutButton,
                                (juce::Component*) &sketchVol, (juce::Component*) &bpm })
        addAndMakeVisible (c);
    midiOutA   = std::make_unique<BA> (proc.apvts, "midiOut", midiOutButton);
    sketchVolA = std::make_unique<SA> (proc.apvts, "sketchVol", sketchVol);
    bpmA       = std::make_unique<SA> (proc.apvts, "bpm", bpm);
    bpm.setNumDecimalPlacesToDisplay (0);

    // COLOR
    for (int l = 0; l < hs::kNumLanes; ++l)
    {
        auto* t = colorTabs.add (new LedButton (juce::String (hs::laneNames()[(size_t) l]), laneColour (l)));
        t->setTooltip ("Show the " + juce::String (hs::laneNames()[(size_t) l]).toLowerCase() + " COLOR settings");
        t->onClick = [this, l] { selectColorLane (l); };
        addAndMakeVisible (t);
    }
    colorOnButton.setClickingTogglesState (true);
    colorOnButton.setTooltip ("Turn the retro colour on for this part");
    drive.setTooltip ("DRIVE: warm tape saturation");
    wobble.setTooltip ("WOBBLE: wow and flutter, the pitch drift of old tape and vinyl");
    noise.setTooltip ("NOISE: tape hiss and vinyl crackle");
    crush.setTooltip ("CRUSH: lower bit depth and sample rate, like an old sampler");
    tone.setTooltip ("TONE: rolls off the highs, like worn tape");
    colMix.setTooltip ("MIX: blend between the clean and the coloured sound");
    machine.setTooltip ("MACHINE: the sound and swing of a classic sampler - Off, MPC60, MPC2000, MPC3000 or SP-1200");
    flavor.setTooltip ("FLAVOR: how much of the machine you hear and feel (its filters, EQ, saturation and swing)");
    exag.setTooltip ("EXAGGERATE: push the machine past its real specs - lower rate, fewer bits, more swing, narrower filters");
    funk.setTooltip ("FUNK: random push and pull, accents and ghost notes, and a filter that wanders");
    machine.setDoubleClickReturnValue (true, 0.0);
    flavor.setDoubleClickReturnValue (true, 0.6);
    exag.setDoubleClickReturnValue (true, 0.0);
    funk.setDoubleClickReturnValue (true, 0.0);
    addAndMakeVisible (colorOnButton);
    for (auto* k : { &drive, &wobble, &noise, &crush, &tone, &colMix, &machine, &flavor, &exag, &funk }) addAndMakeVisible (*k);
    selectColorLane (0);

    // PART TONE
    for (int l = 0; l < hs::kNumLanes; ++l)
    {
        auto* t = partTabs.add (new LedButton (juce::String (hs::laneNames()[(size_t) l]), laneColour (l)));
        t->setTooltip ("Show the " + juce::String (hs::laneNames()[(size_t) l]).toLowerCase() + " PART TONE settings");
        t->onClick = [this, l] { selectPartLane (l); };
        addAndMakeVisible (t);
    }
    pStrum.setTooltip ("STRUM: notes played together roll like a guitar (down on the beat, up off it). Single notes lay back behind the beat");
    pHp.setTooltip ("HIGH PASS: cuts the lows of this part (off at the left, up to 1.5 kHz)");
    pLp.setTooltip ("LOW PASS: cuts the highs of this part (off at the right, down to 300 Hz)");
    pGain.setTooltip ("GAIN: middle = as is. Left = quieter. Right = pushes the part into a console bus compressor (4:1, auto release, a little saturation): louder, punchier, glued");
    pGain.setBipolar (true);
    pGain.setDoubleClickReturnValue (true, 0.5);
    pStrum.setDoubleClickReturnValue (true, 0.0);
    pHp.setDoubleClickReturnValue (true, 0.0);
    pLp.setDoubleClickReturnValue (true, 1.0);
    for (auto* k : { &pStrum, &pHp, &pLp, &pGain }) addAndMakeVisible (*k);
    addItems (melHarm, hs::harmonyNames());
    addItems (melFlavor, hs::melodyFlavorNames());
    melHarm.setTooltip ("MELODY HARMONY: a second voice that follows the melody in the key - 3rds, 6ths, octaves, 5ths, stacks, or the chord tones");
    melFlavor.setTooltip ("MELODY FLAVOR: how the hooks are written - Catchy, Soulful (runs), Rap Flow, Dreamy, Bluesy (blue-note slides), Pentatonic, Latin (pushes), Jazz (chromatic approaches)");
    addAndMakeVisible (melHarm); addAndMakeVisible (melFlavor);
    melHarmA = std::make_unique<CA> (proc.apvts, "melHarm", melHarm);
    melFlavorA = std::make_unique<CA> (proc.apvts, "melFlavor", melFlavor);
    selectPartLane (hs::BassLane);

    // TAPE STOP
    tapeButton.getProperties().set ("accent", true);
    tapeButton.setTooltip ("Hold to slow the whole beat down to a stop, let go to spin it back up. With LATCH on, click once to stop and again to start");
    latchButton.setClickingTogglesState (true);
    latchButton.setTooltip ("LATCH: the TAPE STOP button stays down until you click it again");
    latchButton.onClick = [this] { tapeButton.latch = latchButton.getToggleState(); };
    loopStopButton.setClickingTogglesState (true);
    loopStopButton.setTooltip ("LOOP END: automatic tape stop at the end of every loop, back in on the one");
    tapeTime.setTooltip ("STOP: how long it takes to slow down");
    tapeStart.setTooltip ("START: how long it takes to spin back up");
    for (juce::Component* c : { (juce::Component*) &tapeButton, (juce::Component*) &latchButton, (juce::Component*) &loopStopButton,
                                (juce::Component*) &tapeTime, (juce::Component*) &tapeStart })
        addAndMakeVisible (c);
    loopStopA  = std::make_unique<BA> (proc.apvts, "tapeLoop", loopStopButton);
    tapeTimeA  = std::make_unique<SA> (proc.apvts, "tapeTime", tapeTime);
    tapeStartA = std::make_unique<SA> (proc.apvts, "tapeStart", tapeStart);

    proc.addChangeListener (this);
    setSize (kW, kH);
    refreshProgressions();
    startTimerHz (30);
}

HooksmithPanel::~HooksmithPanel()
{
    proc.removeChangeListener (this);
    for (auto& l : lanes) l.reset();
    tooltips.setLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void HooksmithPanel::selectArpLane (int lane)
{
    lane = juce::jlimit (0, HooksmithProcessor::kArpLanes - 1, lane);
    if (lane == arpLane && arpOnA != nullptr) return;   // already showing this part
    arpLane = lane;
    arpOnA.reset(); arpRateA.reset(); arpModeA.reset(); arpOctA.reset(); arpGateA.reset();
    arpOnA   = std::make_unique<BA> (proc.apvts, HooksmithProcessor::laneParam (arpLane, "arpOn"), arpOnButton);
    arpRateA = std::make_unique<CA> (proc.apvts, HooksmithProcessor::laneParam (arpLane, "arpRate"), arpRate);
    arpModeA = std::make_unique<CA> (proc.apvts, HooksmithProcessor::laneParam (arpLane, "arpMode"), arpMode);
    arpOctA  = std::make_unique<CA> (proc.apvts, HooksmithProcessor::laneParam (arpLane, "arpOct"), arpOct);
    arpGateA = std::make_unique<SA> (proc.apvts, HooksmithProcessor::laneParam (arpLane, "arpGate"), arpGate);
    for (int i = 0; i < arpTabs.size(); ++i) arpTabs[i]->setToggleState (i == arpLane, juce::dontSendNotification);
    arpOnButton.getProperties().set ("ledColour", (juce::int64) laneColour (arpLane).getARGB());
    arpOnButton.repaint();
}

void HooksmithPanel::selectColorLane (int lane)
{
    colorLane = juce::jlimit (0, (int) hs::kNumLanes - 1, lane);
    colorOnA.reset(); driveA.reset(); wobbleA.reset(); noiseA.reset(); crushA.reset(); toneA.reset(); colMixA.reset();
    machineA.reset(); flavorA.reset(); exagA.reset(); funkA.reset();
    auto id = [this] (const char* what) { return HooksmithProcessor::laneParam (colorLane, what); };
    colorOnA = std::make_unique<BA> (proc.apvts, id ("colOn"), colorOnButton);
    driveA   = std::make_unique<SA> (proc.apvts, id ("drive"), drive);
    wobbleA  = std::make_unique<SA> (proc.apvts, id ("wobble"), wobble);
    noiseA   = std::make_unique<SA> (proc.apvts, id ("noise"), noise);
    crushA   = std::make_unique<SA> (proc.apvts, id ("crush"), crush);
    toneA    = std::make_unique<SA> (proc.apvts, id ("tone"), tone);
    colMixA  = std::make_unique<SA> (proc.apvts, id ("colMix"), colMix);
    machineA = std::make_unique<SA> (proc.apvts, id ("machine"), machine);
    flavorA  = std::make_unique<SA> (proc.apvts, id ("flavor"), flavor);
    exagA    = std::make_unique<SA> (proc.apvts, id ("exag"), exag);
    funkA    = std::make_unique<SA> (proc.apvts, id ("funk"), funk);
    shownMachine = -1;
    for (int i = 0; i < colorTabs.size(); ++i) colorTabs[i]->setToggleState (i == colorLane, juce::dontSendNotification);
    colorOnButton.getProperties().set ("ledColour", (juce::int64) laneColour (colorLane).getARGB());
    colorOnButton.repaint();
}

void HooksmithPanel::selectPartLane (int lane)
{
    partLane = juce::jlimit (0, hs::kNumLanes - 1, lane);
    auto id = [this] (const char* w) { return HooksmithProcessor::laneParam (partLane, w); };
    pStrumA.reset(); pHpA.reset(); pLpA.reset(); pGainA.reset();
    pStrumA = std::make_unique<SA> (proc.apvts, id ("pstrum"), pStrum);
    pHpA    = std::make_unique<SA> (proc.apvts, id ("hp"), pHp);
    pLpA    = std::make_unique<SA> (proc.apvts, id ("lp"), pLp);
    pGainA  = std::make_unique<SA> (proc.apvts, id ("gain"), pGain);
    pStrum.setEnabled (partLane != hs::DrumsLane);
    for (int i = 0; i < partTabs.size(); ++i) partTabs[i]->setToggleState (i == partLane, juce::dontSendNotification);
    repaint (partArea);
}

void HooksmithPanel::setChoice (const juce::String& id, int index)
{
    if (auto* prm = proc.apvts.getParameter (id))
    {
        prm->beginChangeGesture();
        prm->setValueNotifyingHost (prm->convertTo0to1 ((float) index));
        prm->endChangeGesture();
    }
}

void HooksmithPanel::importFiles (const juce::Array<juce::File>& files)
{
    int last = -1, count = 0;
    juce::String error;
    for (auto& f : files)
    {
        if (! f.hasFileExtension ("mid;midi")) continue;
        const int i = proc.importMidiFile (f, error);
        if (i >= 0) { last = i; ++count; }
    }
    if (last >= 0)
    {
        proc.selectImport (last);
        refreshProgressions();
        setStatus (count == 1 ? "IMPORTED " + files.getFirst().getFileNameWithoutExtension().toUpperCase()
                              : "IMPORTED " + juce::String (count) + " FILES");
    }
    else if (error.isNotEmpty()) setStatus (error.toUpperCase());
}

bool HooksmithPanel::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files) if (juce::File (f).hasFileExtension ("mid;midi")) return true;
    return false;
}

void HooksmithPanel::filesDropped (const juce::StringArray& files, int, int)
{
    juce::Array<juce::File> fs;
    for (auto& f : files) fs.add (juce::File (f));
    importFiles (fs);
}

void HooksmithPanel::refreshProgressions()
{
    const auto items = proc.progressionMenu();
    progression.clear (juce::dontSendNotification);
    progression.addItemList (items, 1);
    shownImports = proc.getNumImports();
    const int named = (int) hs::progressionNames ((int) proc.apvts.getRawParameterValue ("genre")->load()).size();
    int idx = (int) proc.apvts.getRawParameterValue ("prog")->load();
    if (proc.getActiveImport() >= 0) idx = named + 1 + proc.getActiveImport();
    if (idx >= items.size()) idx = 0;
    progression.setSelectedItemIndex (idx, juce::dontSendNotification);
    shownGenre = (int) proc.apvts.getRawParameterValue ("genre")->load();
}

void HooksmithPanel::resized()
{
    const int x0 = 36, w = kW - 72;
    headerArea = { x0, 14, w, 64 };
    songArea   = { x0, 90, w, 74 };
    screenArea = { x0, 174, 700, 280 };
    progArea   = { x0 + 712, 174, w - 712, 280 };
    grooveArea = { x0, 464, w, 96 };
    const int lw = (w - 30) / 4;
    for (int l = 0; l < hs::kNumLanes; ++l)
        laneAreas[l] = { x0 + l * (lw + 10), 570, lw, 214 };
    arpArea    = { x0, 794, 620, 92 };
    exportArea = { x0 + 632, 794, w - 632, 92 };
    partArea   = { x0, 896, w, 90 };
    colorArea  = { x0, 996, w - 372, 90 };
    tapeArea   = { x0 + w - 360, 996, 360, 90 };

    // PART TONE + MELODY
    {
        auto tabs = juce::Rectangle<int> (partArea.getX() + 216, partArea.getY() + 6, 4 * 92, 24);
        for (auto* t : partTabs) { t->setBounds (tabs.removeFromLeft (88)); tabs.removeFromLeft (4); }
        auto c = partArea.reduced (14, 0).withTrimmedTop (36).withHeight (48);
        auto left = c.removeFromLeft (576);
        const int kw = left.getWidth() / 4;
        for (auto* k : { &pStrum, &pHp, &pLp, &pGain })
            k->setBounds (left.removeFromLeft (kw).withSizeKeepingCentre (42, 42).translated (0, -4));
        c.removeFromLeft (20);
        melGroup = c;
        auto m = c.withTrimmedTop (2);
        auto colA = m.removeFromLeft (m.getWidth() / 2).reduced (8, 0);
        auto colB = m.reduced (8, 0);
        melHarm.setBounds (colA.removeFromBottom (28));
        melFlavor.setBounds (colB.removeFromBottom (28));
    }

    // COLOR
    {
        auto tabs = juce::Rectangle<int> (colorArea.getRight() - 14 - 4 * 92, colorArea.getY() + 6, 4 * 92, 24);
        for (auto* t : colorTabs) { t->setBounds (tabs.removeFromLeft (88)); tabs.removeFromLeft (4); }
        auto c = colorArea.reduced (14, 0).withTrimmedTop (36).withHeight (48);
        colorOnButton.setBounds (c.removeFromLeft (100).withSizeKeepingCentre (100, 34).translated (0, -4));
        c.removeFromLeft (6);
        auto mc = c.removeFromRight (c.getWidth() * 46 / 100);   // MACHINE group on the right
        const int cw = c.getWidth() / 6;
        for (auto* k : { &drive, &wobble, &noise, &crush, &tone, &colMix })
            k->setBounds (c.removeFromLeft (cw).withSizeKeepingCentre (42, 42).translated (0, -4));
        machineGroup = mc.toNearestInt();
        mc.removeFromLeft (8);
        const int mw = mc.getWidth() / 4;
        for (auto* k : { &machine, &flavor, &exag, &funk })
            k->setBounds (mc.removeFromLeft (mw).withSizeKeepingCentre (42, 42).translated (0, -4));
    }
    // TAPE STOP
    {
        auto t = tapeArea.reduced (14, 0).withTrimmedTop (32).withHeight (52);
        tapeButton.setBounds (t.removeFromLeft (110).withSizeKeepingCentre (110, 40));
        t.removeFromLeft (8);
        auto col = t.removeFromLeft (92);
        latchButton.setBounds (col.removeFromTop (25).reduced (0, 1));
        loopStopButton.setBounds (col.removeFromTop (25).reduced (0, 1));
        t.removeFromLeft (4);
        tapeTime.setBounds (t.removeFromLeft (t.getWidth() / 2).withSizeKeepingCentre (44, 44).translated (0, -4));
        tapeStart.setBounds (t.withSizeKeepingCentre (44, 44).translated (0, -4));
    }

    // Header
    auto h = headerArea.reduced (14, 0);
    h.removeFromLeft (290);
    mode.setBounds (h.removeFromLeft (160).withSizeKeepingCentre (160, 30));
    h.removeFromLeft (10);
    playButton.setBounds (h.removeFromLeft (92).withSizeKeepingCentre (92, 36));
    h.removeFromLeft (10);
    generateButton.setBounds (h.removeFromLeft (150).withSizeKeepingCentre (150, 40));
    lcdArea = h.removeFromRight (340).withSizeKeepingCentre (340, 46);

    // Song row
    auto s = songArea.reduced (14, 0).withTrimmedTop (30).withHeight (32);
    s.removeFromLeft (0);
    genre.setBounds (s.removeFromLeft (220));
    s.removeFromLeft (10);
    key.setBounds (s.removeFromLeft (74));
    s.removeFromLeft (10);
    scale.setBounds (s.removeFromLeft (160));
    s.removeFromLeft (22);
    for (auto* b : barButtons) { b->setBounds (s.removeFromLeft (54)); s.removeFromLeft (4); }
    s.removeFromLeft (18);
    const int sw = (s.getWidth() - 5 * 4) / 6;
    for (auto* b : sectionButtons) { b->setBounds (s.removeFromLeft (sw)); s.removeFromLeft (4); }

    // Screen
    screen.setBounds (screenArea.reduced (12).withTrimmedTop (22));

    // PROGRESSOR
    auto pr = progArea.reduced (12, 0);
    progression.setBounds (pr.getX(), progArea.getY() + 36, pr.getWidth(), 28);
    importButton.setBounds (pr.getRight() - 196, progArea.getY() + 6, 104, 24);
    captureButton.setBounds (pr.getRight() - 88, progArea.getY() + 6, 88, 24);
    const int padTop = progArea.getY() + 74;
    const int ww = pr.getWidth() / 7;
    static const int whiteSlots[] = { 0, 2, 4, 5, 7, 9, 11 };
    for (int i = 0; i < 7; ++i)
        pads[whiteSlots[i]]->setBounds (pr.getX() + i * ww, padTop + 36, ww, 78);
    static const int blackSlots[] = { 1, 3, 6, 8, 10 };
    static const int after[] = { 0, 1, 3, 4, 5 };   // sits between white i and i+1
    for (int i = 0; i < 5; ++i)
    {
        const int cx = pr.getX() + (after[i] + 1) * ww;
        pads[blackSlots[i]]->setBounds (cx - ww * 4 / 10, padTop, ww * 8 / 10, 54);
    }
    auto kr = juce::Rectangle<int> (pr.getX(), progArea.getY() + 196, pr.getWidth(), 60);
    const int kw = kr.getWidth() / 3;
    for (auto* k : { &complexity, &density, &spice })
        k->setBounds (kr.removeFromLeft (kw).withSizeKeepingCentre (54, 54));

    // GROOVE
    auto gr = grooveArea.reduced (14, 0);
    feel.setBounds (gr.getX() + 2, grooveArea.getY() + 46, 200, 30);
    auto gk = juce::Rectangle<int> (gr.getX() + 230, grooveArea.getY() + 24, gr.getWidth() - 230, 54);
    const int gw = gk.getWidth() / 6;
    for (auto* k : { &feelAmt, &swing, &pocket, &dynamics, &strum, &humanize })
        k->setBounds (gk.removeFromLeft (gw).withSizeKeepingCentre (52, 52));

    for (int l = 0; l < hs::kNumLanes; ++l)
        lanes[l]->setBounds (laneAreas[l]);

    // Arp
    {
        auto tabs = juce::Rectangle<int> (arpArea.getRight() - 14 - 3 * 92, arpArea.getY() + 6, 3 * 92, 24);
        for (auto* t : arpTabs) { t->setBounds (tabs.removeFromLeft (88)); tabs.removeFromLeft (4); }
    }
    auto a = arpArea.reduced (14, 0).withTrimmedTop (42).withHeight (40);
    arpOnButton.setBounds (a.removeFromLeft (92).withSizeKeepingCentre (92, 34));
    a.removeFromLeft (18);
    arpRate.setBounds (a.removeFromLeft (100).withSizeKeepingCentre (100, 28));
    a.removeFromLeft (12);
    arpMode.setBounds (a.removeFromLeft (140).withSizeKeepingCentre (140, 28));
    a.removeFromLeft (12);
    arpOct.setBounds (a.removeFromLeft (100).withSizeKeepingCentre (100, 28));
    a.removeFromLeft (20);
    arpGate.setBounds (a.removeFromLeft (48).withSizeKeepingCentre (48, 48).translated (0, -8));

    // Export
    auto e = exportArea.reduced (14, 8).withTrimmedTop (22);
    dragAll.setBounds (e.removeFromLeft (92));
    e.removeFromLeft (8);
    dragSong.setBounds (e.removeFromLeft (92));
    e.removeFromLeft (16);
    auto col = e.removeFromLeft (110);
    midiOutButton.setBounds (col.withSizeKeepingCentre (110, 34));
    e.removeFromLeft (8);
    sketchVol.setBounds (e.removeFromLeft (e.getWidth() / 2).withSizeKeepingCentre (48, 48).translated (0, -6));
    bpm.setBounds (e.withSizeKeepingCentre (48, 48).translated (0, -6));

    renderBackground();
}

void HooksmithPanel::renderBackground()
{
    constexpr float renderScale = 2.0f;   // sharp on Retina screens
    background = juce::Image (juce::Image::ARGB, (int) (kW * renderScale), (int) (kH * renderScale), true);
    juce::Graphics g (background);
    g.addTransform (juce::AffineTransform::scale (renderScale));

    const auto full = juce::Rectangle<float> (0, 0, (float) kW, (float) kH);
    brushed (g, full, Col::chassisTop, Col::chassisBot, 19);

    for (int side = 0; side < 2; ++side)
    {
        auto ear = side == 0 ? full.withWidth (26.0f) : full.withLeft ((float) kW - 26.0f);
        brushed (g, ear, Col::ear.brighter (0.15f), Col::ear.darker (0.3f), 23 + side);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawVerticalLine (side == 0 ? 26 : kW - 27, 0.0f, (float) kH);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.drawVerticalLine (side == 0 ? 27 : kW - 26, 0.0f, (float) kH);
        for (float yy : { 60.0f, (float) kH * 0.5f, (float) kH - 60.0f })
        {
            auto slot = juce::Rectangle<float> (12.0f, 30.0f).withCentre ({ ear.getCentreX(), yy });
            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.fillRoundedRectangle (slot.translated (0.0f, 1.0f), 6.0f);
            g.setColour (juce::Colour (0xff070708));
            g.fillRoundedRectangle (slot, 6.0f);
        }
    }

    // Header faceplate
    {
        const auto hr = headerArea.toFloat();
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.fillRoundedRectangle (hr.translated (0.0f, 2.0f), 6.0f);
        juce::Path p; p.addRoundedRectangle (hr, 6.0f);
        g.saveState(); g.reduceClipRegion (p);
        brushed (g, hr, juce::Colour (0xff3a3d44), juce::Colour (0xff1f2125), 5);
        g.restoreState();
        g.setColour (juce::Colours::black.withAlpha (0.7f));
        g.drawRoundedRectangle (hr, 6.0f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.1f));
        g.drawHorizontalLine ((int) hr.getY() + 1, hr.getX() + 6.0f, hr.getRight() - 6.0f);
        for (auto pt : { hr.getTopLeft().translated (10, 10), hr.getBottomLeft().translated (10, -10),
                         hr.getTopRight().translated (-10, 10), hr.getBottomRight().translated (-10, -10) })
            screw (g, pt, 4.5f, pt.x * 0.37f + pt.y * 0.11f);

        glowText (g, "HOOKSMITH", juce::Rectangle<float> (hr.getX() + 26.0f, hr.getY() + 9.0f, 270.0f, 34.0f),
                  Fonts::logo (25.0f), Col::amber, juce::Justification::centredLeft);
        engraved (g, "PROGRESSOR ENGINE  //  MB AUDIO", juce::Rectangle<float> (hr.getX() + 28.0f, hr.getY() + 42.0f, 270.0f, 14.0f),
                  Fonts::label (11.5f), juce::Justification::centredLeft, Col::dim);
        engraved (g, "MODE", mode.getBounds().toFloat().withY (hr.getY() + 1.0f).withHeight (14.0f), Fonts::label (11.0f),
                  juce::Justification::centredLeft, Col::dim.brighter (0.3f));
        lcdBox (g, lcdArea.toFloat());
    }

    for (auto r : { songArea, screenArea, progArea, grooveArea, laneAreas[0], laneAreas[1], laneAreas[2], laneAreas[3], arpArea, exportArea,
                    colorArea, tapeArea, partArea })
        recess (g, r.toFloat(), 8.0f);

    const auto title = [&] (juce::Rectangle<int> area, const char* t, const char* sub)
    {
        engraved (g, t, area.toFloat().translated (16.0f, 7.0f).withHeight (20.0f), Fonts::label (16.0f), juce::Justification::centredLeft);
        if (sub != nullptr)
        {
            const float tw = juce::GlyphArrangement::getStringWidth (Fonts::label (16.0f), t);
            engraved (g, sub, area.toFloat().translated (16.0f + tw + 14.0f, 8.0f).withHeight (20.0f), Fonts::body (13.0f),
                      juce::Justification::centredLeft, Col::dim);
        }
    };
    const auto caption = [&] (juce::Component& c, const char* text, float dy = -17.0f)
    {
        engraved (g, text, c.getBounds().toFloat().translated (0.0f, dy).withHeight (14.0f), Fonts::label (11.5f),
                  juce::Justification::centredLeft, Col::dim.brighter (0.3f));
    };

    caption (genre, "GENRE"); caption (key, "KEY"); caption (scale, "SCALE");
    caption (*barButtons[0], "LENGTH");
    caption (*sectionButtons[0], "SECTION");

    title (screenArea, "SCREEN", "chords, melody, bass and drums for this section");
    title (progArea, "PROGRESSOR", "chord engine");
    engraved (g, "CHORD BANK  -  click to play", juce::Rectangle<float> ((float) progArea.getX() + 14.0f, (float) pads[1]->getY() - 12.0f, 300.0f, 12.0f),
              Fonts::label (11.0f), juce::Justification::centredLeft, Col::dim.brighter (0.2f));
    const auto knobLabel = [&] (const ui::Knob& k, const char* text)
    {
        engraved (g, text, k.getBounds().toFloat().withY ((float) k.getBottom() - 1.0f).withHeight (14.0f).expanded (30.0f, 0.0f),
                  Fonts::label (11.5f), juce::Justification::centred, Col::dim.brighter (0.3f));
    };
    knobLabel (complexity, "COMPLEXITY");
    knobLabel (density, "DENSITY");
    knobLabel (spice, "SPICE");

    title (grooveArea, "GROOVE", "the feel: timing, accents and swing");
    caption (feel, "FEEL", -16.0f);
    knobLabel (feelAmt, "AMOUNT");
    knobLabel (swing, "SWING");
    knobLabel (pocket, "PUSH  |  LAY BACK");
    knobLabel (dynamics, "DYNAMICS");
    knobLabel (strum, "STRUM");
    knobLabel (humanize, "HUMANIZE");

    title (arpArea, "ARPEGGIATOR", "one per part");
    caption (arpRate, "RATE", -15.0f); caption (arpMode, "MODE", -15.0f); caption (arpOct, "RANGE", -15.0f);
    knobLabel (arpGate, "GATE");

    title (exportArea, "EXPORT", "drag into your DAW");

    title (colorArea, "COLOR", "retro tape colour + classic samplers, per part");
    title (partArea, "PART TONE", "per part");
    knobLabel (pStrum, "STRUM"); knobLabel (pHp, "HIGH PASS"); knobLabel (pLp, "LOW PASS"); knobLabel (pGain, "GAIN  (BUS COMP)");
    {
        // MELODY group: divider + captions
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillRect ((float) melGroup.getX() - 10.0f, (float) melGroup.getY() - 6.0f, 1.5f, (float) melGroup.getHeight() + 10.0f);
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRect ((float) melGroup.getX() - 8.5f, (float) melGroup.getY() - 6.0f, 1.0f, (float) melGroup.getHeight() + 10.0f);
        caption (melHarm, "MELODY HARMONY", -16.0f);
        caption (melFlavor, "MELODY FLAVOR", -16.0f);
    }
    knobLabel (drive, "DRIVE"); knobLabel (wobble, "WOBBLE"); knobLabel (noise, "NOISE");
    knobLabel (crush, "CRUSH"); knobLabel (tone, "TONE"); knobLabel (colMix, "MIX");
    {
        // MACHINE group: a divider, and the machine's name in amber under its knob
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        g.fillRect ((float) machineGroup.getX(), (float) machineGroup.getY() - 6.0f, 1.5f, (float) machineGroup.getHeight() + 10.0f);
        g.setColour (juce::Colours::white.withAlpha (0.05f));
        g.fillRect ((float) machineGroup.getX() + 1.5f, (float) machineGroup.getY() - 6.0f, 1.0f, (float) machineGroup.getHeight() + 10.0f);
        const int m = (int) proc.apvts.getRawParameterValue (HooksmithProcessor::laneParam (colorLane, "machine"))->load();
        const auto name = HooksmithProcessor::machineNames()[m].toUpperCase();
        engraved (g, name, machine.getBounds().toFloat().withY ((float) machine.getBottom() - 1.0f).withHeight (14.0f).expanded (30.0f, 0.0f),
                  Fonts::label (11.5f), juce::Justification::centred, m > 0 ? Col::amber : Col::dim.brighter (0.3f));
        knobLabel (flavor, "FLAVOR"); knobLabel (funk, "FUNK");
        engraved (g, "EXAGGERATE", exag.getBounds().toFloat().withY ((float) exag.getBottom() - 1.0f).withHeight (14.0f).expanded (30.0f, 0.0f),
                  Fonts::label (10.0f), juce::Justification::centred, Col::dim.brighter (0.3f));
    }
    title (tapeArea, "TAPE STOP", nullptr);
    knobLabel (tapeTime, "STOP"); knobLabel (tapeStart, "START");
    engraved (g, "BUILT-IN", sketchVol.getBounds().toFloat().withY ((float) sketchVol.getBottom() - 2.0f).withHeight (14.0f).expanded (20.0f, 0.0f),
              Fonts::label (11.0f), juce::Justification::centred, Col::dim.brighter (0.3f));
    engraved (g, "TEMPO", bpm.getBounds().toFloat().withY ((float) bpm.getBottom() - 2.0f).withHeight (14.0f).expanded (20.0f, 0.0f),
              Fonts::label (11.0f), juce::Justification::centred, Col::dim.brighter (0.3f));
}

void HooksmithPanel::paint (juce::Graphics& g)
{
    g.drawImage (background, getLocalBounds().toFloat());

    // Lane colour stripes
    for (int l = 0; l < hs::kNumLanes; ++l)
    {
        auto r = laneAreas[l].toFloat();
        g.setColour (laneColour (l).withAlpha (0.65f));
        g.fillRoundedRectangle (r.getX() + 10.0f, r.getY() + 3.0f, r.getWidth() - 20.0f, 2.0f, 1.0f);
    }

    // LCD: what's playing
    const auto lcd = lcdArea.toFloat().reduced (12.0f, 4.0f);
    const auto& pat = proc.getPattern();
    const double pos = proc.displayPos.load();
    juce::String top, main;
    if (statusText.isNotEmpty())
    {
        top = "STATUS";
        main = statusText;
    }
    else
    {
        const auto s = proc.currentSettings();
        top = juce::String (hs::genreNames()[(size_t) s.genre]).toUpperCase() + "  /  " + juce::String (hs::sectionNames()[(size_t) s.section]).toUpperCase()
            + "  /  " + juce::String (s.bars) + " BARS";
        const hs::ChordSpan* now = nullptr;
        if (pos >= 0) for (auto& ch : pat.chords) if (pos >= ch.start && pos < ch.start + ch.len) now = &ch;
        if (now != nullptr) main = now->name + "   " + now->roman;
        else main = juce::String (pat.progressionName).toUpperCase() + (pat.minor ? "  (MINOR)" : "  (MAJOR)");
    }
    g.setFont (Fonts::mono (11.0f));
    g.setColour (Col::amber.withAlpha (0.55f));
    g.drawText (top, lcd.withHeight (14.0f), juce::Justification::centredLeft);
    g.drawText (juce::String (proc.displayBpm.load(), 1) + " BPM", lcd.withHeight (14.0f), juce::Justification::centredRight);
    glowText (g, main, lcd.withTrimmedTop (14.0f), Fonts::mono (main.length() > 26 ? 15.0f : 19.0f), Col::amber, juce::Justification::centredLeft);

    // Running LED next to the logo
    led (g, { (float) headerArea.getX() + 16.0f, (float) headerArea.getY() + 26.0f }, 3.5f, Col::green, pos >= 0.0);
}

void HooksmithPanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    for (auto& l : lanes) l->refresh();
    for (auto* p : pads) p->repaint();
    screen.repaint();
    repaint();
}

void HooksmithPanel::timerCallback()
{
    if (const int m = (int) proc.apvts.getRawParameterValue (HooksmithProcessor::laneParam (colorLane, "machine"))->load(); m != shownMachine)
    { shownMachine = m; repaint (colorArea); }
    const int bars = (int) proc.apvts.getRawParameterValue ("bars")->load();
    for (int i = 0; i < barButtons.size(); ++i) barButtons[i]->setToggleState (i == bars, juce::dontSendNotification);
    const int sec = (int) proc.apvts.getRawParameterValue ("section")->load();
    for (int i = 0; i < sectionButtons.size(); ++i) sectionButtons[i]->setToggleState (i == sec, juce::dontSendNotification);

    if ((int) proc.apvts.getRawParameterValue ("genre")->load() != shownGenre || proc.getNumImports() != shownImports) refreshProgressions();
    int progIdx = (int) proc.apvts.getRawParameterValue ("prog")->load();
    if (proc.getActiveImport() >= 0)
        progIdx = (int) hs::progressionNames ((int) proc.apvts.getRawParameterValue ("genre")->load()).size() + 1 + proc.getActiveImport();
    if (progression.getSelectedItemIndex() != progIdx && progIdx < progression.getNumItems())
        progression.setSelectedItemIndex (progIdx, juce::dontSendNotification);

    if (proc.getPatternVersion() != shownVersion)
    {
        shownVersion = proc.getPatternVersion();
        for (auto* p : pads) p->repaint();
        screen.repaint();
    }

    screen.setPlayPos (proc.displayPos.load());
    const int held = proc.displayHeldSlot.load();
    for (int s = 0; s < 12; ++s) pads[s]->setLit (s == held);

    for (int l = 0; l < HooksmithProcessor::kArpLanes; ++l)
    {
        const auto steps = proc.displayArpSteps[(size_t) l].load();
        if (steps != lastArpSteps[(size_t) l]) { lastArpSteps[(size_t) l] = steps; arpBlink[(size_t) l] = 3; }
        else if (arpBlink[(size_t) l] > 0) --arpBlink[(size_t) l];
        lanes[l]->setArpBlink (arpBlink[(size_t) l] > 0);
    }

    {
        const bool stopOn = proc.apvts.getRawParameterValue ("tapeStop")->load() > 0.5f;
        const int ts = proc.displayTape.load();
        tapeButton.setToggleState (stopOn || ts == 1 || ts == 2, juce::dontSendNotification);
        for (int l = 0; l < hs::kNumLanes; ++l)
        {
            const bool on = proc.apvts.getRawParameterValue (HooksmithProcessor::laneParam (l, "colOn"))->load() > 0.5f;
            if (on != colorShown[(size_t) l]) { colorShown[(size_t) l] = on; lanes[l]->repaint(); }
        }
    }

    captureButton.setToggleState (proc.hasCapture(), juce::dontSendNotification);
    if (statusTicks > 0 && --statusTicks == 0) statusText = {};
    repaint (lcdArea);
    repaint (headerArea.withWidth (40));
}

// =============================================================================
// Editor: hosts the panel and scales it (drag the corner to resize)
// =============================================================================
HooksmithEditor::HooksmithEditor (HooksmithProcessor& p)
    : AudioProcessorEditor (p), proc (p), panel (p)
{
    addAndMakeVisible (panel);
    float scale = proc.uiScale;
    if (scale <= 0.0f)
    {
        scale = 1.0f;
        if (auto* d = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
            scale = (float) juce::jlimit (0.6, 1.0, (d->userArea.getHeight() - 110.0) / (double) kH);
    }
    setResizable (true, true);
    setResizeLimits ((int) (kW * 0.55f), (int) (kH * 0.55f), (int) (kW * 1.5f), (int) (kH * 1.5f));
    if (auto* c = getConstrainer()) c->setFixedAspectRatio ((double) kW / (double) kH);
    setSize ((int) std::lround (kW * scale), (int) std::lround (kH * scale));
}

void HooksmithEditor::resized()
{
    const float scale = (float) getWidth() / (float) kW;
    panel.setTransform (juce::AffineTransform::scale (scale));
    panel.setBounds (0, 0, kW, kH);
    proc.uiScale = scale;
}
