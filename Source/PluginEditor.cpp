#include "PluginEditor.h"

void SpectralDisplay::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(12.0f);
    g.setColour(juce::Colour(0xff05060d));
    g.fillRoundedRectangle(r, 18.0f);

    // Proto-Synth inspired low-cost blueprint grid. It is visual only: no DSP state
    // is owned by the UI, and the timer only repaints the already-atomic bin taps.
    g.setColour(juce::Colour(0xff181a2c));
    for (float x = r.getX() + 18.0f; x < r.getRight(); x += 28.0f)
        g.drawVerticalLine(static_cast<int>(x), r.getY() + 8.0f, r.getBottom() - 8.0f);
    for (float y = r.getY() + 18.0f; y < r.getBottom(); y += 28.0f)
        g.drawHorizontalLine(static_cast<int>(y), r.getX() + 8.0f, r.getRight() - 8.0f);

    g.setColour(juce::Colour(0xff2f2a62));
    g.drawRoundedRectangle(r, 18.0f, 1.5f);

    const int bins = processor.getDisplayBinCount();
    const float gap = 2.25f;
    const float pad = 18.0f;
    const float w = juce::jmax(2.0f, (r.getWidth() - pad * 2.0f - gap * static_cast<float>(bins - 1)) / static_cast<float>(bins));
    const float h = r.getHeight() - pad * 2.0f;

    juce::Path contour;
    for (int i = 0; i < bins; ++i)
    {
        const float v = processor.getDisplayBin(i);
        const float x = r.getX() + pad + i * (w + gap);
        const float barH = juce::jlimit(2.0f, h, v * h);
        auto bar = juce::Rectangle<float>(x, r.getBottom() - pad - barH, w, barH);

        const auto hue = 0.56f + 0.22f * (static_cast<float>(i) / static_cast<float>(juce::jmax(1, bins - 1)));
        g.setColour(juce::Colour::fromHSV(hue, 0.72f, 0.45f + 0.48f * v, 0.90f));
        g.fillRoundedRectangle(bar, 3.25f);

        const float cx = bar.getCentreX();
        const float cy = bar.getY();
        if (i == 0) contour.startNewSubPath(cx, cy);
        else        contour.lineTo(cx, cy);
    }

    g.setColour(juce::Colour(0x99d7d2ff));
    g.strokePath(contour, juce::PathStrokeType(2.0f));

    g.setColour(juce::Colour(0xffeeeaff));
    g.setFont(13.5f);
    g.drawText("FreeVox8 spectral grid: 128-band RT-safe envelope monitor", r.toNearestInt().reduced(14), juce::Justification::topLeft);

    g.setColour(juce::Colour(0xff8e87bd));
    g.setFont(11.0f);
    g.drawText("Vocoder | Mask | Ghost | Morph", r.toNearestInt().reduced(14), juce::Justification::bottomRight);
}

