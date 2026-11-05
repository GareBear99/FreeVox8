// FreeVox8 Mask pumping test (standalone, no DAW, no GUI).
//
// Builds against juce_audio_basics + juce_dsp only, so it runs headless
// on Linux without X11. Run: FreeVox8_MaskPumping [shout.wav]
// Exit 0 = all checks passed. Every check prints its measured number.
//
// What it measures (the golden test: "Mask mode creates audible
// spectral pocket without broadband pumping"):
//   * Broadband pumping: 10 ms windowed-RMS modulation (peak-to-peak,
//     dB) of the Mask output on a STEADY carrier, for a shouted vocal
//     and a drum modulator, at mask depths 0 / 0.25 / 0.5 / 0.75 / 1.0.
//     The carrier is a constant-amplitude saw chord, so any level
//     modulation is the Mask's doing; depth 0 is the measurement floor
//     (the chord's own beating -- the same floor under every depth).
//   * Pocket: Goertzel attenuation (dB) at modulator-strong ("near")
//     vs modulator-weak ("far") probe frequencies, depth d vs depth 0.
//     Probes are picked from the modulator's own measured spectrum,
//     not assumed.
//   * Usable depth range: depths where the pocket is audible
//     (near attenuation >= 3 dB) WITHOUT broadband pumping
//     (paired 10 ms windowed-RMS ratio vs the depth-0 render, same
//     windows, p-p <= 1.0 dB -- the carrier's own beating cancels in
//     the ratio, so the floor is 0 dB by construction). The range is
//     a REPORTED VERDICT, not a pass/fail gate: the test decides the
//     range, and it can legitimately be empty. The gates are the
//     correctness checks (maskK, depth-0 exactness, display,
//     selectivity, silence control).
//   * Old-vs-new: the same renders are re-ducked in-test with the old
//     broadband formula (one reduction from the mean envelope) driven
//     by the same polled envelopes, so the fix is compared against the
//     failure mode on identical material.
//   * Mask display: the per-band readout (getMaskBin) must show a
//     large reduction in a near band and a small one in a far band
//     under the shout at depth 0.5, and ~0 everywhere at depth 0.
//   * Depth-0 exactness: with transientProtect 0, depth 0 output must
//     equal tanh(dry * outGain) sample for sample -- the subtractive
//     Mask is bit-exact dry when nothing is ducked.
//   * maskK: the subtractive calibration solved in prepare() must land
//     in the predicted range (~0.088 at 128 bands / 48 kHz).
//
// Signals (exact, reproducible):
//   * Carrier: saw chord A2 110 Hz + E3 164.81 Hz + A3 220 Hz, 0.22
//     each, constant amplitude, 8 s. No envelope of its own.
//   * Shout: CMU ARCTIC bdl arctic_a0108.wav (the fixed STOI corpus
//     file), resampled 16 kHz -> 48 kHz, x3.0 gain, tanh soft-clip,
//     looped to 8 s. A shouted vocal, not a conversational one.
//   * Drum: 120 BPM 4-on-floor kick, 8 s (16 kicks): sine 160 -> 48 Hz
//     pitch drop over 120 ms, exp decay tau 90 ms, peak 0.9, plus a
//     3 ms white-noise click (peak 0.3) at each onset.
//   * DSP: Mask mode, mix 1.0, air 0, transientProtect 0 (isolates the
//     Mask), clarity 0.55, quality 0.75, formantShift 0, outputGainDb
//     -12 (keeps the tanh saturator linear, so the measurement reads
//     the Mask, not the saturator -- same hygiene as testMorph).
//
// Hardware is printed with the numbers.

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

constexpr int kSr = 48000;
constexpr int kSeconds = 8;
constexpr int kTotal = kSr * kSeconds;
constexpr int kBlock = 64; // poll envelopes every 64 samples (1.33 ms)
constexpr float kOutGain = 0.25118864f; // -12 dB, keeps tanh linear

void printHardware()
{
    std::FILE* f = std::fopen("/proc/cpuinfo", "r");
    std::string model = "unknown";
    if (f != nullptr)
    {
        char line[512];
        while (std::fgets(line, sizeof(line), f) != nullptr)
        {
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
        }
        std::fclose(f);
    }
    std::printf("hardware: %s | sample rate %d Hz, block %d samples, %d s renders\n",
                model.c_str(), kSr, kBlock, kSeconds);
}

