#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include "FreeVox8SpectralEngine.h"

// FreeVox8DSP v0.4
// -----------------
// Production-oriented realtime MVP for the FreeVox8 spectral-vocoder family.
// This backend intentionally uses a fixed 128-band log filterbank instead of a
// dynamic FFT graph so the current source release remains deterministic,
// allocation-free in processBlock, and easy to validate in plugin hosts.
//
// Design law:
// - no heap allocation inside process()
// - no coefficient rebuilds inside process()
// - no locks, file I/O, background threads, or UI ownership in DSP
// - stable parameter/session contract for the later STFT/phase-vocoder backend
class FreeVox8DSP
{
public:
    static constexpr int kNumBands = 128;
    static constexpr int kMaxChannels = 2;

    enum class Mode : int
    {
        Vocoder = 0,
        Mask = 1,
        Ghost = 2,
        Morph = 3
    };

    struct Parameters
    {
        Mode mode = Mode::Vocoder;
        float mix = 1.0f;
        float carrierBlend = 0.65f;
        float maskDepth = 0.5f;
        float clarity = 0.55f;
        float formantShift = 0.0f; // semitones; RT-safe envelope remap
        float ghost = 0.0f;
        float air = 0.15f;
        float freeze = 0.0f;
        float transientProtect = 0.35f;
        float stereoWidth = 1.0f;
        float quality = 0.75f;
        float outputGainDb = 0.0f;
        int engineMode = 0;
    };

    void prepare(double sampleRate, int maxBlockSize, int numChannels)
    {
        sr = sampleRate > 0.0 ? sampleRate : 44100.0;
        channels = juce::jlimit(1, kMaxChannels, numChannels);

        juce::dsp::ProcessSpec spec {
            sr,
            static_cast<juce::uint32>(juce::jmax(1, maxBlockSize)),
            static_cast<juce::uint32>(channels)
        };

        for (auto& b : bands)
        {
            b.modLowpass.prepare(spec);
            b.modHighpass.prepare(spec);
            b.carLowpass.prepare(spec);
            b.carHighpass.prepare(spec);
            b.env.fill(0.0f);
            b.frozenEnv.fill(0.0f);
        }

        buildFixedBandFilters();
        spectral.prepare(sr, maxBlockSize, channels);
        reset();
    }

    void reset()
    {
        for (auto& b : bands)
        {
            b.modLowpass.reset();
            b.modHighpass.reset();
            b.carLowpass.reset();
            b.carHighpass.reset();
            b.env.fill(0.0f);
            b.frozenEnv.fill(0.0f);
        }

        for (auto& v : displayBins)
            v.store(0.0f, std::memory_order_relaxed);

        phase.fill(0.0);
        previousInput.fill(0.0f);
        currentMidiNote = 48;
        noiseState = 0x12345678u;
        spectral.reset();
        lastSpectralEngine.store(false, std::memory_order_relaxed);
    }

    void setParameters(const Parameters& p) noexcept
    {
        params = p;
    }

