// FreeVox8 artefact taxonomy -- objective measurements (Rank 8).
//
// Builds against juce_audio_basics + juce_dsp only, headless on Linux.
// Run: FreeVox8_ArtefactTaxonomy
// Exit 0 = all checks passed. Every measurement prints its number.
//
// One named objective test per artefact in the paper §7 taxonomy. The
// subjective halves (word recognition, ABX preference, shifted-vowel
// identity by ear) need human ears and are NOT asserted here -- the
// listening materials are rendered by Tests/RenderListening.cpp into
// ~/workspace/vox8-bench/listening/ and listed in its LISTENING.md.
// Nothing in this file claims a listening result.
//
//   1. Phasiness (Ghost at large freeze holds vs filterbank): spectral
//      centroid / flatness / frame-to-frame flux over a 4 s frozen
//      hold. The Ghost engine's resynthesis is broken (paper §8 item
//      22), so its numbers are REPORT-ONLY and are labelled as
//      confounded by the envelope sweep, not as a working vocoder's.
//   2. Transient smearing / doubling: drum modulator + plosive burst
//      through the vocoder -- onset rise time (10-90%), pre-echo
//      (energy 20 ms before vs after the onset), doubling (extra
//      envelope peaks > 50% within 150 ms), vs the dry modulator.
//   3. Musical noise: Mask mode at high depth on sparse material --
//      per-band reduction flicker (std of getMaskBin over time in
//      quiet passages) and output spectral flicker (std of frame
//      band-energy in dB in quiet passages).
//   4. Consonant loss: synthetic fricatives /s/ /f/ /sh/ -- 2-4 kHz
//      and 4-8 kHz band-energy preservation (vocoded/dry, dB) at
//      air = 0 / 0.5 / 1.0, with a vowel-band control. Extends the
//      STOI per-band scores (2400 Hz 0.430, 3024 Hz 0.495).
//   5. Formant-shift artefacts: shifted-vowel identity -- measured
//      F1/F2 before/after +/-12 st shift (filterbank; Ghost
//      report-only), shift depth in semitones vs the intended 12,
//      F2/F1 ratio preservation.
//   6. Freeze blur / wobble: NOT re-run -- pilot + Bench cross-check
//      (correction gain 0.03 dB, smear 4.8-6.2 dB) are referenced.
//   7. Pumping: NOT re-run -- Step 7 MaskPumping (usable range EMPTY
//      at the 1 dB bar) is referenced.
//
// Hardware is printed with the numbers. All signals are synthesised
// in memory with a fixed seed, so runs are reproducible.

#include "../Source/DSP/FreeVox8DSP.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

namespace
{
int g_failures = 0;

void check(bool ok, const char* name)
{
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (! ok)
        ++g_failures;
}

std::uint32_t g_rng = 0x12345678u;
float randUni()
{
    g_rng = 1664525u * g_rng + 1013904223u;
    return static_cast<float>(((g_rng >> 9) & 0x7FFFFF) / 4194304.0) - 1.0f;
}

constexpr double kSr = 44100.0;
constexpr int kBs = 512;

void printHardware()
{
    std::FILE* f = std::fopen("/proc/cpuinfo", "r");
    std::string model = "unknown";
    if (f != nullptr)
    {
        char line[512];
        while (std::fgets(line, sizeof(line), f) != nullptr)
            if (std::strncmp(line, "model name", 10) == 0)
            {
                const char* colon = std::strchr(line, ':');
                if (colon != nullptr)
                {
                    model = colon + 2;
                    while (! model.empty() && (model.back() == '\n' || model.back() == ' '))
                        model.pop_back();
                }
                break;
            }
        std::fclose(f);
    }
    std::printf("hardware: %s | sample rate %.0f Hz\n", model.c_str(), kSr);
}

FreeVox8DSP::Parameters baseParams()
{
    FreeVox8DSP::Parameters p;
    p.mode = FreeVox8DSP::Mode::Vocoder;
    p.mix = 1.0f;
    p.carrierBlend = 1.0f; // pure internal carrier: output is carrier x envelope
    p.maskDepth = 0.5f;
    p.clarity = 0.55f;
    p.formantShift = 0.0f;
    p.ghost = 0.0f;
    p.air = 0.0f;
    p.freeze = 0.0f;
    p.transientProtect = 0.0f;
    p.stereoWidth = 1.0f;
    p.quality = 0.75f;
    p.outputGainDb = 0.0f;
    p.engineMode = 0;
    return p;
}

float bandFreq(int b)
{
    const float t = static_cast<float>(b) / 127.0f;
    return 55.0f * std::pow(18500.0f / 55.0f, t);
}

// Goertzel magnitude (dB) of a segment at one frequency, Hann window.
double goertzelDb(const std::vector<float>& x, size_t start, size_t n, double freq)
{
    const double w = 2.0 * 3.14159265358979 * freq / kSr;
    double re = 0.0, im = 0.0;
    for (size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double>(i) / (n > 1 ? n - 1 : 1);
        const double win = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 * t);
        const double a = w * static_cast<double>(i);
        const double v = x[start + i] * win;
        re += v * std::cos(a);
        im -= v * std::sin(a);
    }
    const double mag = std::sqrt(re * re + im * im) / n;
    return 20.0 * std::log10(mag + 1e-12);
}

// Band-integrated energy (linear) over [fLo, fHi] by a Goertzel grid.
double bandEnergy(const std::vector<float>& x, size_t start, size_t n,
                  double fLo, double fHi, double step)
{
    double sum = 0.0;
    for (double f = fLo; f <= fHi + 1e-9; f += step)
    {
        const double db = goertzelDb(x, start, n, f);
        const double mag = std::pow(10.0, db / 20.0);
        sum += mag * mag;
    }
    return sum;
}