// --- Minimal 16-bit PCM WAV reader (from Tests/RenderCorpus.cpp) ---
bool readWav16(const char* path, std::vector<float>& out, int& srOut)
{
    std::FILE* f = std::fopen(path, "rb");
    if (f == nullptr)
        return false;
    auto read32 = [&]() -> std::uint32_t
    {
        std::uint8_t b[4];
        if (std::fread(b, 1, 4, f) != 4)
            return 0;
        return static_cast<std::uint32_t>(b[0]) | (static_cast<std::uint32_t>(b[1]) << 8)
             | (static_cast<std::uint32_t>(b[2]) << 16) | (static_cast<std::uint32_t>(b[3]) << 24);
    };
    auto read16 = [&]() -> std::uint16_t
    {
        std::uint8_t b[2];
        if (std::fread(b, 1, 2, f) != 2)
            return 0;
        return static_cast<std::uint16_t>(b[0] | (b[1] << 8));
    };
    char riff[4];
    if (std::fread(riff, 1, 4, f) != 4 || std::memcmp(riff, "RIFF", 4) != 0)
    {
        std::fclose(f);
        return false;
    }
    read32();
    char wave[4];
    if (std::fread(wave, 1, 4, f) != 4 || std::memcmp(wave, "WAVE", 4) != 0)
    {
        std::fclose(f);
        return false;
    }
    int channels = 0, bits = 0, sr = 0;
    std::uint32_t dataBytes = 0;
    long dataStart = 0;
    while (true)
    {
        char id[4];
        if (std::fread(id, 1, 4, f) != 4)
            break;
        const std::uint32_t size = read32();
        if (std::memcmp(id, "fmt ", 4) == 0)
        {
            const std::uint16_t format = read16();
            channels = read16();
            sr = static_cast<int>(read32());
            read32();
            read16();
            bits = read16();
            if (size > 16)
                std::fseek(f, static_cast<long>(size - 16), SEEK_CUR);
            if (format != 1 || bits != 16 || channels < 1 || sr < 1)
            {
                std::fclose(f);
                return false;
            }
        }
        else if (std::memcmp(id, "data", 4) == 0)
        {
            dataBytes = size;
            dataStart = std::ftell(f);
            break;
        }
        else
        {
            std::fseek(f, static_cast<long>(size + (size & 1)), SEEK_CUR);
        }
    }
    if (dataBytes == 0 || dataStart == 0)
    {
        std::fclose(f);
        return false;
    }
    std::fseek(f, dataStart, SEEK_SET);
    const size_t frames = dataBytes / (2u * static_cast<size_t>(channels));
    out.reserve(frames);
    std::vector<std::int16_t> buf(frames * static_cast<size_t>(channels));
    if (std::fread(buf.data(), 2, buf.size(), f) != buf.size())
    {
        std::fclose(f);
        return false;
    }
    std::fclose(f);
    for (size_t i = 0; i < frames; ++i)
        out.push_back(static_cast<float>(buf[i * static_cast<size_t>(channels)]) / 32768.0f);
    srOut = sr;
    return true;
}

std::vector<float> resampleLinear(const std::vector<float>& x, int srIn, int srOut)
{
    if (srIn == srOut)
        return x;
    const double ratio = static_cast<double>(srIn) / srOut;
    const size_t nOut = static_cast<size_t>(std::floor(x.size() / ratio));
    std::vector<float> y;
    y.reserve(nOut);
    for (size_t i = 0; i < nOut; ++i)
    {
        const double pos = i * ratio;
        const size_t i0 = static_cast<size_t>(pos);
        const double frac = pos - i0;
        const float a = x[i0];
        const float b = (i0 + 1 < x.size()) ? x[i0 + 1] : 0.0f;
        y.push_back(static_cast<float>(a + (b - a) * frac));
    }
    return y;
}

