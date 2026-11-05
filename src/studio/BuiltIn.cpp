#include "BuiltIn.h"
#include <juce_gui_extra/juce_gui_extra.h>

namespace tabletop
{
namespace
{
    juce::Font gearFont (float h, const char* style = "Bold Italic")
    {
        static const bool have = juce::Font::findAllTypefaceNames().contains ("Avenir Next Condensed");
        return have ? juce::Font (juce::FontOptions ("Avenir Next Condensed", style, h)) : juce::Font (juce::FontOptions (h, juce::Font::bold));
    }

    float peakOf (const float* x, int n)
    {
        float p = 0.0f;
        for (int i = 0; i < n; ++i)
            p = std::max (p, std::abs (x[i]));
        return p;
    }

    void decayPeak (std::atomic<float>& meter, float now) { meter = std::max (now, meter.load() * 0.85f); }

    /** Knobs, faders and keys for the studio's gear. */
    struct GearLook : juce::LookAndFeel_V4
    {
        void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override
        {
            const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
            const float d = std::min (r.getWidth(), r.getHeight()) * 0.78f;
            const auto k = juce::Rectangle<float> (d, d).withCentre (r.getCentre());
            const auto c = k.getCentre();
            // the printed scale
            g.setColour (juce::Colours::white.withAlpha (0.75f));
            for (int i = 0; i <= 10; ++i)
            {
                const float a = start + (end - start) * i / 10.0f, r0 = d * 0.56f, r1 = d * (i % 5 == 0 ? 0.66f : 0.62f);
                g.drawLine (c.x + std::sin (a) * r0, c.y - std::cos (a) * r0, c.x + std::sin (a) * r1, c.y - std::cos (a) * r1, i % 5 == 0 ? 1.6f : 1.0f);
            }
            // shadow, skirt, cap
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillEllipse (k.translated (2.0f, 3.5f));
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4d53), k.getX(), k.getY(), juce::Colour (0xff121315), k.getRight(), k.getBottom(), false));
            g.fillEllipse (k);
            const auto cap = k.reduced (d * 0.12f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3a3d42), cap.getX(), cap.getY(), juce::Colour (0xff1c1d20), cap.getRight(), cap.getBottom(), false));
            g.fillEllipse (cap);
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.drawEllipse (cap.reduced (0.5f), 1.0f);
            const float a = start + (end - start) * pos;
            g.setColour (juce::Colour (0xfff2f2f0));
            g.drawLine (c.x + std::sin (a) * d * 0.12f, c.y - std::cos (a) * d * 0.12f, c.x + std::sin (a) * d * 0.36f, c.y - std::cos (a) * d * 0.36f, d * 0.05f);
        }

        void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float sliderPos, float, float, juce::Slider::SliderStyle, juce::Slider&) override
        {
            const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
            const auto slot = r.withSizeKeepingCentre (6.0f, r.getHeight());
            g.setColour (juce::Colours::white.withAlpha (0.55f));
            for (int i = 0; i <= 10; ++i)
            {
                const float ty = r.getY() + r.getHeight() * i / 10.0f;
                g.fillRect (slot.getX() - 14.0f, ty - 0.5f, 9.0f, 1.0f);
                g.fillRect (slot.getRight() + 5.0f, ty - 0.5f, 9.0f, 1.0f);
            }
            g.setColour (juce::Colours::black.withAlpha (0.85f));
            g.fillRoundedRectangle (slot, 3.0f);
            const auto cap = juce::Rectangle<float> (std::min (r.getWidth(), 44.0f), 26.0f).withCentre ({ r.getCentreX(), sliderPos });
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.fillRoundedRectangle (cap.translated (2.0f, 4.0f), 4.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff5a5d63), 0, cap.getY(), juce::Colour (0xff222327), 0, cap.getBottom(), false));
            g.fillRoundedRectangle (cap, 4.0f);
            g.setColour (juce::Colours::white.withAlpha (0.18f));
            for (int i = 1; i < 5; ++i)
                g.fillRect (cap.getX() + 4.0f, cap.getY() + cap.getHeight() * i / 5.0f, cap.getWidth() - 8.0f, 1.0f);
            g.setColour (juce::Colour (0xfff2f2f0));
            g.fillRect (cap.withSizeKeepingCentre (cap.getWidth() - 6.0f, 2.0f));
        }

        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
        {
            auto r = b.getLocalBounds().toFloat().reduced (2.0f);
            const bool on = b.getToggleState();
            g.setColour (juce::Colours::black.withAlpha (0.5f));
            g.fillRoundedRectangle (r.translated (0.0f, 3.0f), 5.0f);
            r = r.translated (0.0f, down ? 2.0f : 0.0f);
            g.setGradientFill (juce::ColourGradient (juce::Colour (on ? 0xffe25a4a : 0xffd8d9db).brighter (over ? 0.06f : 0.0f), 0, r.getY(),
                                                     juce::Colour (on ? 0xffa8261c : 0xffa9abaf), 0, r.getBottom(), false));
            g.fillRoundedRectangle (r, 5.0f);
        }

        void drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool down) override
        {
            g.setFont (gearFont (13.0f));
            g.setColour (b.getToggleState() ? juce::Colours::white : juce::Colour (0xff1b1d22));
            g.drawText (b.getButtonText(), b.getLocalBounds().translated (0, down ? 2 : 0), juce::Justification::centred);
        }
    };

    GearLook& gearLook()
    {
        static GearLook look;
        return look;
    }

    void ledMeter (juce::Graphics& g, juce::Rectangle<float> r, float level, bool vertical = true)
    {
        const int segs = 14;
        const float db = juce::Decibels::gainToDecibels (level, -60.0f);
        const float lit = juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 51.0f) * segs;
        for (int i = 0; i < segs; ++i)
        {
            auto s = vertical ? juce::Rectangle<float> (r.getX(), r.getBottom() - (i + 1) * r.getHeight() / segs, r.getWidth(), r.getHeight() / segs).reduced (0.0f, 1.2f)
                              : juce::Rectangle<float> (r.getX() + i * r.getWidth() / segs, r.getY(), r.getWidth() / segs, r.getHeight()).reduced (1.2f, 0.0f);
            const auto c = i >= segs - 2 ? juce::Colour (0xffff3b2f) : i >= segs - 5 ? juce::Colour (0xffffc23a) : juce::Colour (0xff3dff6a);
            g.setColour (i < lit ? c : c.withAlpha (0.12f));
            g.fillRoundedRectangle (s, 1.5f);
        }
    }

    void brushed (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour base, uint32_t seed)
    {
        g.setGradientFill (juce::ColourGradient (base.brighter (0.12f), r.getX(), r.getY(), base.darker (0.18f), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, 10.0f);
        juce::Random rng ((juce::int64) seed);
        for (float y = r.getY() + 2; y < r.getBottom() - 2; y += 1.5f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.012f + 0.03f * rng.nextFloat()));
            g.drawHorizontalLine ((int) y, r.getX() + 6, r.getRight() - 6);
        }
        g.setColour (juce::Colours::white.withAlpha (0.16f));
        g.drawRoundedRectangle (r.reduced (1.0f), 9.0f, 1.2f);
        g.setColour (juce::Colours::black.withAlpha (0.6f));
        g.drawRoundedRectangle (r, 10.0f, 1.5f);
    }

    // ---------------------------------------------------------------- the interface's panel

    class InterfaceEditor : public juce::AudioProcessorEditor, private juce::Timer
    {
    public:
        explicit InterfaceEditor (InterfaceProcessor& p) : juce::AudioProcessorEditor (p), proc (p)
        {
            setLookAndFeel (&gearLook());
            volume.setSliderStyle (juce::Slider::RotaryVerticalDrag);
            volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            volume.setRange (0.0, 1.0);
            volume.setValue (proc.volume.load(), juce::dontSendNotification);
            volume.onValueChange = [this] { proc.volume = (float) volume.getValue(); };
            addAndMakeVisible (volume);
            setSize (InterfaceProcessor::kPanelWidth, InterfaceProcessor::kPanelHeight);
            startTimerHz (30);
        }
        ~InterfaceEditor() override { setLookAndFeel (nullptr); }

        void resized() override { volume.setBounds (620, 120, 240, 240); }

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            g.fillAll (juce::Colour (0xff0c0d0f));
            brushed (g, r.reduced (6.0f), juce::Colour (0xff1e2024), 3);
            g.setColour (juce::Colours::white);
            g.setFont (gearFont (46.0f, "Heavy Italic"));
            g.drawText ("tabletop", 48, 34, 320, 56, juce::Justification::centredLeft);
            g.setFont (gearFont (18.0f));
            g.setColour (juce::Colours::white.withAlpha (0.75f));
            g.drawText ("AI-2  AUDIO INTERFACE", 52, 88, 360, 24, juce::Justification::centredLeft);
            // the meters: OUT L / R to the speakers, IN 1 / 2 from the computer
            const char* labels[4] = { "L", "R", "1", "2" };
            for (int i = 0; i < 4; ++i)
            {
                const float x = 70.0f + i * 62.0f + (i >= 2 ? 70.0f : 0.0f);
                ledMeter (g, { x, 150, 34, 220 }, proc.peaks[(size_t) i].load());
                g.setColour (juce::Colours::white.withAlpha (0.85f));
                g.setFont (gearFont (16.0f));
                g.drawText (labels[i], (int) x - 10, 378, 54, 20, juce::Justification::centred);
            }
            g.setFont (gearFont (15.0f));
            g.drawText ("SPEAKERS", 62, 404, 140, 22, juce::Justification::centred);
            g.drawText ("COMPUTER IN", 256, 404, 150, 22, juce::Justification::centred);
            g.drawText ("VOLUME", 620, 372, 240, 22, juce::Justification::centred);
            // MIDI activity and the power light
            const bool midi = juce::Time::getMillisecondCounter() - (juce::uint32) proc.midiActivity.load() < 120u;
            auto led = [&] (juce::Point<float> c, juce::Colour col, bool on, const juce::String& text) {
                if (on)
                {
                    g.setGradientFill (juce::ColourGradient (col.withAlpha (0.6f), c, col.withAlpha (0.0f), c.translated (16, 0), true));
                    g.fillEllipse (juce::Rectangle<float> (32, 32).withCentre (c));
                }
                g.setColour (on ? col : col.withAlpha (0.2f));
                g.fillEllipse (juce::Rectangle<float> (12, 12).withCentre (c));
                g.setColour (juce::Colours::white.withAlpha (0.8f));
                g.drawText (text, (int) c.x + 14, (int) c.y - 10, 90, 20, juce::Justification::centredLeft);
            };
            led ({ 470, 170 }, juce::Colour (0xff4aa8ff), true, "POWER");
            led ({ 470, 210 }, juce::Colour (0xff3dff6a), midi, "MIDI");
        }

    private:
        void timerCallback() override { repaint (40, 140, 520, 300); }
        InterfaceProcessor& proc;
        juce::Slider volume;
    };

    // ---------------------------------------------------------------- the mixer's panel

    class MixerEditor : public juce::AudioProcessorEditor, private juce::Timer
    {
    public:
        explicit MixerEditor (MixerProcessor& p) : juce::AudioProcessorEditor (p), proc (p)
        {
            setLookAndFeel (&gearLook());
            for (int c = 0; c < MixerProcessor::kChannels; ++c)
            {
                auto& ch = proc.channels[(size_t) c];
                auto& s = strips[(size_t) c];
                s.pan.setSliderStyle (juce::Slider::RotaryVerticalDrag);
                s.pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                s.pan.setRange (-1.0, 1.0);
                s.pan.setDoubleClickReturnValue (true, 0.0);
                s.pan.setValue (ch.pan.load(), juce::dontSendNotification);
                s.pan.onValueChange = [&ch, &s] { ch.pan = (float) s.pan.getValue(); };
                s.fader.setSliderStyle (juce::Slider::LinearVertical);
                s.fader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
                s.fader.setRange (0.0, 1.0);
                s.fader.setDoubleClickReturnValue (true, 0.75);
                s.fader.setValue (ch.level.load(), juce::dontSendNotification);
                s.fader.onValueChange = [&ch, &s] { ch.level = (float) s.fader.getValue(); };
                s.mute.setButtonText ("MUTE");
                s.mute.setClickingTogglesState (true);
                s.mute.setToggleState (ch.mute.load(), juce::dontSendNotification);
                s.mute.onClick = [&ch, &s] { ch.mute = s.mute.getToggleState(); };
                for (juce::Component* comp : { (juce::Component*) &s.pan, (juce::Component*) &s.fader, (juce::Component*) &s.mute })
                    addAndMakeVisible (comp);
            }
            masterFader.setSliderStyle (juce::Slider::LinearVertical);
            masterFader.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            masterFader.setRange (0.0, 1.0);
            masterFader.setDoubleClickReturnValue (true, 0.75);
            masterFader.setValue (proc.master.load(), juce::dontSendNotification);
            masterFader.onValueChange = [this] { proc.master = (float) masterFader.getValue(); };
            addAndMakeVisible (masterFader);
            setSize (MixerProcessor::kPanelWidth, MixerProcessor::kPanelHeight);
            startTimerHz (30);
        }
        ~MixerEditor() override { setLookAndFeel (nullptr); }

        void resized() override
        {
            for (int c = 0; c < MixerProcessor::kChannels; ++c)
            {
                auto& s = strips[(size_t) c];
                const int x = 50 + c * 132;
                s.pan.setBounds (x + 16, 120, 96, 96);
                s.mute.setBounds (x + 22, 236, 84, 44);
                s.fader.setBounds (x + 20, 320, 60, 400);
            }
            masterFader.setBounds (900, 320, 60, 400);
        }

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            g.fillAll (juce::Colour (0xff101113));
            brushed (g, r.reduced (6.0f), juce::Colour (0xff5d6168), 9);
            g.setColour (juce::Colours::white);
            g.setFont (gearFont (40.0f, "Heavy Italic"));
            g.drawText ("MX-6", 50, 30, 200, 52, juce::Justification::centredLeft);
            g.setFont (gearFont (18.0f));
            g.setColour (juce::Colours::white.withAlpha (0.8f));
            g.drawText ("STEREO MIXER", 170, 44, 240, 30, juce::Justification::centredLeft);
            for (int c = 0; c < MixerProcessor::kChannels; ++c)
            {
                const float x = 50.0f + c * 132.0f;
                g.setColour (juce::Colours::black.withAlpha (0.18f));
                g.fillRoundedRectangle (x + 4, 100, 120, 670, 8.0f);
                g.setColour (juce::Colours::white.withAlpha (0.9f));
                g.setFont (gearFont (24.0f, "Heavy Italic"));
                g.drawText (juce::String (c + 1), (int) x, 726, 128, 34, juce::Justification::centred);
                g.setFont (gearFont (13.0f));
                g.drawText ("PAN", (int) x, 100, 128, 18, juce::Justification::centred);
                ledMeter (g, { x + 92, 330, 14, 380 }, proc.channels[(size_t) c].peak.load());
            }
            g.setColour (juce::Colours::black.withAlpha (0.22f));
            g.fillRoundedRectangle (860, 100, 200, 670, 8.0f);
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.setFont (gearFont (24.0f, "Heavy Italic"));
            g.drawText ("MAIN", 860, 726, 140, 34, juce::Justification::centred);
            ledMeter (g, { 984, 330, 14, 380 }, proc.mainPeaks[0].load());
            ledMeter (g, { 1008, 330, 14, 380 }, proc.mainPeaks[1].load());
        }

    private:
        void timerCallback() override { repaint(); }
        struct Strip
        {
            juce::Slider pan, fader;
            juce::TextButton mute;
        };
        MixerProcessor& proc;
        std::array<Strip, MixerProcessor::kChannels> strips;
        juce::Slider masterFader;
    };

    juce::var parseState (const void* data, int size) { return juce::JSON::parse (juce::String::fromUTF8 (static_cast<const char*> (data), size)); }
    void writeState (juce::MemoryBlock& dest, const juce::var& v)
    {
        const auto text = juce::JSON::toString (v, true);
        dest.replaceAll (text.toRawUTF8(), text.getNumBytesAsUTF8());
    }
} // namespace

