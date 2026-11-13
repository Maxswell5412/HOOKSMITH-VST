// Renders HOOKSMITH's real screen + real sound for promo videos.
// VideoKit <out.wav> <seconds> <bpm> [actions...] > frames.bgra   (1274x1080 @ 30 fps)
// actions:  T:param=value   T:gen   T:genbtn   T0-T1:param=from>to (sweep)
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

struct Head : juce::AudioPlayHead
{
    double ppq = 0, bpm = 120;
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p; p.setBpm (bpm); p.setPpqPosition (ppq); p.setIsPlaying (true);
        p.setTimeSignature (TimeSignature { 4, 4 });
        return p;
    }
};

struct Action { double t0, t1; juce::String key; float a = 0, b = 0; bool done = false; };

static void setParam (HooksmithProcessor& p, const juce::String& id, float v)
{
    if (auto* prm = p.apvts.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    else std::fprintf (stderr, "no param %s\n", id.toRawUTF8());
}

static juce::Button* findButton (juce::Component& c, const juce::String& text)
{
    if (auto* b = dynamic_cast<juce::Button*> (&c)) if (b->getButtonText() == text) return b;
    for (auto* ch : c.getChildren()) if (auto* f = findButton (*ch, text)) return f;
    return nullptr;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 4) { std::fprintf (stderr, "usage\n"); return 1; }
    const juce::File wav { juce::String (argv[1]) };
    const double seconds = juce::String (argv[2]).getDoubleValue();
    const double sr = 48000.0; const int fps = 30; const int block = (int) sr / fps;
    std::vector<Action> acts;
    for (int i = 4; i < argc; ++i)
    {
        juce::String s (argv[i]);
        Action a;
        auto time = s.upToFirstOccurrenceOf (":", false, false), rest = s.fromFirstOccurrenceOf (":", false, false);
        a.t0 = time.upToFirstOccurrenceOf ("-", false, false).getDoubleValue();
        a.t1 = time.contains ("-") ? time.fromFirstOccurrenceOf ("-", false, false).getDoubleValue() : a.t0;
        a.key = rest.upToFirstOccurrenceOf ("=", false, false);
        auto val = rest.fromFirstOccurrenceOf ("=", false, false);
        a.a = val.upToFirstOccurrenceOf (">", false, false).getFloatValue();
        a.b = val.contains (">") ? val.fromFirstOccurrenceOf (">", false, false).getFloatValue() : a.a;
        acts.push_back (a);
    }

    HooksmithProcessor p;
    Head head; head.bpm = juce::String (argv[3]).getDoubleValue();
    p.setPlayHead (&head);
    p.setPlayConfigDetails (0, 2, sr, block);
    p.prepareToPlay (sr, block);
    setParam (p, "bpm", (float) head.bpm);
    // actions at t=0 that are plain params go before the first idea
    for (auto& a : acts) if (a.t0 <= 0 && a.t1 <= 0 && a.key != "gen" && a.key != "genbtn") { setParam (p, a.key, a.a); a.done = true; }
    p.generateAll();

    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    juce::Component* panel = ed->getChildComponent (0);
    panel->setBounds (0, 0, 1180, 1000);
    auto* hp = dynamic_cast<HooksmithPanel*> (panel);

    const int frames = (int) std::lround (seconds * fps);
    juce::AudioBuffer<float> all (2, frames * block);
    juce::AudioBuffer<float> buf (2, block);
    const float scale = (1080.0f / 1000.0f) * (std::getenv ("VK_SCALE") ? (float) std::atof (std::getenv ("VK_SCALE")) : 1.0f);
    std::vector<uint8_t> row;

    for (int f = 0; f < frames; ++f)
    {
        const double t = (double) f / fps;
        for (auto& a : acts)
        {
            if (a.done) continue;
            if (a.t1 > a.t0)   // sweep
            {
                if (t >= a.t0)
                {
                    const double k = juce::jlimit (0.0, 1.0, (t - a.t0) / (a.t1 - a.t0));
                    setParam (p, a.key, (float) (a.a + (a.b - a.a) * k));
                    if (t >= a.t1) a.done = true;
                }
            }
            else if (t >= a.t0)
            {
                if (a.key == "gen") p.generateAll();
                else if (a.key == "genbtn") { if (auto* b = findButton (*panel, "Generate All")) b->onClick(); }
                else setParam (p, a.key, a.a);
                a.done = true;
            }
        }
        p.refreshNow();

        buf.clear();
        juce::MidiBuffer midi;
        p.processBlock (buf, midi);
        for (int ch = 0; ch < 2; ++ch) all.copyFrom (ch, f * block, buf, ch, 0, block);
        head.ppq += block * head.bpm / (60.0 * sr);

        if (hp) hp->tick();
        auto img = panel->createComponentSnapshot (panel->getLocalBounds(), true, scale);
        img = img.convertedToFormat (juce::Image::ARGB);
        const juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
        for (int y = 0; y < bd.height; ++y)   // ARGB in memory = B, G, R, A (premultiplied, opaque here)
            std::fwrite (bd.getLinePointer (y), 1, (size_t) bd.width * 4, stdout);
        if (f == 0) std::fprintf (stderr, "frame %dx%d\n", bd.width, bd.height);
    }
    std::fflush (stdout);

    wav.deleteFile();
    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::AudioFormatWriter> w (fmt.createWriterFor (new juce::FileOutputStream (wav), sr, 2, 24, {}, 0));
    w->writeFromAudioSampleBuffer (all, 0, all.getNumSamples());
    return 0;
}