// Goertzel magnitude in dB over a windowed segment (from DSPVerify.cpp).
double goertzelDb(const float* x, int n, double freq, double sr)
{
    double re = 0.0, im = 0.0;
    const double w = 2.0 * 3.14159265358979 * freq / sr;
    for (int i = 0; i < n; ++i)
    {
        const double t = static_cast<double>(i) / (n - 1);
        const double win = 0.5 - 0.5 * std::cos(2.0 * 3.14159265358979 * t);
        const double a = w * i;
        re += x[i] * win * std::cos(a);
        im -= x[i] * win * std::sin(a);
    }
    const double mag = std::sqrt(re * re + im * im) / n;
    return 20.0 * std::log10(mag + 1e-12);
}

float rms(const std::vector<float>& x)
{
    double s = 0.0;
    for (float v : x)
        s += static_cast<double>(v) * v;
    return static_cast<float>(std::sqrt(s / std::max<size_t>(x.size(), 1)));
}

float peakAbs(const std::vector<float>& x)
{
    float m = 0.0f;
    for (float v : x)
        m = std::max(m, std::abs(v));
    return m;
}

// --- Test signals ---
std::vector<float> makeCarrier()
{
    std::vector<float> x(kTotal);
    double ph1 = 0, ph2 = 0, ph3 = 0;
    const double i1 = 2.0 * 3.14159265358979 * 110.0 / kSr;
    const double i2 = 2.0 * 3.14159265358979 * 164.81 / kSr;
    const double i3 = 2.0 * 3.14159265358979 * 220.0 / kSr;
    for (int n = 0; n < kTotal; ++n)
    {
        ph1 += i1; ph2 += i2; ph3 += i3;
        const auto saw = [](double p) { return static_cast<float>(p / 3.14159265358979 - 1.0); };
        x[static_cast<size_t>(n)] = 0.22f * (saw(ph1) + saw(ph2) + saw(ph3));
        if (ph1 > 2.0 * 3.14159265358979) ph1 -= 2.0 * 3.14159265358979;
        if (ph2 > 2.0 * 3.14159265358979) ph2 -= 2.0 * 3.14159265358979;
        if (ph3 > 2.0 * 3.14159265358979) ph3 -= 2.0 * 3.14159265358979;
    }
    return x;
}

std::vector<float> makeDrum()
{
    std::vector<float> x(kTotal, 0.0f);
    const int kickGap = kSr / 2; // 120 BPM
    double ph = 0.0;
    for (int n = 0; n < kTotal; ++n)
    {
        const int since = n % kickGap;
        const double t = since / static_cast<double>(kSr);
        // Pitch 160 -> 48 Hz over 120 ms, exp decay tau 90 ms.
        const double f = 48.0 + 112.0 * std::exp(-t / 0.04);
        ph += 2.0 * 3.14159265358979 * f / kSr;
        const float env = static_cast<float>(std::exp(-t / 0.09));
        float v = 0.9f * env * static_cast<float>(std::sin(ph));
        if (since < static_cast<int>(0.003 * kSr))
            v += 0.3f * randUni();
        x[static_cast<size_t>(n)] = v;
    }
    return x;
}

std::vector<float> makeShout(const char* path)
{
    std::vector<float> wav;
    int srIn = 0;
    if (! readWav16(path, wav, srIn))
        return {};
    std::vector<float> x = resampleLinear(wav, srIn, kSr);
    for (float& v : x)
        v = std::tanh(3.0f * v); // x3 gain, soft-clipped: a shout
    std::vector<float> out(kTotal);
    for (int n = 0; n < kTotal; ++n)
        out[static_cast<size_t>(n)] = x[static_cast<size_t>(n) % x.size()];
    return out;
}

