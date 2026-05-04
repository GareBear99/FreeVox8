#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    std::atomic<float>* raw(juce::AudioProcessorValueTreeState& state, const char* id)
    {
        return state.getRawParameterValue(id);
    }
}

FreeVox8AudioProcessor::FreeVox8AudioProcessor()
: AudioProcessor(BusesProperties()
    .withInput ("Input",     juce::AudioChannelSet::stereo(), true)
    .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)
    .withOutput("Output",    juce::AudioChannelSet::stereo(), true))
, apvts(*this, nullptr, "FreeVox8State", createParameterLayout())
{
}

void FreeVox8AudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    dsp.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());
}

void FreeVox8AudioProcessor::releaseResources()
{
    dsp.reset();
}

bool FreeVox8AudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto mainIn = layouts.getMainInputChannelSet();
    const auto mainOut = layouts.getMainOutputChannelSet();
    if (mainIn != mainOut) return false;
    if (mainOut != juce::AudioChannelSet::mono() && mainOut != juce::AudioChannelSet::stereo()) return false;

    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet(true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void FreeVox8AudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear(ch, 0, buffer.getNumSamples());

    FreeVox8DSP::Parameters p;
    p.mode = static_cast<FreeVox8DSP::Mode>(static_cast<int>(raw(apvts, "mode")->load()));
    p.mix = raw(apvts, "mix")->load();
    p.carrierBlend = raw(apvts, "carrier_blend")->load();
    p.maskDepth = raw(apvts, "mask_depth")->load();
    p.clarity = raw(apvts, "clarity")->load();
    p.formantShift = raw(apvts, "formant_shift")->load();
    p.ghost = raw(apvts, "ghost")->load();
    p.air = raw(apvts, "air")->load();
    p.freeze = raw(apvts, "freeze")->load();
    p.transientProtect = raw(apvts, "transient_protect")->load();
    p.stereoWidth = raw(apvts, "stereo_width")->load();
    p.quality = raw(apvts, "quality")->load();
    p.outputGainDb = raw(apvts, "output_gain")->load();
    p.engineMode = static_cast<int>(raw(apvts, "engine")->load());
    dsp.setParameters(p);

    const juce::AudioBuffer<float>* sidechain = nullptr;
    juce::AudioBuffer<float> sidechainView;
    if (getBusCount(true) > 1 && getChannelCountOfBus(true, 1) > 0)
    {
        // Important: this is a zero-allocation bus view. Do not resize scratch buffers
        // in processBlock; hosts may call this on the realtime audio thread.
        sidechainView = getBusBuffer(buffer, true, 1);
        sidechain = &sidechainView;
    }

    dsp.process(buffer, sidechain, midi);
}

juce::AudioProcessorEditor* FreeVox8AudioProcessor::createEditor()
{
    return new FreeVox8AudioProcessorEditor(*this);
}

void FreeVox8AudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void FreeVox8AudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessorValueTreeState::ParameterLayout FreeVox8AudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;
    params.push_back(std::make_unique<juce::AudioParameterChoice>("mode", "Mode", juce::StringArray { "Vocoder", "Mask", "Ghost", "Morph" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterChoice>("engine", "Engine", juce::StringArray { "Low Latency 128", "Spectral Ghost 1024" }, 0));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("mix", "Mix", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("carrier_blend", "Carrier Blend", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.65f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("mask_depth", "Mask Depth", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.50f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("clarity", "Clarity", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.55f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("formant_shift", "Formant Shift", juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("ghost", "Ghost", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("air", "Air", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.15f));
    params.push_back(std::make_unique<juce::AudioParameterBool>("freeze", "Freeze", false));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("transient_protect", "Transient Protect", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.35f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("stereo_width", "Stereo Width", juce::NormalisableRange<float>(0.0f, 2.0f, 0.001f), 1.0f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("quality", "Quality", juce::NormalisableRange<float>(0.0f, 1.0f, 0.001f), 0.75f));
    params.push_back(std::make_unique<juce::AudioParameterFloat>("output_gain", "Output Gain", juce::NormalisableRange<float>(-24.0f, 12.0f, 0.01f), 0.0f));
    return { params.begin(), params.end() };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FreeVox8AudioProcessor();
}