// Stationary vowel: formant-weighted harmonic stack (same fixture as
// the morph test). vowel 0 = /a/ (F1 730, F2 1090), 1 = /i/
// (F1 270, F2 2290, F3 3000). Optional vibrato: rate Hz, depth st.
float vowelSample(double t, double f0, int vowel, double vibRate = 0.0, double vibDepthSt = 0.0)
{
    double f = f0;
    if (vibRate > 0.0)
        f = f0 * std::pow(2.0, vibDepthSt * std::sin(2.0 * 3.14159265358979 * vibRate * t) / 12.0);
    // Instantaneous phase by integrating the (slowly varying) frequency:
    // vibrato at 6 Hz over a 23 ms frame moves phase by < 1 cycle, so
    // evaluating the phase as f(t)*t is accurate to a fraction of a
    // cycle here; the fixture is a measurement signal, not a synth.
    double s = 0.0;
    for (int h = 1; h <= 20; ++h)
    {
        const double fh = f * h;
        if (fh > 8000.0)
            break;
        double formant = 0.0;
        if (vowel == 0)
            formant = 1.0 / (1.0 + std::pow((fh - 730.0) / 120.0, 2.0))
                    + 0.8 / (1.0 + std::pow((fh - 1090.0) / 140.0, 2.0)) + 0.05;
        else
            formant = 1.0 / (1.0 + std::pow((fh - 270.0) / 100.0, 2.0))
                    + 0.9 / (1.0 + std::pow((fh - 2290.0) / 250.0, 2.0))
                    + 0.5 / (1.0 + std::pow((fh - 3000.0) / 200.0, 2.0)) + 0.10;
        s += formant / (1.0 + 0.15 * h) * std::sin(2.0 * 3.14159265358979 * fh * t + 0.7 * h);
    }
    return static_cast<float>(s * 0.25);
}

// ---------------------------------------------------------------- phasiness
// Frozen hold: 1 s live /a/, then freeze on, hold 4 s. Per 256-sample
// frame in the hold (after 0.5 s settling): 128-band magnitudes,
// centroid, spectral flatness, frame-to-frame flux (L1 on normalised
// magnitudes), RMS. A phasiness-free frozen hold is spectrally still;
// a wandering spectrum is the measurable analogue here (the Ghost
// engine propagates no phase at all, so classical vertical-coherence
// phasiness does not apply to it -- see the confound note in the
// report).
void testPhasiness()
{
    std::printf("--- phasiness: 4 s frozen hold, /a/ F0 220, carrier MIDI 50 ---\n");
    for (int engine = 0; engine <= 1; ++engine)
    {
        for (int vib = 0; vib <= 1; ++vib)
        {
            FreeVox8DSP dsp;
            dsp.prepare(kSr, kBs, 2);
            auto p = baseParams();
            p.engineMode = engine;
            dsp.setParameters(p);
            const int total = static_cast<int>(5.0 * kSr);
            juce::AudioBuffer<float> buf(2, kBs);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 50, 0.9f), 0);
            std::vector<float> out;
            out.reserve(static_cast<size_t>(total));
            bool frozen = false;
            for (int start = 0; start < total; start += kBs)
            {
                if (! frozen && start >= static_cast<int>(1.0 * kSr))
                {
                    p.freeze = 1.0f;
                    dsp.setParameters(p);
                    frozen = true;
                }
                for (int ch = 0; ch < 2; ++ch)
                    for (int n = 0; n < kBs; ++n)
                    {
                        const double t = (start + n) / kSr;
                        buf.getWritePointer(ch)[n] = vowelSample(t, 220.0, 0,
                                                                 vib ? 6.0 : 0.0, vib ? 0.5 : 0.0);
                    }
                dsp.process(buf, nullptr, midi);
                for (int n = 0; n < kBs; ++n)
                    out.push_back(buf.getReadPointer(0)[n]);
            }
            // Hold frames: 1.5 s .. 5.0 s, 1024-sample frames, 256 hop.
            // 1024-sample frames (not 256): a 256-sample Goertzel
            // window beats against the carrier harmonics frame to
            // frame and drowns the hold's own wander in window noise.
            const int holdStart = static_cast<int>(1.5 * kSr);
            const int frameLen = 1024;
            const int hopLen = 256;
            const int nFrames = (static_cast<int>(out.size()) - holdStart - frameLen) / hopLen;
            std::vector<double> cents, flats, fluxes, rmss;
            std::vector<double> prevMag(128, 0.0);
            bool first = true;
            for (int fr = 0; fr < nFrames; ++fr)
            {
                const size_t s0 = static_cast<size_t>(holdStart + fr * hopLen);
                std::vector<double> mag(128, 0.0);
                double sum = 0.0;
                for (int b = 0; b < 128; ++b)
                {
                    const double db = goertzelDb(out, s0, frameLen, bandFreq(b));
                    mag[static_cast<size_t>(b)] = std::pow(10.0, db / 20.0);
                    sum += mag[static_cast<size_t>(b)];
                }
                double num = 0.0, den = 0.0, geo = 0.0;
                for (int b = 0; b < 128; ++b)
                {
                    const double m = mag[static_cast<size_t>(b)] / std::max(sum, 1e-18);
                    num += bandFreq(b) * m * m;
                    den += m * m;
                    geo += std::log(mag[static_cast<size_t>(b)] + 1e-12);
                }
                cents.push_back(den > 0.0 ? num / den : 0.0);
                const double arith = sum / 128.0;
                flats.push_back(std::exp(geo / 128.0) / std::max(arith, 1e-18));
                if (! first)
                {
                    double flux = 0.0;
                    for (int b = 0; b < 128; ++b)
                        flux += std::abs(mag[static_cast<size_t>(b)] / std::max(sum, 1e-18)
                                       - prevMag[static_cast<size_t>(b)]);
                    fluxes.push_back(flux);
                }
                first = false;
                for (int b = 0; b < 128; ++b)
                    prevMag[static_cast<size_t>(b)] = mag[static_cast<size_t>(b)] / std::max(sum, 1e-18);
                double e = 0.0;
                for (int n = 0; n < frameLen; ++n)
                {
                    const double v = out[s0 + static_cast<size_t>(n)];
                    e += v * v;
                }
                rmss.push_back(std::sqrt(e / frameLen));
            }
            auto meanStd = [](const std::vector<double>& v) {
                double m = 0.0;
                for (double x : v)
                    m += x;
                m /= static_cast<double>(v.size());
                double s = 0.0;
                for (double x : v)
                    s += (x - m) * (x - m);
                return std::make_pair(m, std::sqrt(s / v.size()));
            };
            const auto c = meanStd(cents), f = meanStd(flats), fl = meanStd(fluxes);
            const double rmsFirst = 20.0 * std::log10(rmss.front() + 1e-12);
            const double rmsLast = 20.0 * std::log10(rmss.back() + 1e-12);
            std::printf("  phasiness engine=%s %s: centroid %.0f +/- %.0f Hz | flatness %.4f +/- %.4f | flux %.4f +/- %.4f /frame | RMS first %.2f dB last %.2f dB (drift %+.2f dB)\n",
                        engine == 0 ? "filterbank" : "ghost   ",
                        vib ? "vibrato" : "static ",
                        c.first, c.second, f.first, f.second, fl.first, fl.second,
                        rmsFirst, rmsLast, rmsLast - rmsFirst);
            if (engine == 1)
                std::printf("    (ghost numbers are REPORT-ONLY: the FFT cross-synthesis resynthesis (2026-11-06) imposes a fresh envelope per 256-sample hop, so the frame-to-frame flux here is the STFT/OLA hop fluctuation, not a broken resynthesis -- the Ghost STOI is 0.7939, so the fluctuation does not destroy intelligibility)\n");
        }
    }
    check(true, "phasiness hold measured (see numbers above)");
}