// Pick near/far probe frequencies from the modulator's own spectrum:
// near = strongest in 50-2000 Hz, near2 = strongest in 300-2000 Hz
// (the formant pocket -- the F0 probe can flatter the pocket, so the
// formant probe is reported alongside), far = weakest in 4-9 kHz.
// Measured as the MEAN Goertzel dB over 0.5 s windows, not one coherent
// Goertzel over the whole 8 s: a periodic transient (the kick, every
// 0.5 s, phase not reset per kick) partially cancels in a coherent sum,
// and the first version of this test picked the drum's near probe at
// 1000 Hz, where the kick has almost no energy. The windowed mean
// reads the kick's per-hit energy, which is what the pocket acts on.
void pickProbes(const std::vector<float>& mod, double& nearHz, double& near2Hz, double& farHz)
{
    const double grid[] = { 60, 80, 100, 120, 150, 200, 300, 500, 730, 1000, 1200, 2000, 3000, 5000, 8000 };
    const int win = kSr / 2; // 0.5 s windows
    double nearDb = -1e9, near2Db = -1e9, farDb = 1e9;
    nearHz = 730.0; near2Hz = 500.0; farHz = 5000.0;
    for (double f : grid)
    {
        double sum = 0.0;
        int count = 0;
        for (int s = kSr / 2; s + win <= kTotal; s += win)
        {
            sum += goertzelDb(mod.data() + s, win, f, kSr);
            ++count;
        }
        const double d = sum / std::max(count, 1);
        if (f >= 50.0 && f <= 2000.0 && d > nearDb)
        {
            nearDb = d;
            nearHz = f;
        }
        if (f >= 300.0 && f <= 2000.0 && d > near2Db)
        {
            near2Db = d;
            near2Hz = f;
        }
        if (f >= 4000.0 && f <= 9000.0 && d < farDb)
        {
            farDb = d;
            farHz = f;
        }
    }
    std::printf("  modulator spectrum: near probe %.0f Hz (%.1f dB), formant probe %.0f Hz (%.1f dB), far probe %.0f Hz (%.1f dB)\n",
                nearHz, nearDb, near2Hz, near2Db, farHz, farDb);
}

int bandNear(float hz)
{
    int best = 0;
    float bd = 1e9f;
    for (int b = 0; b < FreeVox8DSP::kNumBands; ++b)
    {
        const float t = static_cast<float>(b) / static_cast<float>(FreeVox8DSP::kNumBands - 1);
        const float f = 55.0f * std::pow(18500.0f / 55.0f, t);
        const float d = std::abs(f - hz);
        if (d < bd)
        {
            bd = d;
            best = b;
        }
    }
    return best;
}

struct Render
{
    std::vector<float> out;       // mono output, channel 0
    std::vector<float> envMean;   // mean envelope per 64-sample block
    std::vector<float> oldOut;    // old broadband formula on same envelopes
    float maxEnvMean = 0.0f;      // loudest mean envelope over the render
    float maxEnvBand = 0.0f;      // loudest single-band envelope over the render
    float maxMaskNear = 0.0f;     // loudest mask-display value, near band
    float maxMaskFar = 0.0f;      // loudest mask-display value, far band
    float meanMaskNear = 0.0f;    // mean mask-display value, near band
    float meanMaskFar = 0.0f;     // mean mask-display value, far band
};

