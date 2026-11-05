#pragma once

// The studio's own gear: the audio interface (the way out to your speakers, in from your computer's inputs and
// MIDI controllers) and a six-channel mixer.

#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>

namespace tabletop
{
/** The audio interface. Its channels:
      inputs 0, 1   SPEAKERS L, R (from cables)          -> outputs 0, 1 to the computer's speakers (VOLUME)
      inputs 2, 3   the computer's audio inputs 1, 2      -> outputs 2, 3 on its IN 1 / IN 2 jacks
      MIDI          MIDI from the computer's controllers  -> its MIDI jack
    With only SPEAKERS L cabled, the speakers both play it. */
class InterfaceProcessor : public juce::AudioProcessor
{
public:
    InterfaceProcessor();
    static constexpr int kPanelWidth = 960, kPanelHeight = 480;

    const juce::String getName() const override { return "Audio Interface"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported (const BusesLayout&) const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    std::atomic<float> volume { 0.8f };
    std::atomic<bool> speakersStereo { true };
    std::array<std::atomic<float>, 4> peaks {}; // out L, out R, in 1, in 2
    std::atomic<int> midiActivity { 0 };

private:
    float smoothed = 0.8f;
};

/** A six-channel stereo mixer: LEVEL fader, PAN and MUTE per channel, the MAIN fader. A channel with only its
    L input cabled is mono (it feeds both sides through PAN). */
class MixerProcessor : public juce::AudioProcessor
{
public:
    static constexpr int kChannels = 6;
    static constexpr int kPanelWidth = 1100, kPanelHeight = 800;
    MixerProcessor();

    const juce::String getName() const override { return "Mixer MX-6"; }
    void prepareToPlay (double, int maxBlock) override { scratch.setSize (2, juce::jmax (1, maxBlock)); }
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported (const BusesLayout&) const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    struct Channel
    {
        std::atomic<float> level { 0.75f }, pan { 0.0f }, peak { 0.0f };
        std::atomic<bool> mute { false }, stereo { true };
        float gainL = 0.0f, gainR = 0.0f; // smoothed (audio thread)
    };
    std::array<Channel, kChannels> channels;
    std::atomic<float> master { 0.8f };
    std::array<std::atomic<float>, 2> mainPeaks {};

private:
    juce::AudioBuffer<float> scratch;
    float masterSmoothed = 0.8f;
};
} // namespace tabletop
