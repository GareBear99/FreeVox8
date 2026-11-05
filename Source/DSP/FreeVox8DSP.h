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
// Band count is compile-time. The production plugin ships 128. The
// STOI reduced-band runs (paper §4.2 saturation question) rebuild this
// same code with FREEVOX8_NUM_BANDS=32/16, which re-spaces the log
// filterbank over the same 55 Hz–18.5 kHz range -- a true fewer-band
// vocoder, not an envelope grouping of the 128-band one. Default 128.
#ifndef FREEVOX8_NUM_BANDS
#define FREEVOX8_NUM_BANDS 128
#endif

class FreeVox8DSP
{
public:
    static constexpr int kNumBands = FREEVOX8_NUM_BANDS;
    static constexpr int kMaxChannels = 2;

    enum class Mode : int
    {
        Vocoder = 0,
        Mask = 1,
        Ghost = 2,
        // Morph = two-envelope morph in this (Low Latency 128) engine:
        // e = (1-alpha)*e1 + alpha*e2 per band, imposed on one carrier,
        // alpha = Ghost parameter (0 = main input, 1 = sidechain). The
        // Spectral Ghost 1024 engine's mode 3 is still the old blend
        // until the spectral morph lands there.
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
            for (auto& f : b.modLowpass)    f.prepare(spec);
            for (auto& f : b.modHighpass)   f.prepare(spec);
            for (auto& f : b.carLowpass)    f.prepare(spec);
            for (auto& f : b.carHighpass)   f.prepare(spec);
            for (auto& f : b.mod2Lowpass)   f.prepare(spec);
            for (auto& f : b.mod2Highpass)  f.prepare(spec);
            b.env.fill(0.0f);
            b.frozenEnv.fill(0.0f);
            b.env2.fill(0.0f);
            b.frozenEnv2.fill(0.0f);
        }