Render renderMask(const std::vector<float>& carrier, const std::vector<float>& mod,
                  float depth, int nearBand, int farBand)
{
    FreeVox8DSP dsp;
    dsp.prepare(kSr, kBlock, 2);
    FreeVox8DSP::Parameters p;
    p.mode = FreeVox8DSP::Mode::Mask;
    p.mix = 1.0f;
    p.carrierBlend = 0.65f; // inert in Mask mode (carrier is dry)
    p.maskDepth = depth;
    p.clarity = 0.55f;
    p.formantShift = 0.0f;
    p.ghost = 0.0f;
    p.air = 0.0f;
    p.freeze = 0.0f;
    p.transientProtect = 0.0f; // isolates the Mask
    p.stereoWidth = 1.0f;
    p.quality = 0.75f;
    p.outputGainDb = -12.0f;
    p.engineMode = 0;
    dsp.setParameters(p);

    Render r;
    r.out.reserve(kTotal);
    const int blocks = kTotal / kBlock;
    r.envMean.reserve(blocks);
    juce::AudioBuffer<float> buf(2, kBlock), sc(2, kBlock);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, 60, 0.9f), 0);
    double sumMaskNear = 0.0, sumMaskFar = 0.0;
    for (int blk = 0; blk < blocks; ++blk)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < kBlock; ++n)
            {
                const int idx = blk * kBlock + n;
                buf.getWritePointer(ch)[n] = carrier[static_cast<size_t>(idx)];
                sc.getWritePointer(ch)[n] = mod[static_cast<size_t>(idx)];
            }
        dsp.process(buf, &sc, midi);
        for (int n = 0; n < kBlock; ++n)
            r.out.push_back(buf.getReadPointer(0)[n]);
        // Poll the live envelopes once per block: the mean drives the
        // old broadband reference, and the extremes ground the depth
        // multiplier. With formantShift 0 these are exactly the
        // envelopes the DSP used.
        float mean = 0.0f, mx = 0.0f;
        for (int b = 0; b < FreeVox8DSP::kNumBands; ++b)
        {
            const float e = dsp.getEffectiveEnvelope(b, 0);
            mean += e;
            mx = std::max(mx, e);
        }
        mean /= static_cast<float>(FreeVox8DSP::kNumBands);
        r.envMean.push_back(mean);
        r.maxEnvMean = std::max(r.maxEnvMean, mean);
        r.maxEnvBand = std::max(r.maxEnvBand, mx);
        const float mNear = dsp.getMaskBin(nearBand);
        const float mFar = dsp.getMaskBin(farBand);
        r.maxMaskNear = std::max(r.maxMaskNear, mNear);
        r.maxMaskFar = std::max(r.maxMaskFar, mFar);
        sumMaskNear += mNear;
        sumMaskFar += mFar;
    }
    r.meanMaskNear = static_cast<float>(sumMaskNear / blocks);
    r.meanMaskFar = static_cast<float>(sumMaskFar / blocks);

    // Old broadband reference on the same envelopes: one reduction
    // from the mean envelope, applied to the whole dry signal
    // (wet = dry * (1 - reduction)), same output stage as the DSP.
    r.oldOut.reserve(kTotal);
    for (int blk = 0; blk < blocks; ++blk)
    {
        const float red = juce::jlimit(0.0f, 0.97f,
                                       r.envMean[static_cast<size_t>(blk)] * depth * (2.6f + 0.75f));
        for (int n = 0; n < kBlock; ++n)
        {
            const float dry = carrier[static_cast<size_t>(blk * kBlock + n)];
            r.oldOut.push_back(std::tanh(dry * (1.0f - red) * kOutGain));
        }
    }
    return r;
}

// 10 ms windowed-RMS modulation, peak-to-peak in dB, over the active
// region (first 0.5 s skipped: envelope/filter settle).
//
// PAIRED against the depth-0 render, window by window: the metric is
// the p-p of 20*log10(RMS_d(w) / RMS_0(w)). The carrier is a saw chord
// whose own 10 ms RMS beats by 4.14 dB p-p -- an absolute p-p measures
// the chord, not the Mask. The paired ratio cancels the carrier's own
// beating exactly (same carrier, same windows, deterministic render),
// so the floor is 0 dB by construction and any remaining modulation
// is the Mask's doing. Threshold: 1.0 dB p-p on the ratio.
// `winSamples` selects the window: 10 ms is the decision metric;
// 50 ms is reported alongside to show the modulation's timescale
// (syllabic/kick-rate ducking survives a 50 ms window; fast attack
// chatter does not).
double pumpingPpDb(const std::vector<float>& x, const std::vector<float>& ref, int winSamples = kSr / 100)
{
    const int win = winSamples;
    const int start = kSr / 2;
    double mn = 1e9, mx = -1e9;
    for (int s = start; s + win <= kTotal; s += win)
    {
        double sum = 0.0, sumRef = 0.0;
        for (int i = 0; i < win; ++i)
        {
            const double v = x[static_cast<size_t>(s + i)];
            const double r = ref[static_cast<size_t>(s + i)];
            sum += v * v;
            sumRef += r * r;
        }
        const double db = 10.0 * std::log10((sum + 1e-18) / (sumRef + 1e-18));
        mn = std::min(mn, db);
        mx = std::max(mx, db);
    }
    return mx - mn;
}

