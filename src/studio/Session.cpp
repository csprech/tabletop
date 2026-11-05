#include "Session.h"
#include "BuiltIn.h"
#include "trackdesk/src/plugin/TrackdeskProcessor.h"

namespace tabletop
{
using Graph = juce::AudioProcessorGraph;

Session::Session()
{
    deviceManager.initialiseWithDefaultDevices (0, 2);
    graph.enableAllBuses();
    audioInNode = graph.addNode (std::make_unique<Graph::AudioGraphIOProcessor> (Graph::AudioGraphIOProcessor::audioInputNode))->nodeID;
    audioOutNode = graph.addNode (std::make_unique<Graph::AudioGraphIOProcessor> (Graph::AudioGraphIOProcessor::audioOutputNode))->nodeID;
    midiInNode = graph.addNode (std::make_unique<Graph::AudioGraphIOProcessor> (Graph::AudioGraphIOProcessor::midiInputNode))->nodeID;
    player.setProcessor (&graph);
    deviceManager.addAudioCallback (&player);
    // every MIDI controller plugged in now or later reaches the interface's MIDI jack
    deviceManager.addMidiInputDeviceCallback ({}, &player);
    knownMidiInputs = juce::MidiInput::getAvailableDevices();
    for (auto& d : knownMidiInputs)
        deviceManager.setMidiInputDeviceEnabled (d.identifier, true);
    midiDeviceListConnection = juce::MidiDeviceListConnection::make ([this] { midiDevicesChanged(); });
}

Session::~Session()
{
    midiDeviceListConnection.reset();
    deviceManager.removeMidiInputDeviceCallback ({}, &player);
    deviceManager.removeAudioCallback (&player);
    player.setProcessor (nullptr);
    graph.clear();
    deviceManager.closeAudioDevice();
}

void Session::midiDevicesChanged()
{
    const auto now = juce::MidiInput::getAvailableDevices();
    for (auto& d : now)
    {
        bool known = false;
        for (auto& k : knownMidiInputs)
            known = known || k.identifier == d.identifier;
        if (! known)
            deviceManager.setMidiInputDeviceEnabled (d.identifier, true);
    }
    knownMidiInputs = now;
}

// ---------------------------------------------------------------- devices

juce::String Session::addDevice (const juce::String& typeId, juce::Point<float> pos, const juce::String& wantedUid)
{
    const auto* type = findDeviceType (typeId);
    if (type == nullptr || (type->unique && hasDeviceOfType (typeId)))
        return {};
    auto processor = type->create();
    auto node = graph.addNode (std::move (processor));
    if (node == nullptr)
        return {};
    Device d;
    d.uid = wantedUid.isNotEmpty() ? wantedUid : juce::Uuid().toString();
    d.type = type;
    d.node = node->nodeID;
    d.pos = pos;
    devices.push_back (d);
    if (typeId == "interface")
    {
        // the interface is the way to the computer: its speakers out, the computer's inputs and controllers in
        graph.addConnection ({ { node->nodeID, 0 }, { audioOutNode, 0 } });
        graph.addConnection ({ { node->nodeID, 1 }, { audioOutNode, 1 } });
        graph.addConnection ({ { audioInNode, 0 }, { node->nodeID, 2 } });
        graph.addConnection ({ { audioInNode, 1 }, { node->nodeID, 3 } });
        graph.addConnection ({ { midiInNode, Graph::midiChannelIndex }, { node->nodeID, Graph::midiChannelIndex } });
    }
    refreshMonoInputs();
    changed();
    return d.uid;
}

void Session::removeDevice (const juce::String& uid)
{
    for (int i = (int) cables.size(); --i >= 0;)
        if (cables[(size_t) i].from == uid || cables[(size_t) i].to == uid)
            disconnect (i);
    for (auto it = devices.begin(); it != devices.end(); ++it)
        if (it->uid == uid)
        {
            graph.removeNode (it->node);
            devices.erase (it);
            break;
        }
    refreshAudioInput();
    changed();
}

void Session::moveDevice (const juce::String& uid, juce::Point<float> pos)
{
    for (auto& d : devices)
        if (d.uid == uid)
            d.pos = pos;
}

void Session::bringToFront (const juce::String& uid)
{
    for (auto it = devices.begin(); it != devices.end(); ++it)
        if (it->uid == uid)
        {
            auto d = *it;
            devices.erase (it);
            devices.push_back (d);
            return;
        }
}

const Session::Device* Session::findDevice (const juce::String& uid) const
{
    for (auto& d : devices)
        if (d.uid == uid)
            return &d;
    return nullptr;
}

juce::AudioProcessor* Session::getProcessor (const juce::String& uid) const
{
    if (const auto* d = findDevice (uid))
        if (auto node = graph.getNodeForId (d->node))
            return node->getProcessor();
    return nullptr;
}

bool Session::hasDeviceOfType (const juce::String& typeId) const
{
    for (auto& d : devices)
        if (d.type->id == typeId)
            return true;
    return false;
}

// ---------------------------------------------------------------- cables

int Session::findCable (const juce::String& uid, int jack) const
{
    for (size_t i = 0; i < cables.size(); ++i)
        if ((cables[i].from == uid && cables[i].fromJack == jack) || (cables[i].to == uid && cables[i].toJack == jack))
            return (int) i;
    return -1;
}

bool Session::canConnect (const juce::String& a, int aJack, const juce::String& b, int bJack) const
{
    const auto* da = findDevice (a);
    const auto* db = findDevice (b);
    if (da == nullptr || db == nullptr || a == b)
        return false;
    if (! juce::isPositiveAndBelow (aJack, (int) da->type->jacks.size()) || ! juce::isPositiveAndBelow (bJack, (int) db->type->jacks.size()))
        return false;
    const auto& ja = da->type->jacks[(size_t) aJack];
    const auto& jb = db->type->jacks[(size_t) bJack];
    return ja.isInput() != jb.isInput() && ja.isMidi() == jb.isMidi() && findCable (a, aJack) < 0 && findCable (b, bJack) < 0;
}

juce::Colour Session::nextCableColour (bool midi)
{
    if (midi)
        return juce::Colour (0xff1a1b1e);
    static const juce::uint32 palette[] = { 0xffd8342c, 0xfff2c12e, 0xff2f6fd6, 0xff3da55a, 0xffe8e6e0, 0xffe26b1f, 0xff8e4bd0 };
    return juce::Colour (palette[(size_t) (colourIndex++ % (int) std::size (palette))]);
}

bool Session::connect (const juce::String& a, int aJack, const juce::String& b, int bJack, juce::Colour colour)
{
    if (! canConnect (a, aJack, b, bJack))
        return false;
    Cable c;
    const bool aIsInput = findDevice (a)->type->jacks[(size_t) aJack].isInput();
    c.from = aIsInput ? b : a;
    c.fromJack = aIsInput ? bJack : aJack;
    c.to = aIsInput ? a : b;
    c.toJack = aIsInput ? aJack : bJack;
    c.colour = colour != juce::Colour() ? colour : nextCableColour (findDevice (a)->type->jacks[(size_t) aJack].isMidi());
    cables.push_back (c);
    wire (c, true);
    refreshMonoInputs();
    refreshAudioInput();
    changed();
    return true;
}

void Session::disconnect (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) cables.size()))
        return;
    wire (cables[(size_t) index], false);
    cables.erase (cables.begin() + index);
    refreshMonoInputs();
    refreshAudioInput();
    changed();
}