// ------------------------------------------------- transient smearing
// 5 ms RMS envelope (1 ms hop). Per onset: rise time 10-90% of the
// peak in [onset, onset+150 ms]; pre-echo = raw-waveform energy in
// the 20 ms before the onset vs the 20 ms after (dB) -- measured on
// the samples, not the envelope, because a 5 ms RMS window straddles
// the onset and counts post-onset energy as pre-echo; doubling =
// extra envelope peaks > 50% of the main peak in (onset+10 ms,
// onset+150 ms], on a 21 ms moving-averaged envelope (a 5 ms RMS
// window ripples at the kick's own ~50-160 Hz rate and the raw peaks
// are the kick oscillating, not doubles) with a prominence rule (the
// valley between two counted peaks must dip below 70% of the smaller
// peak) and >= 20 ms separation. Reported as the median over onsets.
struct OnsetMetrics
{
    double riseMs = 0.0, preEchoDb = 0.0;
    int doubles = 0;
};

std::vector<double> rmsEnvelope(const std::vector<float>& x, int winSamples, int hopSamples)
{
    std::vector<double> env;
    for (size_t s = 0; s + static_cast<size_t>(winSamples) <= x.size(); s += static_cast<size_t>(hopSamples))
    {
        double e = 0.0;
        for (int n = 0; n < winSamples; ++n)
        {
            const double v = x[s + static_cast<size_t>(n)];
            e += v * v;
        }
        env.push_back(std::sqrt(e / winSamples));
    }
    return env;
}

std::vector<double> movingAverage(const std::vector<double>& x, int taps)
{
    // Trailing average: shifts peaks uniformly by ~taps/2, which the
    // relative doubling measurement does not depend on.
    std::vector<double> y(x.size(), 0.0);
    double sum = 0.0;
    for (size_t i = 0; i < x.size(); ++i)
    {
        sum += x[i];
        if (i >= static_cast<size_t>(taps))
            sum -= x[i - static_cast<size_t>(taps)];
        const int count = static_cast<int>(std::min<size_t>(i + 1, static_cast<size_t>(taps)));
        y[i] = sum / count;
    }
    return y;
}