        buildFixedBandFilters();
        maskK = computeMaskK();
        spectral.prepare(sr, maxBlockSize, channels);
        reset();
    }

    void reset()
    {
        for (auto& b : bands)
        {
            for (auto& f : b.modLowpass)    f.reset();
            for (auto& f : b.modHighpass)   f.reset();
            for (auto& f : b.carLowpass)    f.reset();
            for (auto& f : b.carHighpass)   f.reset();
            for (auto& f : b.mod2Lowpass)   f.reset();
            for (auto& f : b.mod2Highpass)  f.reset();
            b.env.fill(0.0f);
            b.frozenEnv.fill(0.0f);
            b.env2.fill(0.0f);
            b.frozenEnv2.fill(0.0f);
        }

        for (auto& v : displayBins)
            v.store(0.0f, std::memory_order_relaxed);

        for (auto& v : maskDisplay)
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
        const bool isMask = (params.mode == Mode::Mask);

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
                // Mask mode ducks the dry signal itself, so the band
                // loop runs on dry, not the internal carrier: the old
                // broadband Mask never let the internal carrier reach
                // the output either, and this keeps that contract
                // (MIDI note is inert in Mask mode, as before).
                const float carrier = isMask ? dry : makeCarrierSample(midiHz, dry, ch);
                const float transient = juce::jlimit(0.0f, 1.0f, std::abs(dry - previousInput[static_cast<size_t>(ch)]) * 12.0f);
                previousInput[static_cast<size_t>(ch)] = dry;

                // Morph mode runs TWO modulator envelopes: e1 from the main
                // input, e2 from the sidechain. Every other mode analyses
                // the single modulator (sidechain if connected, else dry).
                if (params.mode == Mode::Morph && sc != nullptr)
                    analyseEnvelopeSample(ch, dry, sc[n], true, attackCoeff, releaseCoeff, freezeOn);
                else
                    analyseEnvelopeSample(ch, modulator, 0.0f, false, attackCoeff, releaseCoeff, freezeOn);

                float vocoded = 0.0f;
                float envelopeSum = 0.0f;
                float ducked = 0.0f;

                for (int bi = 0; bi < kNumBands; ++bi)
                {
                    auto& b = bands[static_cast<size_t>(bi)];
                    const float shiftedEnv = readShiftedEnvelope(ch, static_cast<float>(bi) - formantBins, freezeOn);
                    // Two-envelope morph: interpolate the envelopes per
                    // band, e = (1-alpha)*e1 + alpha*e2, and impose the
                    // result on the one carrier. Alpha is the Ghost
                    // parameter in Morph mode (0 = main input, 1 =
                    // sidechain). With no sidechain connected, e2 is the
                    // frozen snapshot -- live-vs-frozen morph when freeze
                    // is held, and (the frozen copy tracks the live
                    // envelope with a ~28 ms lag) effectively the vocoder
                    // when freeze is off. This replaces the old mode-3
                    // blend (0.58*vocoded + 0.42*tanh(dry + ...)), which
                    // mixed waveforms rather than interpolating spectra.
                    float effEnv = shiftedEnv;
                    if (params.mode == Mode::Morph)
                    {
                        const float alpha = juce::jlimit(0.0f, 1.0f, ghost);
                        const float env2 = (sc != nullptr)
                            ? readShiftedEnvelope2(ch, static_cast<float>(bi) - formantBins, freezeOn)
                            : readShiftedEnvelope(ch, static_cast<float>(bi) - formantBins, true);
                        effEnv = (1.0f - alpha) * shiftedEnv + alpha * env2;
                    }
                    const float bandCarrier = b.carHighpass[static_cast<size_t>(ch)].processSample(b.carLowpass[static_cast<size_t>(ch)].processSample(carrier));
                    // Mask mode: per-band ducking, same shape as the
                    // Ghost Mask (reduction = min(0.96, e * depth * 3.2)
                    // per band). The ducked slices are SUBTRACTED from
                    // dry instead of summing the kept slices: the
                    // filterbank sum is not a reconstruction (its
                    // transfer is +11..+19 dB with +/-66 deg phase, so
                    // a summed kept-signal would be loud and ripply at
                    // depth 0), while the subtractive form is bit-exact
                    // dry at depth 0 and never boosts: maskK is the
                    // no-boost bound of the bank's sum transfer, solved
                    // from the real coefficients in prepare() (0.9x the
                    // minimum over frequency of 2*Re(H)/|H|^2). No
                    // bandGain here: that is the vocoder's emphasis
                    // curve, and applying it to the subtraction would
                    // over-remove highs and under-remove lows.
                    const float reduction = isMask
                        ? juce::jlimit(0.0f, 0.96f, effEnv * maskDepth * 3.2f)
                        : 0.0f;
                    if (isMask)
                    {
                        ducked += bandCarrier * reduction * maskK;
                    }
                    else
                    {
                        const float consonantLift = 1.0f + air * highBandWeight(bi) * 1.6f;
                        const float shaped = bandCarrier * effEnv * consonantLift * (2.15f + clarity * 4.25f);
                        vocoded += shaped * bandGain(bi);
                        envelopeSum += effEnv;
                    }

                    // Per-band mask readout for the UI meter: the
                    // reduction actually applied in this band (channel
                    // 0 is the representative tap), smoothed like the
                    // spectrum display. In every other mode the target
                    // is 0, so the meter decays to empty instead of
                    // showing stale Mask data.
                    if (ch == 0)
                    {
                        const float previous = maskDisplay[static_cast<size_t>(bi)].load(std::memory_order_relaxed);
                        maskDisplay[static_cast<size_t>(bi)].store(previous * 0.86f + reduction * 0.14f,
                                                                   std::memory_order_relaxed);
                    }
                }

                const float transientReturn = dry * transientProtect * transient * 0.12f;

                float wet = 0.0f;
                if (isMask)
                {
                    // Per-band Mask: dry minus the ducked slices.
                    // Depth 0 is bit-exact dry (ducked sums to zero);
                    // the pocket lives in the ducked sum, not in a
                    // broadband gain, so a loud band cannot duck the
                    // whole carrier.
                    wet = dry - ducked + transientReturn;
                }
                else
                {
                    envelopeSum *= 1.0f / static_cast<float>(kNumBands);

                    const float breath = air * whiteNoise() * (0.010f + envelopeSum * 0.085f);
                    vocoded = vocoded * drive + breath + transientReturn;

                    wet = vocoded;
                    switch (params.mode)
                    {
                        case Mode::Vocoder:
                            break;

                        case Mode::Ghost:
                            wet = vocoded * (1.0f + ghost * 1.95f) + dry * ghost * 0.06f;
                            break;

                        // Morph: no separate formula -- the morph already
                        // happened in the envelope (two-envelope interpolation
                        // above), so mode 3 is the vocoded signal. The old
                        // blend formula is gone; see the band loop.
                        case Mode::Morph:
                            break;

                        case Mode::Mask:
                            break; // handled above, not a broadband gain
                    }
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

    // Per-band Mask readout for the UI meter: the reduction actually
    // applied in each band (0..0.96), same atomic-tap discipline as
    // getDisplayBin (audio thread writes, UI timer reads). Routes to
    // the Spectral Ghost engine when it is the active engine, exactly
    // like getDisplayBin. The UI rendering is a later step; this is
    // the data it reads.
    float getMaskBin(int index) const noexcept
    {
        index = juce::jlimit(0, kNumBands - 1, index);
        if (lastSpectralEngine.load(std::memory_order_relaxed))
            return spectral.getMaskBin(index);
        return maskDisplay[static_cast<size_t>(index)].load(std::memory_order_relaxed);
    }

    int getMaskBinCount() const noexcept { return kNumBands; }

    // Mean applied reduction across bands (0..0.96): the single number
    // a Mask meter needs next to the per-band array.
    float getMaskMeanReduction() const noexcept
    {
        if (lastSpectralEngine.load(std::memory_order_relaxed))
            return spectral.getMaskMeanReduction();
        float sum = 0.0f;
        for (const auto& v : maskDisplay)
            sum += v.load(std::memory_order_relaxed);
        return sum / static_cast<float>(kNumBands);
    }

    // Subtractive-Mask calibration, solved from the real filter
    // coefficients in prepare(). ~0.088 at 128 bands / 48 kHz.
    float getMaskK() const noexcept { return maskK; }

    // Effective per-band envelope actually imposed on the carrier, for
    // measurement and UI readout. In Morph mode this is the interpolated
    // (1-alpha)*e1 + alpha*e2 mix; in every other mode it is the single
    // modulator envelope. Read-only: it does not change DSP state.
    float getEffectiveEnvelope(int band, int channel = 0) const noexcept
    {
        const int bi = juce::jlimit(0, kNumBands - 1, band);
        const int ch = juce::jlimit(0, kMaxChannels - 1, channel);
        const auto& b = bands[static_cast<size_t>(bi)];
        const float e1 = b.env[static_cast<size_t>(ch)];
        if (params.mode != Mode::Morph)
            return e1;
        const float alpha = juce::jlimit(0.0f, 1.0f, params.ghost);
        return (1.0f - alpha) * e1 + alpha * b.env2[static_cast<size_t>(ch)];
    }


private:
    struct Band
    {
        // Per-channel mono IIR filters (not ProcessorDuplicator: the
        // duplicator has no processSample() in JUCE 7 or 8, and this engine
        // processes sample-by-sample, so one mono filter per channel is
        // the correct object. 128 bands x 2 paths x 2 filters = 512
        // instances per channel, as before.
        std::array<juce::dsp::IIR::Filter<float>, kMaxChannels> modLowpass;
        std::array<juce::dsp::IIR::Filter<float>, kMaxChannels> modHighpass;
        std::array<juce::dsp::IIR::Filter<float>, kMaxChannels> carLowpass;
        std::array<juce::dsp::IIR::Filter<float>, kMaxChannels> carHighpass;
        // Second modulator filterbank, for Morph mode only: the sidechain
        // needs its own filter states (running the same filter object on
        // two signals per sample would mix their states). A further 256
        // instances per channel, built once in prepare(), run only when
        // Morph mode has a sidechain connected.
        std::array<juce::dsp::IIR::Filter<float>, kMaxChannels> mod2Lowpass;
        std::array<juce::dsp::IIR::Filter<float>, kMaxChannels> mod2Highpass;
        std::array<float, kMaxChannels> env {};
        std::array<float, kMaxChannels> frozenEnv {};
        std::array<float, kMaxChannels> env2 {};
        std::array<float, kMaxChannels> frozenEnv2 {};
    };

    std::array<Band, kNumBands> bands;
    std::array<std::atomic<float>, kNumBands> displayBins {};
    // Per-band Mask readout for the UI meter: the reduction actually
    // applied in each band (0..0.96), written by the audio thread in
    // Mask mode, decayed to 0 in every other mode. Same atomic-tap
    // discipline as displayBins: audio thread writes, UI timer reads,
    // no locks. See getMaskBin().
    std::array<std::atomic<float>, kNumBands> maskDisplay {};
    FreeVox8SpectralEngine spectral;
    std::atomic<bool> lastSpectralEngine { false };
    Parameters params;
    double sr = 44100.0;
    // Subtractive-Mask calibration, solved from the real filter
    // coefficients in prepare() (see computeMaskK). Default is the
    // 128-band ballpark until prepare() runs.
    float maskK = 0.09f;
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
            for (int ch = 0; ch < kMaxChannels; ++ch)
            {
                *bands[static_cast<size_t>(i)].modHighpass[static_cast<size_t>(ch)].coefficients = *hp;
                *bands[static_cast<size_t>(i)].carHighpass[static_cast<size_t>(ch)].coefficients = *hp;
                *bands[static_cast<size_t>(i)].modLowpass[static_cast<size_t>(ch)].coefficients  = *lp;
                *bands[static_cast<size_t>(i)].carLowpass[static_cast<size_t>(ch)].coefficients  = *lp;
                *bands[static_cast<size_t>(i)].mod2Highpass[static_cast<size_t>(ch)].coefficients = *hp;
                *bands[static_cast<size_t>(i)].mod2Lowpass[static_cast<size_t>(ch)].coefficients  = *lp;
            }
        }
    }

    // Solve the subtractive Mask's calibration from the real carrier
    // filter coefficients: H(f) is the complex sum of all bandpass
    // responses. Subtracting k * (sum of ducked slices) from dry gives
    // wet(f) = (1 - k*H(f)) * dry(f) at full duck, so k must satisfy
    // |1 - k*H(f)| <= 1 at every frequency or the Mask boosts where it
    // should cut. That bound is k <= 2*Re(H)/|H|^2 wherever Re(H) > 0;
    // this returns 0.9x its minimum over a log probe grid (measured
    // 0.097 at 128 bands / 48 kHz, i.e. maskK ~0.088; the mean full-duck
    // pocket there is ~-5 dB with the worst frequency at 0 dB -- the
    // pocket depth is bounded by the bank's sum ripple, not by the
    // depth knob: a deeper pocket needs a reconstruction-flat bank).
    // Runs in prepare() only, never in process(). Uses JUCE's own
    // magnitude/phase response, so it tracks the real coefficients
    // at any sample rate or band count.
    float computeMaskK()
    {
        const int probes = 256;
        float kMax = 1.0f;
        bool any = false;
        for (int m = 0; m < probes; ++m)
        {
            const double t = static_cast<double>(m) / static_cast<double>(probes - 1);
            const double freq = 60.0 * std::pow(17000.0 / 60.0, t);
            double re = 0.0, im = 0.0;
            for (int i = 0; i < kNumBands; ++i)
            {
                const auto& b = bands[static_cast<size_t>(i)];
                const auto* hp = b.carHighpass[0].coefficients.get();
                const auto* lp = b.carLowpass[0].coefficients.get();
                const double mag = hp->getMagnitudeForFrequency(freq, sr)
                                 * lp->getMagnitudeForFrequency(freq, sr);
                const double ph = hp->getPhaseForFrequency(freq, sr)
                                + lp->getPhaseForFrequency(freq, sr);
                re += mag * std::cos(ph);
                im += mag * std::sin(ph);
            }
            const double mag2 = re * re + im * im;
            if (mag2 > 1e-12 && re > 0.0)
            {
                const float bound = static_cast<float>(2.0 * re / mag2);
                if (! any || bound < kMax)
                {
                    kMax = bound;
                    any = true;
                }
            }
        }
        if (! any)
            return 0.09f;
        return juce::jlimit(0.01f, 1.0f, 0.9f * kMax);
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
                               float modulator2,
                               bool hasModulator2,
                               float attackCoeff,
                               float releaseCoeff,
                               bool freezeOn)
    {
        for (int bi = 0; bi < kNumBands; ++bi)
        {
            auto& b = bands[static_cast<size_t>(bi)];
            const float bandMod = b.modHighpass[static_cast<size_t>(ch)].processSample(b.modLowpass[static_cast<size_t>(ch)].processSample(modulator));
            const float mag = std::sqrt(bandMod * bandMod + 1.0e-12f);
            auto& env = b.env[static_cast<size_t>(ch)];
            env = (mag > env) ? attackCoeff * env + (1.0f - attackCoeff) * mag
                              : releaseCoeff * env + (1.0f - releaseCoeff) * mag;

            if (! freezeOn)
                b.frozenEnv[static_cast<size_t>(ch)] = 0.9992f * b.frozenEnv[static_cast<size_t>(ch)] + 0.0008f * env;

            if (hasModulator2)
            {
                const float bandMod2 = b.mod2Highpass[static_cast<size_t>(ch)].processSample(b.mod2Lowpass[static_cast<size_t>(ch)].processSample(modulator2));
                const float mag2 = std::sqrt(bandMod2 * bandMod2 + 1.0e-12f);
                auto& env2 = b.env2[static_cast<size_t>(ch)];
                env2 = (mag2 > env2) ? attackCoeff * env2 + (1.0f - attackCoeff) * mag2
                                     : releaseCoeff * env2 + (1.0f - releaseCoeff) * mag2;

                if (! freezeOn)
                    b.frozenEnv2[static_cast<size_t>(ch)] = 0.9992f * b.frozenEnv2[static_cast<size_t>(ch)] + 0.0008f * env2;
            }

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

    float readShiftedEnvelope2(int ch, float bandPosition, bool freezeOn) const noexcept
    {
        const float clamped = juce::jlimit(0.0f, static_cast<float>(kNumBands - 1), bandPosition);
        const int lo = static_cast<int>(std::floor(clamped));
        const int hi = juce::jmin(kNumBands - 1, lo + 1);
        const float t = clamped - static_cast<float>(lo);
        const auto read = [this, ch, freezeOn](int idx) noexcept -> float
        {
            const auto& b = bands[static_cast<size_t>(idx)];
            return freezeOn ? b.frozenEnv2[static_cast<size_t>(ch)] : b.env2[static_cast<size_t>(ch)];
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
        // Level invariance across band counts: the vocoded sum adds N
        // band gains, so scale by 128/N -- at N=128 this is exactly 1.
        // Without it a 16-band render would be ~8x quieter than the
        // 128-band one and a band-count comparison would measure level,
        // not intelligibility. The STOI tool normalises level anyway;
        // this keeps the drive/tanh stage operating at the same level.
        return (0.038f + 0.040f * normalized) * (128.0f / static_cast<float>(kNumBands));
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