void Session::wire (const Cable& c, bool add)
{
    const auto* from = findDevice (c.from);
    const auto* to = findDevice (c.to);
    if (from == nullptr || to == nullptr)
        return;
    const auto& jf = from->type->jacks[(size_t) c.fromJack];
    const auto& jt = to->type->jacks[(size_t) c.toJack];
    const Graph::Connection conn { { from->node, jf.isMidi() ? Graph::midiChannelIndex : jf.channel },
                                   { to->node, jt.isMidi() ? Graph::midiChannelIndex : jt.channel } };
    if (add)
        graph.addConnection (conn);
    else
        graph.removeConnection (conn);
}

void Session::refreshMonoInputs()
{
    // a stereo input pair with only its left jack cabled is mono: the interface's speakers, the mixer's channels
    for (auto& d : devices)
    {
        auto* p = getProcessor (d.uid);
        auto pluggedChannel = [&] (int channel) {
            for (size_t j = 0; j < d.type->jacks.size(); ++j)
                if (d.type->jacks[j].kind == JackSpec::Kind::AudioIn && d.type->jacks[j].channel == channel)
                    return findCable (d.uid, (int) j) >= 0;
            return false;
        };
        if (auto* iface = dynamic_cast<InterfaceProcessor*> (p))
            iface->speakersStereo = pluggedChannel (1);
        else if (auto* mixer = dynamic_cast<MixerProcessor*> (p))
            for (int c = 0; c < MixerProcessor::kChannels; ++c)
                mixer->channels[(size_t) c].stereo = pluggedChannel (c * 2 + 1);
        else if (auto* desk = dynamic_cast<trackdesk::TrackdeskProcessor*> (p))
        {
            // the console hears only cabled inputs (a stereo channel with only L cabled is mono); a cable in AUX 2
            // takes the built-in effect away, as on the original
            for (int c = 0; c < trackdesk::TrackdeskProcessor::kNumInputs; ++c)
                desk->setInputPlugged (c, pluggedChannel (c));
            bool aux2 = false;
            for (size_t j = 0; j < d.type->jacks.size(); ++j)
                if (d.type->jacks[j].kind == JackSpec::Kind::AudioOut && d.type->jacks[j].channel == 5)
                    aux2 = findCable (d.uid, (int) j) >= 0;
            desk->setAux2Plugged (aux2);
        }
    }
}