OnsetMetrics onsetMetrics(const std::vector<double>& env, const std::vector<float>& raw,
                          double onsetSec, double hopSec, double sr)
{
    OnsetMetrics m;
    const int o = static_cast<int>(std::round(onsetSec / hopSec));
    const int post150 = static_cast<int>(std::round(0.150 / hopSec));
    double peak = 0.0;
    int peakIdx = o;
    for (int i = o; i < std::min(o + post150, static_cast<int>(env.size())); ++i)
        if (env[static_cast<size_t>(i)] > peak)
        {
            peak = env[static_cast<size_t>(i)];
            peakIdx = i;
        }
    if (peak <= 0.0)
        return m;
    const double lo = 0.10 * peak, hi = 0.90 * peak;
    int iLo = -1, iHi = -1;
    for (int i = o; i <= peakIdx; ++i)
    {
        if (iLo < 0 && env[static_cast<size_t>(i)] >= lo)
            iLo = i;
        if (iHi < 0 && env[static_cast<size_t>(i)] >= hi)
            iHi = i;
    }
    if (iLo >= 0 && iHi >= 0)
        m.riseMs = (iHi - iLo) * hopSec * 1000.0;
    // Pre-echo on the raw samples: no window straddle.
    const int onsetSamp = static_cast<int>(std::round(onsetSec * sr));
    const int pre20 = static_cast<int>(std::round(0.020 * sr));
    double ePre = 0.0, ePost = 0.0;
    for (int i = std::max(0, onsetSamp - pre20); i < onsetSamp; ++i)
        ePre += static_cast<double>(raw[static_cast<size_t>(i)]) * raw[static_cast<size_t>(i)];
    for (int i = onsetSamp; i < std::min(onsetSamp + pre20, static_cast<int>(raw.size())); ++i)
        ePost += static_cast<double>(raw[static_cast<size_t>(i)]) * raw[static_cast<size_t>(i)];
    m.preEchoDb = 10.0 * std::log10((ePre + 1e-18) / (ePost + 1e-18));
    // Doubling on the smoothed envelope, with prominence.
    const std::vector<double> envS = movingAverage(env, 21);
    const int sep = static_cast<int>(std::round(0.020 / hopSec));
    const int end = std::min(o + post150, static_cast<int>(envS.size()) - 1);
    int lastPeakIdx = peakIdx;
    double lastPeakVal = peak;
    int count = 0;
    for (int i = peakIdx + 1; i <= end; ++i)
    {
        const double v = envS[static_cast<size_t>(i)];
        if (v <= 0.5 * peak)
            continue;
        if (v < envS[static_cast<size_t>(i - 1)] || v < envS[static_cast<size_t>(i + 1)])
            continue; // not a local maximum
        if (i - lastPeakIdx < sep)
            continue;
        double valley = v;
        for (int j = lastPeakIdx; j <= i; ++j)
            valley = std::min(valley, envS[static_cast<size_t>(j)]);
        if (valley < 0.7 * std::min(v, lastPeakVal))
        {
            ++count;
            lastPeakIdx = i;
            lastPeakVal = v;
        }
    }
    m.doubles = count;
    return m;
}

void printOnsetStats(const char* label, const std::vector<OnsetMetrics>& ms)
{
    std::vector<double> rise, pre;
    std::vector<int> dbl;
    for (const auto& m : ms)
    {
        rise.push_back(m.riseMs);
        pre.push_back(m.preEchoDb);
        dbl.push_back(m.doubles);
    }
    std::sort(rise.begin(), rise.end());
    std::sort(pre.begin(), pre.end());
    std::sort(dbl.begin(), dbl.end());
    const size_t n = ms.size();
    std::printf("  transient %s: rise 10-90%% median %.1f ms (range %.1f-%.1f) | pre-echo median %+.1f dB (range %+.1f..%+.1f) | doubling median %d (max %d)\n",
                label, rise[n / 2], rise.front(), rise.back(),
                pre[n / 2], pre.front(), pre.back(), dbl[n / 2], dbl.back());
}

// Drum: 120 BPM kick (sine 160 -> 48 Hz over 120 ms, exp decay tau
// 90 ms, peak 0.9) + 3 ms white-noise click (peak 0.3) at each onset.
float drumSample(double t)
{
    const double beat = 0.5; // 120 BPM
    const double ph = std::fmod(t, beat);
    const int beatIdx = static_cast<int>(t / beat);
    if (beatIdx >= 8 || ph > 0.35)
        return 0.0f;
    const double f = 48.0 + 112.0 * std::exp(-ph / 0.04);
    const double kick = 0.9 * std::exp(-ph / 0.09) * std::sin(2.0 * 3.14159265358979 * f * ph);
    double click = 0.0;
    if (ph < 0.003)
    {
        // Deterministic pseudo-noise from the sample index.
        const std::uint32_t s = static_cast<std::uint32_t>(t * kSr) * 2654435761u;
        click = 0.3 * (static_cast<double>((s >> 9) & 0x7FFFFF) / 4194304.0 - 1.0);
    }
    return static_cast<float>(kick + click);
}

