#include "Registry.h"
#include "BuiltIn.h"
#include "fourpole/src/plugin/FourpoleProcessor.h"
#include "padlab/src/plugin/PadlabProcessor.h"
#include "trackdesk/src/plugin/TrackdeskProcessor.h"
#include "valvebox/src/plugin/ValveboxProcessor.h"

namespace tabletop
{
namespace
{
    using K = JackSpec::Kind;

    std::vector<DeviceType> makeTypes()
    {
        std::vector<DeviceType> t;
        {
            DeviceType d;
            d.id = "valvebox";
            d.name = "Valvebox VB-16";
            d.blurb = "Groovebox: drums, synths, effects, sequencer";
            d.widthMm = 358.0f;
            d.panelWidth = valvebox::ValveboxProcessor::kPanelWidth;
            d.panelHeight = valvebox::ValveboxProcessor::kPanelHeight;
            d.depthMm = d.widthMm * (float) d.panelHeight / (float) d.panelWidth;
            d.rear = juce::Colour (0xff1c3f7e);
            d.jacks = { { "MIDI IN", K::MidiIn }, { "MIDI OUT", K::MidiOut }, { "AUDIO IN", K::AudioIn, 0 }, { "OUT 3", K::AudioOut, 2 },
                        { "OUT 4", K::AudioOut, 3 },  { "OUT L", K::AudioOut, 0 }, { "OUT R", K::AudioOut, 1 } };
            d.create = [] { return std::make_unique<valvebox::ValveboxProcessor>(); };
            t.push_back (d);
        }
        {
            DeviceType d;
            d.id = "padlab";
            d.name = "Padlab";
            d.blurb = "Sampling drum machine and sequencer";
            d.widthMm = 404.0f;
            d.panelWidth = padlab::PadlabProcessor::kPanelWidth;
            d.panelHeight = padlab::PadlabProcessor::kPanelHeight;
            d.depthMm = d.widthMm * (float) d.panelHeight / (float) d.panelWidth;
            d.rear = juce::Colour (0xff2b2d31);
            d.jacks = { { "MIDI IN", K::MidiIn }, { "MIDI OUT", K::MidiOut }, { "REC IN L", K::AudioIn, 0 }, { "REC IN R", K::AudioIn, 1 },
                        { "OUT L", K::AudioOut, 0 }, { "OUT R", K::AudioOut, 1 } };
            d.create = [] { return std::make_unique<padlab::PadlabProcessor>(); };
            t.push_back (d);
        }
        {
            DeviceType d;
            d.id = "fourpole";
            d.name = "Fourpole";
            d.blurb = "Three-oscillator monophonic synthesizer";
            d.widthMm = 714.0f;
            d.panelWidth = fourpole::FourpoleProcessor::kPanelWidth;
            d.panelHeight = fourpole::FourpoleProcessor::kPanelHeight;
            d.depthMm = d.widthMm * (float) d.panelHeight / (float) d.panelWidth;
            d.rear = juce::Colour (0xff2a2622);
            d.jacks = { { "MIDI IN", K::MidiIn }, { "EXT IN", K::AudioIn, 0 }, { "OUTPUT", K::AudioOut, 0 } };
            d.create = [] { return std::make_unique<fourpole::FourpoleProcessor>(); };
            t.push_back (d);
        }
        {
            DeviceType d;
            d.id = "trackdesk";
            d.name = "Trackdesk TD-12";
            d.blurb = "12-track recording console: 10 inputs, effects, recorder";
            d.widthMm = 343.0f;
            d.panelWidth = trackdesk::TrackdeskProcessor::kPanelWidth;
            d.panelHeight = trackdesk::TrackdeskProcessor::kPanelHeight;
            d.depthMm = d.widthMm * (float) d.panelHeight / (float) d.panelWidth;
            d.rear = juce::Colour (0xff1e1f22);
            // the back panel's order, seen from above: the inputs, MIDI and CLICK, the outputs, the computer
            const char* inputs[] = { "IN 1", "IN 2", "IN 3", "IN 4", "IN 5", "IN 6", "7/8 L", "7/8 R", "9/10 L", "9/10 R" };
            for (int c = 0; c < 10; ++c)
                d.jacks.push_back ({ inputs[c], K::AudioIn, c });
            d.jacks.push_back ({ "MIDI OUT", K::MidiOut });
            d.jacks.push_back ({ "CLICK", K::AudioOut, 10 });
            d.jacks.push_back ({ "AUX 1", K::AudioOut, 4 });
            d.jacks.push_back ({ "AUX 2", K::AudioOut, 5 });
            d.jacks.push_back ({ "SUB L", K::AudioOut, 2 });
            d.jacks.push_back ({ "SUB R", K::AudioOut, 3 });
            d.jacks.push_back ({ "MAIN L", K::AudioOut, 0 });
            d.jacks.push_back ({ "MAIN R", K::AudioOut, 1 });
            d.jacks.push_back ({ "USB L", K::AudioIn, 10 });
            d.jacks.push_back ({ "USB R", K::AudioIn, 11 });
            d.jacks.push_back ({ "PHONES 1 L", K::AudioOut, 6 });
            d.jacks.push_back ({ "PHONES 1 R", K::AudioOut, 7 });
            d.jacks.push_back ({ "PHONES 2 L", K::AudioOut, 8 });
            d.jacks.push_back ({ "PHONES 2 R", K::AudioOut, 9 });
            d.create = [] { return std::make_unique<trackdesk::TrackdeskProcessor>(); };
            t.push_back (d);
        }
        {
            DeviceType d;
            d.id = "mixer";
            d.name = "Mixer MX-6";
            d.blurb = "Six stereo channels into the main out";
            d.widthMm = 330.0f;
            d.panelWidth = MixerProcessor::kPanelWidth;
            d.panelHeight = MixerProcessor::kPanelHeight;
            d.depthMm = d.widthMm * (float) d.panelHeight / (float) d.panelWidth;
            d.rear = juce::Colour (0xff2d2f33);
            for (int c = 0; c < MixerProcessor::kChannels; ++c)
            {
                d.jacks.push_back ({ juce::String (c + 1) + " L", K::AudioIn, c * 2 });
                d.jacks.push_back ({ juce::String (c + 1) + " R", K::AudioIn, c * 2 + 1 });
            }
            d.jacks.push_back ({ "MAIN L", K::AudioOut, 0 });
            d.jacks.push_back ({ "MAIN R", K::AudioOut, 1 });
            d.create = [] { return std::make_unique<MixerProcessor>(); };
            t.push_back (d);
        }
        {
            DeviceType d;
            d.id = "interface";
            d.name = "Audio Interface";
            d.blurb = "Your speakers, your computer's inputs and MIDI controllers";
            d.widthMm = 240.0f;
            d.panelWidth = InterfaceProcessor::kPanelWidth;
            d.panelHeight = InterfaceProcessor::kPanelHeight;
            d.depthMm = d.widthMm * (float) d.panelHeight / (float) d.panelWidth;
            d.rear = juce::Colour (0xff1d1e21);
            d.jacks = { { "MIDI", K::MidiOut }, { "IN 1", K::AudioOut, 2 }, { "IN 2", K::AudioOut, 3 }, { "SPEAKERS L", K::AudioIn, 0 },
                        { "SPEAKERS R", K::AudioIn, 1 } };
            d.create = [] { return std::make_unique<InterfaceProcessor>(); };
            d.unique = true;
            t.push_back (d);
        }
        return t;
    }
} // namespace

const std::vector<DeviceType>& deviceTypes()
{
    static const std::vector<DeviceType> types = makeTypes();
    return types;
}

const DeviceType* findDeviceType (const juce::String& id)
{
    for (auto& t : deviceTypes())
        if (t.id == id)
            return &t;
    return nullptr;
}
} // namespace tabletop
