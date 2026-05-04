#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class FreeVox8AudioProcessor;

class SpectralDisplay final : public juce::Component, private juce::Timer
{
public:
    explicit SpectralDisplay(FreeVox8AudioProcessor& p) : processor(p) { startTimerHz(30); }
    void paint(juce::Graphics& g) override;
private:
    void timerCallback() override { repaint(); }
    FreeVox8AudioProcessor& processor;
};