void testTransientSmearing()
{
    std::printf("--- transient smearing / doubling: drum modulator, 8 kicks @120 BPM ---\n");
    const int win = static_cast<int>(0.005 * kSr); // 5 ms RMS window
    const int hop = static_cast<int>(0.001 * kSr); // 1 ms hop
    const double hopSec = hop / kSr;
    std::vector<double> onsets;
    for (int k = 0; k < 8; ++k)
        onsets.push_back(k * 0.5); // kicks at 0.0, 0.5, ..., 3.5 s (beatIdx 0..7); there is no kick at 4.0 s (beatIdx 8)
    // Dry reference.
    {
        std::vector<float> dry;
        for (int i = 0; i < static_cast<int>(4.5 * kSr); ++i)
            dry.push_back(drumSample(i / kSr));
        const auto env = rmsEnvelope(dry, win, hop);
        std::vector<OnsetMetrics> ms;
        for (double o : onsets)
            ms.push_back(onsetMetrics(env, dry, o, hopSec, kSr));
        printOnsetStats("dry drum (reference)", ms);
    }
    for (int engine = 0; engine <= 1; ++engine)
    {
        for (float clarity : { 0.55f, 1.0f })
        {
            FreeVox8DSP dsp;
            dsp.prepare(kSr, kBs, 2);
            auto p = baseParams();
            p.engineMode = engine;
            p.clarity = clarity;
            dsp.setParameters(p);
            const int total = static_cast<int>(4.5 * kSr);
            juce::AudioBuffer<float> buf(2, kBs);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 50, 0.9f), 0);
            std::vector<float> out;
            out.reserve(static_cast<size_t>(total));
            for (int start = 0; start < total; start += kBs)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int n = 0; n < kBs; ++n)
                        buf.getWritePointer(ch)[n] = drumSample((start + n) / kSr);
                dsp.process(buf, nullptr, midi);
                for (int n = 0; n < kBs; ++n)
                    out.push_back(buf.getReadPointer(0)[n]);
            }
            const auto env = rmsEnvelope(out, win, hop);
            std::vector<OnsetMetrics> ms;
            for (double o : onsets)
                ms.push_back(onsetMetrics(env, out, o, hopSec, kSr));
            char label[96];
            std::snprintf(label, sizeof(label), "engine=%s clarity=%.2f",
                          engine == 0 ? "filterbank" : "ghost(report-only)", clarity);
            printOnsetStats(label, ms);
            if (engine == 0)
            {
                std::vector<double> rises;
                for (const auto& m : ms)
                    rises.push_back(m.riseMs);
                std::sort(rises.begin(), rises.end());
                check(rises[rises.size() / 2] < 50.0, "transient: filterbank median rise < 50 ms (sanity)");
            }
        }
    }
    // Plosive: 10 ms broadband burst then /a/ vowel, through the
    // filterbank. The burst-to-vowel ratio is the plosive audibility
    // check: a smeared plosive loses its burst against the vowel.
    std::printf("--- transient smearing: plosive /t/ (10 ms burst + /a/) ---\n");
    {
        auto plosiveGen = [](double t) -> float {
            if (t < 0.5)
                return 0.0f;
            if (t < 0.51)
            {
                const std::uint32_t s = static_cast<std::uint32_t>(t * kSr) * 2654435761u;
                return static_cast<float>(0.8 * (static_cast<double>((s >> 9) & 0x7FFFFF) / 4194304.0 - 1.0));
            }
            return vowelSample(t, 220.0, 0);
        };
        std::vector<float> dry;
        for (int i = 0; i < static_cast<int>(2.0 * kSr); ++i)
            dry.push_back(plosiveGen(i / kSr));
        auto ratio = [](const std::vector<float>& x) {
            double eBurst = 0.0, eVowel = 0.0;
            for (int i = static_cast<int>(0.5 * kSr); i < static_cast<int>(0.51 * kSr); ++i)
                eBurst += x[static_cast<size_t>(i)] * x[static_cast<size_t>(i)];
            for (int i = static_cast<int>(0.6 * kSr); i < static_cast<int>(0.8 * kSr); ++i)
                eVowel += x[static_cast<size_t>(i)] * x[static_cast<size_t>(i)];
            return 10.0 * std::log10((eBurst / 0.01 + 1e-18) / (eVowel / 0.2 + 1e-18));
        };
        const double dryRatio = ratio(dry);
        for (float clarity : { 0.55f, 1.0f })
        {
            FreeVox8DSP dsp;
            dsp.prepare(kSr, kBs, 2);
            auto p = baseParams();
            p.clarity = clarity;
            dsp.setParameters(p);
            const int total = static_cast<int>(2.0 * kSr);
            juce::AudioBuffer<float> buf(2, kBs);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 50, 0.9f), 0);
            std::vector<float> out;
            out.reserve(static_cast<size_t>(total));
            for (int start = 0; start < total; start += kBs)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int n = 0; n < kBs; ++n)
                        buf.getWritePointer(ch)[n] = plosiveGen((start + n) / kSr);
                dsp.process(buf, nullptr, midi);
                for (int n = 0; n < kBs; ++n)
                    out.push_back(buf.getReadPointer(0)[n]);
            }
            std::printf("  plosive /t/: burst-to-vowel ratio dry %+.1f dB | filterbank clarity=%.2f %+.1f dB (loss %+.1f dB)\n",
                        dryRatio, clarity, ratio(out), ratio(out) - dryRatio);
        }
    }
}

// ------------------------------------------------------ musical noise
// Sparse material: 200 ms /a/ bursts every 800 ms, 8 s, as the
// sidechain modulator; dry input is a steady saw chord (the signal
// Mask ducks). Quiet passages are the 400 ms windows starting 300 ms
// after each burst ends. Metrics: per-band reduction flicker = std
// of getMaskBin over the quiet windows (mean over bands, max band);
// output spectral flicker = std of the frame band-energy (dB) at 8
// probe frequencies over the same windows. Depth 0 is the control
// (flicker must be ~0). The real-speech sparse case (an ARCTIC
// sentence as the sidechain modulator) is rendered for the listening
// set by Tests/RenderListening.cpp, not measured here.
float sawChord(double t)
{
    // Sawtooth sum, 110 + 164.81 + 220 Hz, 0.22 each.
    double v = 0.0;
    for (double f : { 110.0, 164.81, 220.0 })
    {
        const double ph = std::fmod(f * t, 1.0);
        v += 0.22 * (2.0 * ph - 1.0);
    }
    return static_cast<float>(v);
}

