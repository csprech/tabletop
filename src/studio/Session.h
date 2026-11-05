#pragma once

// What's on the table and how it's wired: the devices (each a node in one AudioProcessorGraph), the cables
// (each a graph connection, audio or MIDI), the computer's audio device and MIDI inputs, and saving it all.

#include "Registry.h"
#include <juce_audio_utils/juce_audio_utils.h>

namespace tabletop
{
class Session
{
public:
    Session();
    ~Session();

    struct Device
    {
        juce::String uid;
        const DeviceType* type = nullptr;
        juce::AudioProcessorGraph::NodeID node;
        juce::Point<float> pos; // top-left of the body on the table, mm (the jack strip is behind it)
    };
    /** A cable always runs from an output jack to an input jack. */
    struct Cable
    {
        juce::String from;
        int fromJack = 0;
        juce::String to;
        int toJack = 0;
        juce::Colour colour;
    };

    // ---- devices
    /** Puts a device on the table; returns its uid ("" if it can't: an unknown type, or a second interface). */
    juce::String addDevice (const juce::String& typeId, juce::Point<float> pos, const juce::String& uid = {});
    void removeDevice (const juce::String& uid);
    void moveDevice (const juce::String& uid, juce::Point<float> pos);
    void bringToFront (const juce::String& uid);
    const std::vector<Device>& getDevices() const { return devices; }
    const Device* findDevice (const juce::String& uid) const;
    juce::AudioProcessor* getProcessor (const juce::String& uid) const;
    bool hasDeviceOfType (const juce::String& typeId) const;

    // ---- cables
    /** Whether a cable could go between these two jacks (either order): an output and an input of the same
        kind, on different devices, neither already in use. */
    bool canConnect (const juce::String& a, int aJack, const juce::String& b, int bJack) const;
    bool connect (const juce::String& a, int aJack, const juce::String& b, int bJack, juce::Colour colour = {});
    void disconnect (int cableIndex);
    const std::vector<Cable>& getCables() const { return cables; }
    /** The cable plugged into a jack, or -1. */
    int findCable (const juce::String& uid, int jack) const;
    juce::Colour nextCableColour (bool midi);

    // ---- files
    void clear();
    void createDefaultTable();
    juce::var toJson() const;
    bool fromJson (const juce::var& json);

    juce::AudioDeviceManager& getDeviceManager() { return deviceManager; }
    /** Called after anything changes (devices, cables, positions). */
    std::function<void()> onChange;

private:
    void wire (const Cable& c, bool add);
    void refreshMonoInputs();
    void refreshAudioInput();
    void midiDevicesChanged();
    void changed()
    {
        if (onChange)
            onChange();
    }

    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorGraph graph;
    juce::AudioProcessorPlayer player;
    juce::AudioProcessorGraph::NodeID audioInNode, audioOutNode, midiInNode;
    std::vector<Device> devices;
    std::vector<Cable> cables;
    int colourIndex = 0;
    bool inputOpen = false;
    juce::Array<juce::MidiDeviceInfo> knownMidiInputs;
    juce::MidiDeviceListConnection midiDeviceListConnection;
};
} // namespace tabletop