// ---------------------------------------------------------------- interface

InterfaceProcessor::InterfaceProcessor()
    : juce::AudioProcessor (BusesProperties()
                                .withInput ("To Speakers", juce::AudioChannelSet::stereo(), true)
                                .withInput ("Computer In", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Speakers", juce::AudioChannelSet::stereo(), true)
                                .withOutput ("Computer In", juce::AudioChannelSet::stereo(), true))
{
}

void InterfaceProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    const int n = buffer.getNumSamples();
    if (buffer.getNumChannels() < 4)
        return;
    if (! speakersStereo.load())
        buffer.copyFrom (1, 0, buffer, 0, 0, n); // one cable: both speakers
    const float v = volume.load(), target = v * v;
    buffer.applyGainRamp (0, 0, n, smoothed, target);
    buffer.applyGainRamp (1, 0, n, smoothed, target);
    smoothed = target;
    for (int c = 0; c < 4; ++c)
        decayPeak (peaks[(size_t) c], peakOf (buffer.getReadPointer (c), n));
    // (channels 2, 3 pass straight through: the computer's inputs, out of the IN jacks; MIDI passes through too)
    if (! midi.isEmpty())
        midiActivity = (int) juce::Time::getMillisecondCounter();
}

juce::AudioProcessorEditor* InterfaceProcessor::createEditor() { return new InterfaceEditor (*this); }

void InterfaceProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("volume", volume.load());
    writeState (dest, juce::var (o));
}

void InterfaceProcessor::setStateInformation (const void* data, int size)
{
    const auto v = parseState (data, size);
    volume = (float) (double) v.getProperty ("volume", 0.8);
}

// ---------------------------------------------------------------- mixer

MixerProcessor::MixerProcessor()
    : juce::AudioProcessor ([] {
          BusesProperties b;
          for (int c = 0; c < kChannels; ++c)
              b = b.withInput ("Channel " + juce::String (c + 1), juce::AudioChannelSet::stereo(), true);
          return b.withOutput ("Main", juce::AudioChannelSet::stereo(), true);
      }())
{
}

void MixerProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    midi.clear();
    const int n = buffer.getNumSamples();
    if (n > scratch.getNumSamples() || buffer.getNumChannels() < kChannels * 2)
        return;
    scratch.clear (0, n);
    bool anySolo = false;
    juce::ignoreUnused (anySolo);
    for (int c = 0; c < kChannels; ++c)
    {
        auto& ch = channels[(size_t) c];
        const float* inL = buffer.getReadPointer (c * 2);
        const float* inR = ch.stereo.load() ? buffer.getReadPointer (c * 2 + 1) : inL;
        // fader law: 3/4 of the way up is unity, the top +5 dB
        const float lv = ch.level.load() / 0.75f, g = ch.mute.load() ? 0.0f : lv * lv;
        const float p = juce::jlimit (-1.0f, 1.0f, ch.pan.load()), angle = (p + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        const float tl = g * std::cos (angle) * juce::MathConstants<float>::sqrt2, tr = g * std::sin (angle) * juce::MathConstants<float>::sqrt2;
        float peak = 0.0f;
        auto* outL = scratch.getWritePointer (0);
        auto* outR = scratch.getWritePointer (1);
        for (int i = 0; i < n; ++i)
        {
            const float t = (float) (i + 1) / (float) n;
            const float gl = ch.gainL + (tl - ch.gainL) * t, gr = ch.gainR + (tr - ch.gainR) * t;
            const float l = inL[i] * gl, r = inR[i] * gr;
            outL[i] += l;
            outR[i] += r;
            peak = std::max (peak, std::max (std::abs (l), std::abs (r)));
        }
        ch.gainL = tl;
        ch.gainR = tr;
        decayPeak (ch.peak, peak);
    }
    const float mv = master.load() / 0.75f, target = mv * mv;
    scratch.applyGainRamp (0, 0, n, masterSmoothed, target);
    scratch.applyGainRamp (1, 0, n, masterSmoothed, target);
    masterSmoothed = target;
    buffer.copyFrom (0, 0, scratch, 0, 0, n);
    buffer.copyFrom (1, 0, scratch, 1, 0, n);
    decayPeak (mainPeaks[0], peakOf (scratch.getReadPointer (0), n));
    decayPeak (mainPeaks[1], peakOf (scratch.getReadPointer (1), n));
}

juce::AudioProcessorEditor* MixerProcessor::createEditor() { return new MixerEditor (*this); }

void MixerProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto* o = new juce::DynamicObject();
    juce::Array<juce::var> chs;
    for (auto& ch : channels)
    {
        auto* c = new juce::DynamicObject();
        c->setProperty ("level", ch.level.load());
        c->setProperty ("pan", ch.pan.load());
        c->setProperty ("mute", ch.mute.load());
        chs.add (juce::var (c));
    }
    o->setProperty ("channels", chs);
    o->setProperty ("master", master.load());
    writeState (dest, juce::var (o));
}

void MixerProcessor::setStateInformation (const void* data, int size)
{
    const auto v = parseState (data, size);
    if (auto* chs = v["channels"].getArray())
        for (int i = 0; i < std::min (kChannels, chs->size()); ++i)
        {
            const auto& c = chs->getReference (i);
            channels[(size_t) i].level = (float) (double) c.getProperty ("level", 0.75);
            channels[(size_t) i].pan = (float) (double) c.getProperty ("pan", 0.0);
            channels[(size_t) i].mute = (bool) c.getProperty ("mute", false);
        }
    master = (float) (double) v.getProperty ("master", 0.8);
}
} // namespace tabletop