void testMusicalNoise()
{
    std::printf("--- musical noise: Mask mode, sparse bursts, depth 0.8 / 0.3 / 0 ---\n");
    const double burstLen = 0.2, period = 0.8, totalSec = 8.0;
    for (float depth : { 0.8f, 0.3f, 0.0f })
    {
        FreeVox8DSP dsp;
        dsp.prepare(kSr, kBs, 2);
        auto p = baseParams();
        p.mode = FreeVox8DSP::Mode::Mask;
        p.maskDepth = depth;
        p.outputGainDb = -12.0f;
        dsp.setParameters(p);
        const int total = static_cast<int>(totalSec * kSr);
        juce::AudioBuffer<float> buf(2, kBs), sc(2, kBs);
        juce::MidiBuffer midi;
        std::vector<float> out;
        out.reserve(static_cast<size_t>(total));
        std::vector<std::array<float, 128>> maskRows;
        for (int start = 0; start < total; start += kBs)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int n = 0; n < kBs; ++n)
                {
                    const double t = (start + n) / kSr;
                    buf.getWritePointer(ch)[n] = sawChord(t);
                    const double ph = std::fmod(t, period);
                    sc.getWritePointer(ch)[n] = (ph < burstLen)
                        ? vowelSample(t, 220.0, 0) * 0.5f : 0.0f;
                }
            dsp.process(buf, &sc, midi);
            {
                std::array<float, 128> row {};
                for (int b = 0; b < 128; ++b)
                    row[static_cast<size_t>(b)] = dsp.getMaskBin(b);
                maskRows.push_back(row);
            }
            for (int n = 0; n < kBs; ++n)
                out.push_back(buf.getReadPointer(0)[n]);
        }
        // Quiet windows: [burstEnd + 0.3 s, burstEnd + 0.7 s] per period.
        // Mask rows are one per 512-sample block (11.6 ms).
        const double blockSec = kBs / kSr;
        std::vector<int> quietBlocks;
        for (size_t r = 0; r < maskRows.size(); ++r)
        {
            const double t = r * blockSec;
            const double ph = std::fmod(t, period);
            if (ph >= burstLen + 0.3 && ph < burstLen + 0.7)
                quietBlocks.push_back(static_cast<int>(r));
        }
        // Per-band reduction flicker over the quiet blocks.
        double meanStd = 0.0, maxStd = 0.0;
        for (int b = 0; b < 128; ++b)
        {
            double m = 0.0;
            for (int r : quietBlocks)
                m += maskRows[static_cast<size_t>(r)][static_cast<size_t>(b)];
            m /= static_cast<double>(quietBlocks.size());
            double s = 0.0;
            for (int r : quietBlocks)
            {
                const double d = maskRows[static_cast<size_t>(r)][static_cast<size_t>(b)] - m;
                s += d * d;
            }
            const double sd = std::sqrt(s / quietBlocks.size());
            meanStd += sd;
            maxStd = std::max(maxStd, sd);
        }
        meanStd /= 128.0;
        // Output spectral flicker: 256-sample frames in the quiet
        // windows, band-energy std (dB) at 8 probe frequencies.
        double flickSum = 0.0;
        const double probes[8] = { 120, 300, 730, 1090, 2000, 3000, 5000, 8000 };
        for (double pf : probes)
        {
            std::vector<double> vals;
            for (int k = 0; k < 10; ++k) // 10 burst periods
            {
                const double w0 = k * period + burstLen + 0.3;
                for (int fr = 0; fr < 4; ++fr) // 4 frames of 100 ms
                {
                    const size_t s0 = static_cast<size_t>((w0 + fr * 0.1) * kSr);
                    if (s0 + 4410 <= out.size())
                        vals.push_back(goertzelDb(out, s0, 4410, pf));
                }
            }
            double m = 0.0;
            for (double v : vals)
                m += v;
            m /= static_cast<double>(vals.size());
            double s = 0.0;
            for (double v : vals)
                s += (v - m) * (v - m);
            flickSum += std::sqrt(s / vals.size());
        }
        std::printf("  musical noise depth=%.1f: mask-bin flicker in quiet (std of reduction) mean %.4f max %.4f | output spectral flicker mean %.2f dB (8 probes)\n",
                    depth, meanStd, maxStd, flickSum / 8.0);
        if (depth == 0.0f)
            check(meanStd < 0.001,
                  "musical noise: depth-0 control is still (mask-bin flicker ~0 without Mask)");
    }
}

// ------------------------------------------------------ consonant loss
// Synthetic fricatives: white noise through 2nd-order Butterworth
// HP/LP (juce IIR), 0.4 s, 30 ms attack / 200 ms decay envelope.
// /s/ 4-8 kHz, /f/ 1.5-5 kHz, /sh/ 2.5-6.5 kHz. Measure band-energy
// preservation (vocoded/dry, dB) in 2-4 kHz and 4-8 kHz over the
// steady middle 0.2 s, at air = 0 / 0.5 / 1.0. Vowel-band control:
// /a/ vowel, 0.4-1.2 kHz.
std::vector<float> fricative(double fLo, double fHi, double seconds)
{
    juce::dsp::IIR::Filter<float> hp, lp;
    juce::dsp::ProcessSpec spec { kSr, 512, 1 };
    hp.prepare(spec);
    lp.prepare(spec);
    *hp.coefficients = *juce::dsp::IIR::Coefficients<float>::makeHighPass(kSr, static_cast<float>(fLo), 0.707f);
    *lp.coefficients = *juce::dsp::IIR::Coefficients<float>::makeLowPass(kSr, static_cast<float>(fHi), 0.707f);
    const int n = static_cast<int>(seconds * kSr);
    std::vector<float> x;
    x.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
    {
        const double t = i / kSr;
        double env = 1.0;
        if (t < 0.03)
            env = t / 0.03;
        else if (t > 0.2)
            env = std::max(0.0, 1.0 - (t - 0.2) / 0.2);
        x.push_back(static_cast<float>(lp.processSample(hp.processSample(randUni() * 0.7f)) * env));
    }
    return x;
}