    void process(juce::AudioBuffer<float>& buffer,
                 const juce::AudioBuffer<float>* sidechain,
                 const juce::MidiBuffer& midi)
    {
        const int numSamples = buffer.getNumSamples();
        const int numCh = juce::jmin(channels, buffer.getNumChannels());
        if (numSamples <= 0 || numCh <= 0)
            return;

        currentMidiNote = extractLatestMidiNote(midi, currentMidiNote);
        const float midiHz = midiNoteToHz(currentMidiNote);

        if (params.engineMode > 0)
        {
            FreeVox8SpectralEngine::Parameters sp;
            sp.mix=params.mix; sp.maskDepth=params.maskDepth; sp.clarity=params.clarity; sp.formantShift=params.formantShift;
            sp.ghost=params.ghost; sp.air=params.air; sp.freeze=params.freeze; sp.transientProtect=params.transientProtect;
            sp.stereoWidth=params.stereoWidth; sp.outputGainDb=params.outputGainDb; sp.mode=static_cast<int>(params.mode);
            spectral.setMidiNote(currentMidiNote); spectral.setParameters(sp); spectral.process(buffer, sidechain);
            lastSpectralEngine.store(true, std::memory_order_relaxed);
            return;
        }
        lastSpectralEngine.store(false, std::memory_order_relaxed);

        const float wetMix = juce::jlimit(0.0f, 1.0f, params.mix);
        const float dryMix = 1.0f - wetMix;
        const float outGain = juce::Decibels::decibelsToGain(params.outputGainDb);
        const float clarity = juce::jlimit(0.0f, 1.0f, params.clarity);
        const float ghost = juce::jlimit(0.0f, 1.0f, params.ghost);
        const float maskDepth = juce::jlimit(0.0f, 1.0f, params.maskDepth);
        const float air = juce::jlimit(0.0f, 1.0f, params.air);
        const float transientProtect = juce::jlimit(0.0f, 1.0f, params.transientProtect);
        const float stereoWidth = juce::jlimit(0.0f, 2.0f, params.stereoWidth);
        const float quality = juce::jlimit(0.0f, 1.0f, params.quality);
        const bool freezeOn = params.freeze > 0.5f;

        const float attackMs = 0.75f + (1.0f - clarity) * 9.0f;
        const float releaseMs = 22.0f + (1.0f - clarity) * 220.0f;
        const float attackCoeff = coeffFromMs(attackMs);
        const float releaseCoeff = coeffFromMs(releaseMs);
        const float formantBins = params.formantShift / 12.0f * 8.0f; // 128-band musical remap
        const float drive = 1.10f + quality * 0.90f;

        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* x = buffer.getWritePointer(ch);
            const float* sc = (sidechain && sidechain->getNumChannels() > 0)
                ? sidechain->getReadPointer(juce::jmin(ch, sidechain->getNumChannels() - 1))
                : nullptr;

            for (int n = 0; n < numSamples; ++n)
            {
                const float dry = x[n];
                const float modulator = sc ? sc[n] : dry;
                const float carrier = makeCarrierSample(midiHz, dry, ch);
                const float transient = juce::jlimit(0.0f, 1.0f, std::abs(dry - previousInput[static_cast<size_t>(ch)]) * 12.0f);
                previousInput[static_cast<size_t>(ch)] = dry;

                analyseEnvelopeSample(ch, modulator, attackCoeff, releaseCoeff, freezeOn);

                float vocoded = 0.0f;
                float envelopeSum = 0.0f;

                for (int bi = 0; bi < kNumBands; ++bi)
                {
                    auto& b = bands[static_cast<size_t>(bi)];
                    const float shiftedEnv = readShiftedEnvelope(ch, static_cast<float>(bi) - formantBins, freezeOn);
                    const float bandCarrier = b.carHighpass.processSample(ch, b.carLowpass.processSample(ch, carrier));
                    const float consonantLift = 1.0f + air * highBandWeight(bi) * 1.6f;
                    const float shaped = bandCarrier * shiftedEnv * consonantLift * (2.15f + clarity * 4.25f);
                    vocoded += shaped * bandGain(bi);
                    envelopeSum += shiftedEnv;
                }

                envelopeSum *= 1.0f / static_cast<float>(kNumBands);

                const float breath = air * whiteNoise() * (0.010f + envelopeSum * 0.085f);
                const float transientReturn = dry * transientProtect * transient * 0.12f;
                vocoded = vocoded * drive + breath + transientReturn;

                float wet = vocoded;
                switch (params.mode)
                {
                    case Mode::Vocoder:
                        break;

                    case Mode::Mask:
                    {
                        const float reduction = juce::jlimit(0.0f, 0.97f, envelopeSum * maskDepth * (2.6f + quality));
                        wet = dry * (1.0f - reduction) + transientReturn;
                        break;
                    }

                    case Mode::Ghost:
                        wet = vocoded * (1.0f + ghost * 1.95f) + dry * ghost * 0.06f;
                        break;

                    case Mode::Morph:
                        wet = 0.58f * vocoded + 0.42f * fastSaturate(dry + vocoded * (0.28f + ghost * 0.92f));
                        break;
                }

                x[n] = fastSaturate((dry * dryMix + wet * wetMix) * outGain);
            }
        }