void Session::refreshAudioInput()
{
    // the computer's audio input opens only once something is cabled to the interface's IN jacks (macOS asks)
    bool wanted = false;
    for (auto& c : cables)
        if (const auto* d = findDevice (c.from); d != nullptr && d->type->id == "interface" && d->type->jacks[(size_t) c.fromJack].kind == JackSpec::Kind::AudioOut)
            wanted = true;
    if (wanted == inputOpen)
        return;
    auto setup = deviceManager.getAudioDeviceSetup();
    setup.useDefaultInputChannels = false;
    setup.inputChannels.clear();
    if (wanted)
        setup.inputChannels.setRange (0, 2, true);
    deviceManager.setAudioDeviceSetup (setup, true);
    inputOpen = wanted;
}

// ---------------------------------------------------------------- files

void Session::clear()
{
    while (! devices.empty())
        removeDevice (devices.back().uid);
    colourIndex = 0;
}

void Session::createDefaultTable()
{
    clear();
    const auto iface = addDevice ("interface", { -560.0f, -330.0f });
    const auto mixer = addDevice ("mixer", { -250.0f, -400.0f });
    const auto vb = addDevice ("valvebox", { -880.0f, 40.0f });
    const auto pl = addDevice ("padlab", { -470.0f, 40.0f });
    const auto fp = addDevice ("fourpole", { 0.0f, -10.0f });
    auto jack = [this] (const juce::String& uid, const juce::String& label) {
        const auto* d = findDevice (uid);
        for (size_t i = 0; d != nullptr && i < d->type->jacks.size(); ++i)
            if (d->type->jacks[i].label == label)
                return (int) i;
        return -1;
    };
    connect (vb, jack (vb, "OUT L"), mixer, jack (mixer, "1 L"));
    connect (vb, jack (vb, "OUT R"), mixer, jack (mixer, "1 R"));
    connect (pl, jack (pl, "OUT L"), mixer, jack (mixer, "2 L"));
    connect (pl, jack (pl, "OUT R"), mixer, jack (mixer, "2 R"));
    connect (fp, jack (fp, "OUTPUT"), mixer, jack (mixer, "3 L"));
    connect (mixer, jack (mixer, "MAIN L"), iface, jack (iface, "SPEAKERS L"));
    connect (mixer, jack (mixer, "MAIN R"), iface, jack (iface, "SPEAKERS R"));
    connect (iface, jack (iface, "MIDI"), fp, jack (fp, "MIDI IN"));
}

juce::var Session::toJson() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("format", "tabletop-session");
    o->setProperty ("version", 1);
    juce::Array<juce::var> ds, cs;
    for (auto& d : devices)
    {
        auto* j = new juce::DynamicObject();
        j->setProperty ("uid", d.uid);
        j->setProperty ("type", d.type->id);
        j->setProperty ("x", d.pos.x);
        j->setProperty ("y", d.pos.y);
        if (auto* p = getProcessor (d.uid))
        {
            juce::MemoryBlock state;
            p->getStateInformation (state);
            j->setProperty ("state", state.toBase64Encoding());
        }
        ds.add (juce::var (j));
    }
    for (auto& c : cables)
    {
        auto* j = new juce::DynamicObject();
        j->setProperty ("from", c.from);
        j->setProperty ("fromJack", c.fromJack);
        j->setProperty ("to", c.to);
        j->setProperty ("toJack", c.toJack);
        j->setProperty ("colour", c.colour.toString());
        cs.add (juce::var (j));
    }
    o->setProperty ("devices", ds);
    o->setProperty ("cables", cs);
    return juce::var (o);
}

bool Session::fromJson (const juce::var& json)
{
    if (json.getProperty ("format", {}).toString() != "tabletop-session")
        return false;
    clear();
    if (auto* ds = json["devices"].getArray())
        for (auto& j : *ds)
        {
            const auto uid = addDevice (j["type"].toString(), { (float) (double) j["x"], (float) (double) j["y"] }, j["uid"].toString());
            if (uid.isEmpty())
                continue;
            juce::MemoryBlock state;
            if (state.fromBase64Encoding (j["state"].toString()) && state.getSize() > 0)
                if (auto* p = getProcessor (uid))
                    p->setStateInformation (state.getData(), (int) state.getSize());
        }
    if (auto* cs = json["cables"].getArray())
        for (auto& j : *cs)
            connect (j["from"].toString(), (int) j["fromJack"], j["to"].toString(), (int) j["toJack"], juce::Colour::fromString (j["colour"].toString()));
    changed();
    return true;
}
} // namespace tabletop