void testConsonantLoss()
{
    std::printf("--- consonant loss: fricatives /s/ /f/ /sh/ + vowel control ---\n");
    struct Fric { const char* name; double lo, hi; };
    const Fric frics[3] = { { "/s/", 4000, 8000 }, { "/f/", 1500, 5000 }, { "/sh/", 2500, 6500 } };
    for (const auto& fr : frics)
    {
        const std::vector<float> dry = fricative(fr.lo, fr.hi, 0.4);
        const size_t segStart = static_cast<size_t>(0.1 * kSr);
        const size_t segN = static_cast<size_t>(0.2 * kSr);
        const double dry24 = bandEnergy(dry, segStart, segN, 2000, 4000, 200);
        const double dry48 = bandEnergy(dry, segStart, segN, 4000, 8000, 200);
        for (float air : { 0.0f, 0.5f, 1.0f })
        {
            FreeVox8DSP dsp;
            dsp.prepare(kSr, kBs, 2);
            auto p = baseParams();
            p.air = air;
            dsp.setParameters(p);
            const int total = static_cast<int>(0.4 * kSr);
            juce::AudioBuffer<float> buf(2, kBs);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
            std::vector<float> out;
            out.reserve(static_cast<size_t>(total));
            for (int start = 0; start < total; start += kBs)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int n = 0; n < kBs; ++n)
                        buf.getWritePointer(ch)[n] = dry[static_cast<size_t>(start + n)];
                dsp.process(buf, nullptr, midi);
                for (int n = 0; n < kBs; ++n)
                    out.push_back(buf.getReadPointer(0)[n]);
            }
            const double o24 = bandEnergy(out, segStart, segN, 2000, 4000, 200);
            const double o48 = bandEnergy(out, segStart, segN, 4000, 8000, 200);
            std::printf("  consonant %s air=%.1f: 2-4 kHz preservation %+.1f dB | 4-8 kHz %+.1f dB\n",
                        fr.name, air,
                        10.0 * std::log10((o24 + 1e-18) / (dry24 + 1e-18)),
                        10.0 * std::log10((o48 + 1e-18) / (dry48 + 1e-18)));
        }
    }
    // Vowel-band control: /a/ vowel, 0.4-1.2 kHz, air 0. Measured as
    // the sum of Goertzel energies at the signals' OWN harmonic
    // frequencies (dry: 220 Hz harmonics 440/660/880/1100; output:
    // 261.63 Hz carrier harmonics 523/785/1047) -- a fixed frequency
    // grid is invalid here, because the dry and the vocoded output
    // peak at different harmonics and the grid then measures harmonic
    // misalignment, not preservation (the first version of this
    // control read -45 dB for exactly that reason). The fricative
    // measurements above are unaffected: fricatives are noise, with
    // no harmonic structure for a grid to miss.
    {
        std::vector<float> dry;
        for (int i = 0; i < static_cast<int>(1.0 * kSr); ++i)
            dry.push_back(vowelSample(i / kSr, 220.0, 0));
        const size_t segStart = static_cast<size_t>(0.5 * kSr);
        const size_t segN = static_cast<size_t>(0.4 * kSr);
        auto harmEnergy = [&](const std::vector<float>& x, const std::vector<double>& harms) {
            double sum = 0.0;
            for (double f : harms)
            {
                const double db = goertzelDb(x, segStart, segN, f);
                const double mag = std::pow(10.0, db / 20.0);
                sum += mag * mag;
            }
            return sum;
        };
        const double dryV = harmEnergy(dry, { 440, 660, 880, 1100 });
        FreeVox8DSP dsp;
        dsp.prepare(kSr, kBs, 2);
        auto p = baseParams();
        dsp.setParameters(p);
        const int total = static_cast<int>(1.0 * kSr);
        juce::AudioBuffer<float> buf(2, kBs);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
        std::vector<float> out;
        out.reserve(static_cast<size_t>(total));
        for (int start = 0; start < total; start += kBs)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int n = 0; n < kBs; ++n)
                    buf.getWritePointer(ch)[n] = dry[static_cast<size_t>(start + n)];
            dsp.process(buf, nullptr, midi);
            for (int n = 0; n < kBs; ++n)
                out.push_back(buf.getReadPointer(0)[n]);
        }
        const double oV = harmEnergy(out, { 523.25, 784.88, 1046.5 });
        std::printf("  consonant vowel control /a/ air=0: 0.4-1.2 kHz harmonic-energy ratio %+.1f dB (level note only: the carrier's harmonic falloff dominates this number -- the fricative band-energy ratios above are the valid preservation measurement; the vowel-band evidence is the STOI per-band scores, 0.68-0.72 at 476-1200 Hz)\n",
                    10.0 * std::log10((oV + 1e-18) / (dryV + 1e-18)));
    }
    check(true, "consonant loss measured (see numbers above)");
}

// Single-formant vowel: one Lorentzian formant on a harmonic stack.
float singleFormantSample(double t, double f0, double fc, double width)
{
    double s = 0.0;
    for (int h = 1; h <= 20; ++h)
    {
        const double fh = f0 * h;
        if (fh > 8000.0)
            break;
        const double formant = 1.0 / (1.0 + std::pow((fh - fc) / width, 2.0)) + 0.05;
        s += formant / (1.0 + 0.15 * h) * std::sin(2.0 * 3.14159265358979 * fh * t + 0.7 * h);
    }
    return static_cast<float>(s * 0.25);
}

