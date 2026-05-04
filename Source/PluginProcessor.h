#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Config.h"
#include "DSP/FreeVox8DSP.h"

class FreeVox8AudioProcessor final : public juce::AudioProcessor
{
public:
    FreeVox8AudioProcessor();
    ~FreeVox8AudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return FREEVOX8_NAME; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    juce::AudioProcessorValueTreeState apvts;

    float getDisplayBin(int index) const noexcept { return dsp.getDisplayBin(index); }
    int getDisplayBinCount() const noexcept { return dsp.getDisplayBinCount(); }

private:
    FreeVox8DSP dsp;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FreeVox8AudioProcessor)
};