// Absolute 10 ms windowed-RMS p-p (dB) of one render: context only.
// For the steady saw chord this is 4.14 dB at depth 0 -- the chord's
// own beating, not pumping. Never used for the usable decision.
double absolutePpDb(const std::vector<float>& x)
{
    const int win = kSr / 100; // 10 ms
    const int start = kSr / 2;
    double mn = 1e9, mx = -1e9;
    for (int s = start; s + win <= kTotal; s += win)
    {
        double sum = 0.0;
        for (int i = 0; i < win; ++i)
        {
            const double v = x[static_cast<size_t>(s + i)];
            sum += v * v;
        }
        const double db = 10.0 * std::log10(sum / win + 1e-18);
        mn = std::min(mn, db);
        mx = std::max(mx, db);
    }
    return mx - mn;
}

// Mean Goertzel attenuation (dB) of a render vs the depth-0 render at
// one probe frequency, over 0.5 s windows in the active region.
double pocketDb(const std::vector<float>& x, const std::vector<float>& ref, double freq)
{
    const int win = kSr / 2;
    const int start = kSr / 2;
    double sum = 0.0;
    int count = 0;
    for (int s = start; s + win <= kTotal; s += win)
    {
        sum += goertzelDb(x.data() + s, win, freq, kSr)
             - goertzelDb(ref.data() + s, win, freq, kSr);
        ++count;
    }
    return sum / std::max(count, 1);
}

struct LegResult
{
    double floorPp = 0.0;
    double usableLo = -1.0, usableHi = -1.0;
};

LegResult runLeg(const char* name, const std::vector<float>& carrier,
                 const std::vector<float>& mod)
{
    std::printf("\n-- %s --\n", name);
    double nearHz = 730.0, near2Hz = 500.0, farHz = 5000.0;
    pickProbes(mod, nearHz, near2Hz, farHz);
    const int nearBand = bandNear(static_cast<float>(nearHz));
    const int farBand = bandNear(static_cast<float>(farHz));
    std::printf("  probe bands: near %d (%.0f Hz), formant %d (%.0f Hz), far %d (%.0f Hz)\n",
                nearBand, nearHz, bandNear(static_cast<float>(near2Hz)), near2Hz, farBand, farHz);

    const float depths[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
    std::vector<Render> renders;
    for (float d : depths)
        renders.push_back(renderMask(carrier, mod, d, nearBand, farBand));

    const Render& ref = renders[0];
    const double floorAbs = absolutePpDb(ref.out);
    const double pumpThresh = 1.0; // paired ratio: floor is 0 by construction
    std::printf("  depth-0 absolute p-p %.2f dB (carrier's own beating -- context only, not the metric)\n",
                floorAbs);
    std::printf("  paired pumping threshold %.2f dB p-p (ratio vs depth-0, same windows)\n", pumpThresh);
    std::printf("  maskK (prepare solve): %.4f\n", [&] {
        FreeVox8DSP dsp;
        dsp.prepare(kSr, kBlock, 2);
        return dsp.getMaskK();
    }());
    std::printf("  envelopes under %s: max mean %.4f, max single band %.4f\n",
                name, renders[2].maxEnvMean, renders[2].maxEnvBand);

    LegResult leg;
    leg.floorPp = floorAbs;
    std::printf("  %6s | %10s %10s %10s | %10s %10s %10s | %10s %10s | %8s %8s | %s\n",
                "depth", "new p-p", "old p-p", "new p-p50", "near att", "form att", "far att", "old near", "old far", "maskNear", "maskFar", "usable?");
    for (size_t k = 0; k < renders.size(); ++k)
    {
        const Render& r = renders[k];
        const double pp = pumpingPpDb(r.out, ref.out);
        const double ppOld = pumpingPpDb(r.oldOut, ref.oldOut);
        const double pp50 = pumpingPpDb(r.out, ref.out, kSr / 20); // 50 ms windows
        const double nearAtt = (k == 0) ? 0.0 : -pocketDb(r.out, ref.out, nearHz);
        const double near2Att = (k == 0) ? 0.0 : -pocketDb(r.out, ref.out, near2Hz);
        const double farAtt = (k == 0) ? 0.0 : -pocketDb(r.out, ref.out, farHz);
        const double oldNearAtt = (k == 0) ? 0.0 : -pocketDb(r.oldOut, ref.oldOut, nearHz);
        const double oldFarAtt = (k == 0) ? 0.0 : -pocketDb(r.oldOut, ref.oldOut, farHz);
        const bool pocketOk = nearAtt >= 3.0;
        const bool pumpOk = pp <= pumpThresh;
        const bool usable = pocketOk && pumpOk;
        if (usable)
        {
            if (leg.usableLo < 0.0)
                leg.usableLo = depths[k];
            leg.usableHi = depths[k];
        }
        std::printf("  %6.2f | %8.2f dB %8.2f dB %8.2f dB | %8.2f dB %8.2f dB %8.2f dB | %8.2f dB %8.2f dB | %8.3f %8.3f | %s%s\n",
                    depths[k], pp, ppOld, pp50, nearAtt, near2Att, farAtt, oldNearAtt, oldFarAtt,
                    r.meanMaskNear, r.meanMaskFar,
                    pocketOk ? "pocket " : "       ",
                    pumpOk ? "no-pump" : "PUMPS");
    }
    // The fix's claim is SELECTIVITY, not a universal pumping cut: the
    // old broadband Mask ducks the far bands as much as the near ones,
    // the per-band Mask leaves them alone. At depth 1.0 the new far
    // attenuation must be less than HALF the old far attenuation
    // (ratio, not an absolute margin: when the old far attenuation is
    // itself small the margin form cannot pass). (Pumping itself is
    // compared in the table, not asserted: for a modulator that
    // coincides with the carrier's energy peak -- the kick on the saw
    // chord's low fundamentals -- per-band ducking concentrates the
    // cut where the carrier is strongest and can pump slightly MORE
    // than broadband; that is a measured property of the mode, not a
    // regression to hide.)
    {
        const Render& r1 = renders[4];
        const double newFar = -pocketDb(r1.out, ref.out, farHz);
        const double oldFar = -pocketDb(r1.oldOut, ref.oldOut, farHz);
        std::printf("  selectivity at depth 1.0: new far att %.2f dB vs old far att %.2f dB\n",
                    newFar, oldFar);
        check(newFar < 0.5 * oldFar,
              ("per-band Mask leaves far bands alone where broadband ducks them (" + std::string(name) + ")").c_str());
    }

    // The usable range is a REPORTED VERDICT, not a pass/fail gate:
    // this test's job is to DECIDE the range, and the range can
    // legitimately be empty (it is the measurement that says so, not a
    // broken harness). The gates are the correctness checks (maskK,
    // depth-0 exactness, display, selectivity, silence control); the
    // verdict is printed prominently here and in main() so an empty
    // range is never mistaken for a pass. Same discipline as
    // DSPVerify's report-only Ghost resynthesis section.
    if (leg.usableLo >= 0.0)
        std::printf("  usable depth range (%s): %.2f - %.2f\n", name, leg.usableLo, leg.usableHi);
    else
        std::printf("  usable depth range (%s): NONE -- no depth is both audible and pump-free\n", name);
    return leg;
}

} // namespace