// ------------------------------------------------------ formant shift
// Single-formant probes: a vowel with ONE formant (F0 220 Hz), so the
// imposed envelope has a single peak. Two probes, 730 Hz and 2290 Hz,
// at shifts -12 / 0 / +12 st, 2 s each. The formant is the peak of
// the SHIFTED envelope (getShiftedEnvelope -- the envelope the band
// loop actually imposes), found within +/-0.5 octave of the probe
// formant; shift in semitones = 12*log2(f_shift / f_0). Identity is
// the 2290/730 ratio: the shift is a pure multiplicative envelope
// remap, so the ratio must be preserved across shifts. Filterbank
// only: the Ghost engine exposes no shifted-envelope tap, and its
// formant behaviour is already measured in DSPVerify
// (testGhostResynthesis: output centroid static 392/390/387 Hz
// across -12/0/+12 st -- resynthesis does not follow the shift).
// NOTE: an earlier version of this test read the formant from the
// output spectrum. That readout is invalid: the output at any one
// frequency is dominated by carrier-harmonic skirt leakage (the
// morph test proved this -- the 2202 Hz output probe sat at -85 dB
// for both vowels while the band-81 envelope moved +6.72 dB), and
// every probe/shift read the same carrier harmonic (1175 Hz).
// The envelope is the quantity the shift acts on, so it is the
// quantity measured. Two-formant /a/ and /i/ vowels are rendered
// for the listening set (RenderListening) -- their identity is
// judged by ear.
void testFormantShift()
{
    std::printf("--- formant shift: single-formant probes 730 / 2290 Hz, F0 220, shifts -12 / 0 / +12 st (shifted-envelope readout) ---\n");
    struct Probe { double fc; };
    const Probe probes[2] = { { 730 }, { 2290 } };
    const float shifts[3] = { -12.0f, 0.0f, 12.0f };
    double peakHz[2][3] = {};
    for (int pi = 0; pi < 2; ++pi)
    {
        for (int k = 0; k < 3; ++k)
        {
            FreeVox8DSP dsp;
            dsp.prepare(kSr, kBs, 2);
            auto p = baseParams();
            p.formantShift = shifts[k];
            dsp.setParameters(p);
            const int total = static_cast<int>(2.0 * kSr);
            juce::AudioBuffer<float> buf(2, kBs);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 50, 0.9f), 0);
            for (int start = 0; start < total; start += kBs)
            {
                for (int ch = 0; ch < 2; ++ch)
                    for (int n = 0; n < kBs; ++n)
                        buf.getWritePointer(ch)[n] = singleFormantSample((start + n) / kSr, 220.0,
                                                                        probes[pi].fc,
                                                                        probes[pi].fc < 1000 ? 120 : 250);
                dsp.process(buf, nullptr, midi);
            }
            // Peak of the shifted envelope within +/-0.5 octave.
            int bestBand = -1;
            float bestEnv = -1.0f;
            for (int b = 0; b < 128; ++b)
            {
                const float f = bandFreq(b);
                if (f < probes[pi].fc / 1.4142f || f > probes[pi].fc * 1.4142f)
                    continue;
                const float e = dsp.getShiftedEnvelope(b, 0);
                if (e > bestEnv)
                {
                    bestEnv = e;
                    bestBand = b;
                }
            }
            peakHz[pi][k] = bestBand >= 0 ? bandFreq(bestBand) : 0.0;
        }
        const double sPlus = 12.0 * std::log2(peakHz[pi][2] / std::max(peakHz[pi][1], 1.0));
        const double sMinus = 12.0 * std::log2(peakHz[pi][0] / std::max(peakHz[pi][1], 1.0));
        std::printf("  formant probe %.0f Hz filterbank: shifted-envelope peak %.0f / %.0f / %.0f Hz (-12/0/+12) | shift +12 -> %+.1f st, -12 -> %+.1f st\n",
                    probes[pi].fc, peakHz[pi][0], peakHz[pi][1], peakHz[pi][2], sPlus, sMinus);
        // The paper's under-shift claim: +12 st moves formants only
        // ~+6.3 st (filterbank, 8 bins/oct vs the layout's ~15.25
        // bands/oct). Assert the measured shift is in the under-shift
        // band, not the labelled 12 and not 0. Envelope-peak
        // resolution is ~0.8 st per band.
        check(sPlus > 3.0 && sPlus < 10.0,
              "formant shift: filterbank +12 st moves the formant ~+6 st (under-shift, not 0 and not 12)");
        check(sMinus < -3.0 && sMinus > -10.0,
              "formant shift: filterbank -12 st moves the formant ~-6 st");
    }
    // Identity: the 2290/730 ratio before/after the shift. A pure
    // multiplicative remap preserves it; a spectral shift that moved
    // pitch with formants would not.
    const double r0 = peakHz[1][1] / std::max(peakHz[0][1], 1.0);
    const double rPlus = peakHz[1][2] / std::max(peakHz[0][2], 1.0);
    const double rMinus = peakHz[1][0] / std::max(peakHz[0][0], 1.0);
    std::printf("  formant identity filterbank: 2290/730 ratio %.2f (shift 0) -> %.2f (+12) -> %.2f (-12); unshifted envelope peaks %.0f/%.0f Hz\n",
                r0, rPlus, rMinus, peakHz[0][1], peakHz[1][1]);
    check(std::abs(rPlus - r0) / r0 < 0.25 && std::abs(rMinus - r0) / r0 < 0.25,
          "formant shift: F2/F1 ratio preserved within 25% (identity holds, remap not spectral shift)");
    std::printf("  formant shift ghost: measured in DSPVerify testGhostResynthesis -- output centroid moves with the shift (172 / 295 / 1971 Hz for -12 / 0 / +12 st, FFT cross-synthesis redesign 2026-11-06)\n");
}

} // namespace

int main()
{
    std::printf("FreeVox8 artefact taxonomy -- objective measurements\n");
    printHardware();
    testPhasiness();
    testTransientSmearing();
    testMusicalNoise();
    testConsonantLoss();
    testFormantShift();
    std::printf("freeze blur / wobble: NOT re-run -- pilot + Bench cross-check: correction gain 0.03 dB, smear 4.8-6.2 dB (paper §4.7)\n");
    std::printf("pumping: NOT re-run -- Step 7 MaskPumping: usable range EMPTY at the 1 dB bar (paper §7)\n");
    std::printf(g_failures == 0 ? "ALL CHECKS PASSED\n" : "%d CHECK(S) FAILED\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
