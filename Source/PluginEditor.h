#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "UI/SpectralDisplay.h"

class FreeVox8AudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit FreeVox8AudioProcessorEditor(FreeVox8AudioProcessor&);
    ~FreeVox8AudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    FreeVox8AudioProcessor& processor;
    SpectralDisplay display;

    juce::ComboBox modeBox, engineBox;
    juce::Slider mix, carrierBlend, maskDepth, clarity, formantShift, ghost, air, transientProtect, stereoWidth, quality, outputGain;
    juce::ToggleButton freeze;
    juce::Label title, subtitle, status;

    std::unique_ptr<ComboAttachment> modeAttachment, engineAttachment;
    std::unique_ptr<SliderAttachment> mixAttachment, carrierBlendAttachment, maskDepthAttachment, clarityAttachment, formantShiftAttachment, ghostAttachment, airAttachment, transientProtectAttachment, stereoWidthAttachment, qualityAttachment, outputGainAttachment;
    std::unique_ptr<ButtonAttachment> freezeAttachment;

    void setupSlider(juce::Slider& s, const juce::String& labelText);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FreeVox8AudioProcessorEditor)
};
