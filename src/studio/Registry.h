#pragma once

// The devices Tabletop knows: what each is, how big it is on the table (life size, in millimetres), the jacks
// on its back panel and how they map onto its processor's channels, and how to make one.

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>
#include <vector>

namespace tabletop
{
struct JackSpec
{
    enum class Kind
    {
        AudioIn,
        AudioOut,
        MidiIn,
        MidiOut
    };
    juce::String label;
    Kind kind = Kind::AudioOut;
    int channel = 0; // the processor's audio channel (unused for MIDI)

    bool isInput() const { return kind == Kind::AudioIn || kind == Kind::MidiIn; }
    bool isMidi() const { return kind == Kind::MidiIn || kind == Kind::MidiOut; }
};

struct DeviceType
{
    juce::String id, name, blurb;
    float widthMm = 300.0f, depthMm = 200.0f; // the body's footprint; the jack strip adds kStripDepthMm behind it
    int panelWidth = 1000, panelHeight = 700;  // the editor's natural size (it's scaled to the footprint)
    juce::Colour rear { 0xff2a2c31 };          // the back panel's colour
    std::vector<JackSpec> jacks;
    std::function<std::unique_ptr<juce::AudioProcessor>()> create;
    bool unique = false; // only one per table (the audio interface)
};

/** The strip along the back of every device, where its jacks are. */
constexpr float kStripDepthMm = 26.0f;

const std::vector<DeviceType>& deviceTypes();
const DeviceType* findDeviceType (const juce::String& id);
} // namespace tabletop