FreeVox8AudioProcessorEditor::FreeVox8AudioProcessorEditor(FreeVox8AudioProcessor& p)
: AudioProcessorEditor(&p), processor(p), display(p)
{
    setSize(920, 610);

    title.setText("FreeVox8", juce::dontSendNotification);
    title.setFont(juce::Font(34.0f, juce::Font::bold));
    title.setColour(juce::Label::textColourId, juce::Colour(0xfff1efff));
    addAndMakeVisible(title);

    subtitle.setText("Spectral vocoder • ghost freeze • morph mask engine", juce::dontSendNotification);
    subtitle.setFont(juce::Font(15.0f));
    subtitle.setColour(juce::Label::textColourId, juce::Colour(0xffaaa2ff));
    addAndMakeVisible(subtitle);

    status.setText("v0.5.0 source release candidate • 128-band + Spectral Ghost 1024", juce::dontSendNotification);
    status.setFont(juce::Font(12.5f));
    status.setColour(juce::Label::textColourId, juce::Colour(0xff8e87bd));
    addAndMakeVisible(status);

    modeBox.addItemList(juce::StringArray { "Vocoder", "Mask", "Ghost", "Morph" }, 1);
    addAndMakeVisible(modeBox);
    modeAttachment = std::make_unique<ComboAttachment>(processor.apvts, "mode", modeBox);

    engineBox.addItemList(juce::StringArray { "Low Latency 128", "Spectral Ghost 1024" }, 1);
    addAndMakeVisible(engineBox);
    engineAttachment = std::make_unique<ComboAttachment>(processor.apvts, "engine", engineBox);

    setupSlider(mix, "Mix"); setupSlider(carrierBlend, "Carrier"); setupSlider(maskDepth, "Mask"); setupSlider(clarity, "Clarity");
    setupSlider(formantShift, "Formant"); setupSlider(ghost, "Ghost"); setupSlider(air, "Air"); setupSlider(transientProtect, "Transient");
    setupSlider(stereoWidth, "Width"); setupSlider(quality, "Quality"); setupSlider(outputGain, "Output");

    mixAttachment = std::make_unique<SliderAttachment>(processor.apvts, "mix", mix);
    carrierBlendAttachment = std::make_unique<SliderAttachment>(processor.apvts, "carrier_blend", carrierBlend);
    maskDepthAttachment = std::make_unique<SliderAttachment>(processor.apvts, "mask_depth", maskDepth);
    clarityAttachment = std::make_unique<SliderAttachment>(processor.apvts, "clarity", clarity);
    formantShiftAttachment = std::make_unique<SliderAttachment>(processor.apvts, "formant_shift", formantShift);
    ghostAttachment = std::make_unique<SliderAttachment>(processor.apvts, "ghost", ghost);
    airAttachment = std::make_unique<SliderAttachment>(processor.apvts, "air", air);
    transientProtectAttachment = std::make_unique<SliderAttachment>(processor.apvts, "transient_protect", transientProtect);
    stereoWidthAttachment = std::make_unique<SliderAttachment>(processor.apvts, "stereo_width", stereoWidth);
    qualityAttachment = std::make_unique<SliderAttachment>(processor.apvts, "quality", quality);
    outputGainAttachment = std::make_unique<SliderAttachment>(processor.apvts, "output_gain", outputGain);

    freeze.setButtonText("Freeze");
    freeze.setColour(juce::ToggleButton::textColourId, juce::Colour(0xfff1efff));
    addAndMakeVisible(freeze);
    freezeAttachment = std::make_unique<ButtonAttachment>(processor.apvts, "freeze", freeze);

    addAndMakeVisible(display);
}

void FreeVox8AudioProcessorEditor::setupSlider(juce::Slider& s, const juce::String& labelText)
{
    s.setName(labelText);
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 74, 20);
    s.setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff7d6bff));
    s.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff242139));
    s.setColour(juce::Slider::textBoxTextColourId, juce::Colour(0xfff1efff));
    addAndMakeVisible(s);
}

void FreeVox8AudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff05050b));
    auto bg = getLocalBounds().toFloat().reduced(14.0f);
    g.setColour(juce::Colour(0xff10101f));
    g.fillRoundedRectangle(bg, 22.0f);
    g.setColour(juce::Colour(0xff2b2550));
    g.drawRoundedRectangle(bg, 22.0f, 1.4f);

    g.setFont(12.0f);
    g.setColour(juce::Colour(0xff8e87bd));
    for (auto* s : { &mix, &carrierBlend, &maskDepth, &clarity, &formantShift, &ghost, &air, &transientProtect, &stereoWidth, &quality, &outputGain })
        g.drawText(s->getName(), s->getBounds().translated(0, -18), juce::Justification::centred);
}

void FreeVox8AudioProcessorEditor::resized()
{
    auto r = getLocalBounds().reduced(28);
    auto top = r.removeFromTop(82);
    title.setBounds(top.removeFromTop(38));
    subtitle.setBounds(top.removeFromTop(22));
    status.setBounds(top.removeFromTop(18));

    auto controlTop = r.removeFromTop(48);
    modeBox.setBounds(controlTop.removeFromLeft(170).reduced(4));
    engineBox.setBounds(controlTop.removeFromLeft(190).reduced(4));
    freeze.setBounds(controlTop.removeFromLeft(110).reduced(4));

    display.setBounds(r.removeFromTop(245).reduced(0, 4));

    auto row1 = r.removeFromTop(116);
    auto controls1 = { &mix, &carrierBlend, &maskDepth, &clarity, &formantShift, &ghost };
    const int width1 = row1.getWidth() / static_cast<int>(controls1.size());
    for (auto* s : controls1)
        s->setBounds(row1.removeFromLeft(width1).reduced(8, 22));

    auto row2 = r.removeFromTop(116);
    auto controls2 = { &air, &transientProtect, &stereoWidth, &quality, &outputGain };
    const int width2 = row2.getWidth() / static_cast<int>(controls2.size());
    for (auto* s : controls2)
        s->setBounds(row2.removeFromLeft(width2).reduced(8, 22));
}