        applyStereoWidth(buffer, numSamples, numCh, stereoWidth);
    }

    float getDisplayBin(int index) const noexcept
    {
        index = juce::jlimit(0, kNumBands - 1, index);
        if (lastSpectralEngine.load(std::memory_order_relaxed))
            return spectral.getDisplayBin(index);
        return displayBins[static_cast<size_t>(index)].load(std::memory_order_relaxed);
    }

    int getDisplayBinCount() const noexcept { return kNumBands; }

private:
    struct Band
    {
        juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> modLowpass;
        juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> modHighpass;
        juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> carLowpass;
        juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> carHighpass;
        std::array<float, kMaxChannels> env {};
        std::array<float, kMaxChannels> frozenEnv {};
    };

    std::array<Band, kNumBands> bands;
    std::array<std::atomic<float>, kNumBands> displayBins {};
    FreeVox8SpectralEngine spectral;
    std::atomic<bool> lastSpectralEngine { false };
    Parameters params;
    double sr = 44100.0;
    int channels = kMaxChannels;
    int currentMidiNote = 48;
    std::array<double, kMaxChannels> phase { 0.0, 0.0 };
    std::array<float, kMaxChannels> previousInput { 0.0f, 0.0f };
    uint32_t noiseState = 0x12345678u;

    void buildFixedBandFilters()
    {
        const float nyquistSafe = static_cast<float>(sr * 0.47);
        for (int i = 0; i < kNumBands; ++i)
        {
            const float lo = makeBandEdge(i, nyquistSafe);
            const float hi = juce::jmax(lo + 18.0f, makeBandEdge(i + 1, nyquistSafe));
            auto hp = juce::dsp::IIR::Coefficients<float>::makeHighPass(sr, lo, 0.707f);
            auto lp = juce::dsp::IIR::Coefficients<float>::makeLowPass (sr, juce::jmin(hi, nyquistSafe), 0.707f);
            bands[static_cast<size_t>(i)].modHighpass.state = hp;
            bands[static_cast<size_t>(i)].carHighpass.state = hp;
            bands[static_cast<size_t>(i)].modLowpass.state  = lp;
            bands[static_cast<size_t>(i)].carLowpass.state  = lp;
        }
    }

    static float makeBandEdge(int index, float nyquistSafe) noexcept
    {
        const float minHz = 55.0f;
        const float maxHz = juce::jlimit(4000.0f, 18500.0f, nyquistSafe);
        const float t = static_cast<float>(index) / static_cast<float>(kNumBands);
        return minHz * std::pow(maxHz / minHz, t);
    }

    void analyseEnvelopeSample(int ch,
                               float modulator,
                               float attackCoeff,
                               float releaseCoeff,
                               bool freezeOn)
    {
        for (int bi = 0; bi < kNumBands; ++bi)
        {
            auto& b = bands[static_cast<size_t>(bi)];
            const float bandMod = b.modHighpass.processSample(ch, b.modLowpass.processSample(ch, modulator));
            const float mag = std::sqrt(bandMod * bandMod + 1.0e-12f);
            auto& env = b.env[static_cast<size_t>(ch)];
            env = (mag > env) ? attackCoeff * env + (1.0f - attackCoeff) * mag
                              : releaseCoeff * env + (1.0f - releaseCoeff) * mag;

            if (! freezeOn)
                b.frozenEnv[static_cast<size_t>(ch)] = 0.9992f * b.frozenEnv[static_cast<size_t>(ch)] + 0.0008f * env;

            const float visible = freezeOn ? b.frozenEnv[static_cast<size_t>(ch)] : env;
            const float previous = displayBins[static_cast<size_t>(bi)].load(std::memory_order_relaxed);
            const float smoothed = previous * 0.86f + juce::jlimit(0.0f, 1.0f, visible * 10.0f) * 0.14f;
            displayBins[static_cast<size_t>(bi)].store(smoothed, std::memory_order_relaxed);
        }
    }

    float readShiftedEnvelope(int ch, float bandPosition, bool freezeOn) const noexcept
    {
        const float clamped = juce::jlimit(0.0f, static_cast<float>(kNumBands - 1), bandPosition);
        const int lo = static_cast<int>(std::floor(clamped));
        const int hi = juce::jmin(kNumBands - 1, lo + 1);
        const float t = clamped - static_cast<float>(lo);
        const auto read = [this, ch, freezeOn](int idx) noexcept -> float
        {
            const auto& b = bands[static_cast<size_t>(idx)];
            return freezeOn ? b.frozenEnv[static_cast<size_t>(ch)] : b.env[static_cast<size_t>(ch)];
        };
        return read(lo) + (read(hi) - read(lo)) * t;
    }

    static int extractLatestMidiNote(const juce::MidiBuffer& midi, int fallback)
    {
        int note = fallback;
        for (const auto metadata : midi)
        {
            const auto msg = metadata.getMessage();
            if (msg.isNoteOn())
                note = msg.getNoteNumber();
        }
        return note;
    }

    static float midiNoteToHz(int note)
    {
        return 440.0f * std::pow(2.0f, (static_cast<float>(note) - 69.0f) / 12.0f);
    }

    float makeCarrierSample(float hz, float dry, int channel)
    {
        const double inc = juce::MathConstants<double>::twoPi * hz / sr;
        auto& ph = phase[static_cast<size_t>(juce::jlimit(0, kMaxChannels - 1, channel))];
        ph += inc;
        if (ph > juce::MathConstants<double>::twoPi)
            ph -= juce::MathConstants<double>::twoPi;

        const float saw = static_cast<float>((ph / juce::MathConstants<double>::pi) - 1.0);
        const float sine = static_cast<float>(std::sin(ph + channel * 0.17));
        const float pulse = sine >= 0.0f ? 0.55f : -0.55f;
        const float sub = static_cast<float>(std::sin(ph * 0.5 + channel * 0.11)) * 0.22f;
        const float internal = 0.44f * saw + 0.26f * sine + 0.18f * pulse + sub;
        const float blend = juce::jlimit(0.0f, 1.0f, params.carrierBlend);
        return internal * blend + dry * (1.0f - blend);
    }

    float whiteNoise() noexcept
    {
        noiseState = 1664525u * noiseState + 1013904223u;
        return (static_cast<float>((noiseState >> 9) & 0x7FFFFF) / 4194304.0f) - 1.0f;
    }

    float coeffFromMs(float ms) const noexcept
    {
        return std::exp(-1.0f / juce::jmax(1.0f, ms * 0.001f * static_cast<float>(sr)));
    }

    static float bandGain(int band) noexcept
    {
        const float normalized = static_cast<float>(band) / static_cast<float>(kNumBands - 1);
        return 0.038f + 0.040f * normalized;
    }

    static float highBandWeight(int band) noexcept
    {
        const float normalized = static_cast<float>(band) / static_cast<float>(kNumBands - 1);
        return normalized * normalized;
    }

    static float fastSaturate(float x) noexcept
    {
        return std::tanh(x);
    }

    static void applyStereoWidth(juce::AudioBuffer<float>& buffer, int numSamples, int numCh, float width) noexcept
    {
        if (numCh < 2 || std::abs(width - 1.0f) < 0.001f)
            return;
        auto* l = buffer.getWritePointer(0);
        auto* r = buffer.getWritePointer(1);
        for (int i = 0; i < numSamples; ++i)
        {
            const float mid = 0.5f * (l[i] + r[i]);
            const float side = 0.5f * (l[i] - r[i]) * width;
            l[i] = mid + side;
            r[i] = mid - side;
        }
    }
};
