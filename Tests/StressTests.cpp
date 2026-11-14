// Hammers the arps like Ableton would: odd block sizes, loops, toggling every arp setting.
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <cstdio>
#include <cstdlib>
#include <random>

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

static void setParam (HooksmithProcessor& p, const juce::String& id, float v)
{
    auto* prm = p.apvts.getParameter (id);
    if (prm == nullptr) { std::printf ("no param %s\n", id.toRawUTF8()); return; }
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    std::mt19937 rng (1);
    const double sr = 44100.0;
    const bool withEditor = argc > 1;

    for (int round = 0; round < 6; ++round)
    {
        HooksmithProcessor p;
        FakeHead head;
        p.setPlayHead (&head);
        p.setPlayConfigDetails (0, 2, sr, 512);
        p.prepareToPlay (sr, 512);
        setParam (p, "genre", (float) (round * 3 % 17));
        p.generateAll();
        std::unique_ptr<juce::AudioProcessorEditor> ed;
        if (withEditor) ed.reset (p.createEditor());

        juce::AudioBuffer<float> buf (2, 4096);
        for (int b = 0; b < 4000; ++b)
        {
            if (b % 37 == 0)
            {
                const int l = (int) (rng() % 3) + 1;
                const juce::String pre = "lane" + juce::String (l) + "_";
                setParam (p, pre + "arpOn", (float) (rng() % 2));
                setParam (p, pre + "arpRate", (float) (rng() % 12));
                setParam (p, pre + "arpMode", (float) (rng() % 5));
                setParam (p, pre + "arpOct", (float) (rng() % 4 + 1));
                setParam (p, pre + "arpGate", 0.05f + (rng() % 95) / 100.0f);
            }
            if (b % 211 == 0) setParam (p, "mode", (float) (rng() % 3));
            if (b % 97 == 0) head.playing = ! head.playing;
            if (b % 151 == 0) head.ppq = (rng() % 64) - 4.0;   // loop / relocate / pre-roll
            if (b % 300 == 0) p.generateAll();

            const int n = (int) (rng() % 4) == 0 ? (int) (rng() % 4096) + 1 : 512;
            juce::AudioBuffer<float> view (buf.getArrayOfWritePointers(), 2, n);
            juce::MidiBuffer midi;
            if (rng() % 5 == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 40 + (int) (rng() % 40), (juce::uint8) 100), (int) (rng() % n));
            if (rng() % 5 == 0) midi.addEvent (juce::MidiMessage::noteOff (1, 40 + (int) (rng() % 40)), (int) (rng() % n));
            p.processBlock (view, midi);
            if (withEditor && b % 20 == 0) { ed->setSize (900 + (b % 3) * 100, 760); ed->repaint(); };
            head.ppq += n * head.bpm / (60.0 * sr);
        }
        std::printf ("round %d ok\n", round);
    }
    // Click the ARP ON buttons like a user (sync click, same path as a mouse click)
    {
        HooksmithProcessor p;
        p.setPlayConfigDetails (0, 2, sr, 512);
        p.prepareToPlay (sr, 512);
        p.generateAll();
        for (const char* id : { "lane1_colOn", "lane4_colOn" }) setParam (p, id, 1.0f);
        p.storeSlot (hs::MelodyLane, 0); p.rerollLane (hs::MelodyLane); p.storeSlot (hs::MelodyLane, 2); p.recallSlot (hs::MelodyLane, 0);
        p.storeSlot (hs::BassLane, 1);
        setParam (p, "lane1_machine", 4.0f); setParam (p, "lane1_flavor", 0.8f); setParam (p, "lane1_exag", 0.5f); setParam (p, "lane1_funk", 0.7f);
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        std::function<void (juce::Component&)> walk = [&] (juce::Component& c)
        {
            if (auto* b = dynamic_cast<juce::Button*> (&c))
                if (b->getTooltip().containsIgnoreCase ("arpeggiat"))
                    for (int i = 0; i < 4; ++i)
                    {
                        std::printf ("click: %s\n", b->getTooltip().substring (0, 40).toRawUTF8());
                        b->setToggleState (! b->getToggleState(), juce::sendNotificationSync);
                    }
            for (auto* ch : c.getChildren()) walk (*ch);
        };
        walk (*ed);
        std::printf ("clicks ok\n");
        if (const char* shot = std::getenv ("HS_SHOT"))
        {
            ed->setSize (HooksmithPanel::designW, HooksmithPanel::designH);
            if (auto* hp = dynamic_cast<HooksmithPanel*> (ed->getChildComponent (0))) hp->tick();
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::FileOutputStream os { juce::File (shot) };
            os.setPosition (0); os.truncate();
            juce::PNGImageFormat().writeImageToStream (img, os);
        }
    }
    std::printf ("STRESS DONE\n");
    return 0;
}