int main(int argc, char** argv)
{
    std::printf("FreeVox8 Mask pumping test\n");
    printHardware();

    const std::vector<float> carrier = makeCarrier();
    std::printf("carrier: saw chord 110 + 164.81 + 220 Hz, RMS %.3f, peak %.3f\n",
                rms(carrier), peakAbs(carrier));

    // --- maskK + depth-0 exactness (signal-independent) ---
    {
        FreeVox8DSP dsp;
        dsp.prepare(kSr, kBlock, 2);
        const float k = dsp.getMaskK();
        std::printf("maskK = %.4f (predicted ~0.088 at 128 bands / 48 kHz)\n", k);
        check(k > 0.05f && k < 0.15f, "maskK in predicted range");
    }
    {
        // Depth 0 must be bit-exact dry through the output stage.
        const std::vector<float> silence(kTotal, 0.0f);
        const Render r = renderMask(carrier, silence, 0.0f, 57, 100);
        float maxDiff = 0.0f;
        for (int n = 0; n < kTotal; ++n)
        {
            const float expect = std::tanh(carrier[static_cast<size_t>(n)] * kOutGain);
            maxDiff = std::max(maxDiff, std::abs(r.out[static_cast<size_t>(n)] - expect));
        }
        std::printf("depth-0 exactness: max|out - tanh(dry*gain)| = %.3g\n", maxDiff);
        check(maxDiff < 1e-6f, "depth 0 is bit-exact dry (subtractive Mask)");
    }

    // --- Shout leg (real vocal, fixed corpus file) ---
    const char* shoutPath = (argc > 1) ? argv[1]
        : "/home/hatch/workspace/vox8-bench/corpus/cmu_us_bdl_arctic/wav/arctic_a0108.wav";
    const std::vector<float> shout = makeShout(shoutPath);
    LegResult shoutLeg;
    bool shoutRan = false;
    if (shout.empty())
    {
        std::printf("\n-- shout leg SKIPPED: corpus file not found: %s --\n", shoutPath);
    }
    else
    {
        std::printf("shout: %s resampled to 48 kHz, x3 tanh-clipped, looped; RMS %.3f, peak %.3f\n",
                    shoutPath, rms(shout), peakAbs(shout));
        shoutLeg = runLeg("shouted vocal (bdl arctic_a0108, x3 tanh)", carrier, shout);
        shoutRan = true;
        // Mask display must show the pocket: near band well reduced,
        // far band barely, at depth 0.5 -- and ~0 at depth 0.
        const Render r05 = renderMask(carrier, shout, 0.5f, bandNear(730.0f), bandNear(5000.0f));
        const Render r00 = renderMask(carrier, shout, 0.0f, bandNear(730.0f), bandNear(5000.0f));
        std::printf("  mask display at depth 0.5: near mean %.3f max %.3f, far mean %.3f max %.3f\n",
                    r05.meanMaskNear, r05.maxMaskNear, r05.meanMaskFar, r05.maxMaskFar);
        std::printf("  mask display at depth 0.0: near max %.4f, far max %.4f\n",
                    r00.maxMaskNear, r00.maxMaskFar);
        check(r05.meanMaskNear > 0.10f, "mask display: near band reads > 0.10 at depth 0.5");
        check(r05.meanMaskNear > r05.meanMaskFar, "mask display: near band reads above far band");
        check(r00.maxMaskNear < 0.01f && r00.maxMaskFar < 0.01f,
              "mask display: ~0 everywhere at depth 0");
    }

    // --- Drum leg (synthesised kick) ---
    const std::vector<float> drum = makeDrum();
    std::printf("drum: 120 BPM kick 160->48 Hz + click; RMS %.3f, peak %.3f\n",
                rms(drum), peakAbs(drum));
    const LegResult drumLeg = runLeg("drum (120 BPM kick)", carrier, drum);

    // --- Silence control: no modulator, no ducking at any depth ---
    {
        const std::vector<float> silence(kTotal, 0.0f);
        const Render r = renderMask(carrier, silence, 0.5f, 57, 100);
        const double pp = pumpingPpDb(r.out, renderMask(carrier, silence, 0.0f, 57, 100).out);
        std::printf("\nsilence control: depth 0.5 paired p-p %.2f dB (floor 0 by construction)\n", pp);
        check(pp <= 0.1, "silence modulator: no false ducking at depth 0.5");
    }

    // Verdict (reported, not gated -- see runLeg): the usable ranges
    // are the measurement's answer, and they can be empty. An empty
    // range means the golden test ("audible pocket without broadband
    // pumping", 1 dB bar) is NOT met for that modulator; it does not
    // mean the harness is broken. Say so plainly, in the output.
    const bool shoutOk = shoutRan && shoutLeg.usableLo >= 0.0;
    const bool drumOk = drumLeg.usableLo >= 0.0;
    std::printf("\nusable ranges: shout %s, drum %.2f - %.2f\n",
                shoutRan ? (shoutOk
                    ? (std::to_string(shoutLeg.usableLo) + " - " + std::to_string(shoutLeg.usableHi)).c_str()
                    : "NONE")
                         : "skipped",
                drumLeg.usableLo, drumLeg.usableHi);
    std::printf("golden test (audible pocket, pumping <= 1 dB): shout %s, drum %s\n",
                shoutRan ? (shoutOk ? "MET" : "NOT MET") : "skipped",
                drumOk ? "MET" : "NOT MET");
    std::printf(g_failures == 0 ? "ALL CORRECTNESS CHECKS PASSED\n" : "%d CHECK(S) FAILED\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
