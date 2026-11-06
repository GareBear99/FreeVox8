// FreeVox8 Ghost Mask pumping test (standalone, no DAW, no GUI).
//
// The Ghost (FFT cross-synthesis) engine's Mask, measured the same way
// MaskPumping.cpp measures the filterbank Mask -- same signals, same
// definitions, same verdict -- so the two engines are comparable:
//
//   * Carrier: saw chord A2 110 Hz + E3 164.81 Hz + A3 220 Hz, 0.22
//     each, constant amplitude, 8 s. No envelope of its own.
//   * Shout: CMU ARCTIC bdl arctic_a0108.wav, resampled 16 kHz ->
//     48 kHz, x3.0 gain, tanh soft-clip, looped to 8 s.
//   * Drum: 120 BPM 4-on-floor kick, 8 s.
//   * DSP: Mask mode, engine 1 (Ghost), mix 1.0, air 0,
//     transientProtect 0 (isolates the Mask), clarity 0.55,
//     quality 0.75, formantShift 0, outputGainDb -12.
//   * Pocket: Goertzel attenuation (dB) at modulator-strong (near)
//     vs modulator-weak (far) probe frequencies, depth d vs depth 0.
//     Probes picked from the modulator's own spectrum.
//   * Pumping: PAIRED 10 ms windowed-RMS ratio vs the depth-0 render
//     (same windows, p-p <= 1.0 dB). The paired ratio cancels the
//     carrier's own beating and the Ghost engine's OLA latency, so
//     the floor is 0 dB by construction and any remaining modulation
//     is the Mask's doing.
//   * Usable depth range: depths where the pocket is audible
//     (near attenuation >= 3 dB) WITHOUT broadband pumping
//     (paired p-p <= 1.0 dB). A REPORTED VERDICT, not a pass/fail
//     gate -- the range can legitimately be empty.
//
// The filterbank-specific gates in MaskPumping.cpp (maskK, depth-0
// bit-exactness, old-broadband reference, selectivity) do NOT apply
// here: the Ghost engine always vocodes (depth 0 is an FFT->IFFT
// round trip, not bit-exact dry) and never had a broadband Mask.
// The only gate is that the renders complete and the pocket/pumping
// numbers are printed.
//
// Run: FreeVox8_GhostMaskPumping [shout.wav]
// Exit 0 = ran cleanly (the usable range is a verdict, not a gate).

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

constexpr int kSr = 48000;
constexpr int kSeconds = 8;
constexpr int kTotal = kSr * kSeconds;
constexpr int kBlock = 64;
constexpr float kOutGain = 0.25118864f; // -12 dB

std::uint32_t g_rng = 0x12345678u;
float randUni()
{
    g_rng = 1664525u * g_rng + 1013904223u;
    return static_cast<float>(((g_rng >> 9) & 0x7FFFFF) / 4194304.0) - 1.0f;
}

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

struct Render
{
    std::vector<float> out;
    float meanMaskNear = 0.0f;
    float meanMaskFar = 0.0f;
};

Render renderGhostMask(const std::vector<float>& carrier, const std::vector<float>& mod,
                       float depth, int nearBand, int farBand)
{
    FreeVox8DSP dsp;
    dsp.prepare(kSr, kBlock, 2);
    FreeVox8DSP::Parameters p;
    p.mode = FreeVox8DSP::Mode::Mask;
    p.mix = 1.0f;
    p.carrierBlend = 0.65f; // inert in Mask mode
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
    p.engineMode = 1; // Ghost (FFT cross-synthesis)
    dsp.setParameters(p);

    Render r;
    r.out.reserve(kTotal);
    const int blocks = kTotal / kBlock;
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
        sumMaskNear += dsp.getMaskBin(nearBand);
        sumMaskFar += dsp.getMaskBin(farBand);
    }
    r.meanMaskNear = static_cast<float>(sumMaskNear / blocks);
    r.meanMaskFar = static_cast<float>(sumMaskFar / blocks);
    return r;
}

struct LegResult
{
    double usableLo = -1.0, usableHi = -1.0;
};

LegResult runLeg(const char* name, const std::vector<float>& carrier,
                 const std::vector<float>& mod)
{
    std::printf("\n-- %s (Ghost engine) --\n", name);
    double nearHz = 730.0, near2Hz = 500.0, farHz = 5000.0;
    pickProbes(mod, nearHz, near2Hz, farHz);
    const int nearBand = bandNear(static_cast<float>(nearHz));
    const int farBand = bandNear(static_cast<float>(farHz));
    std::printf("  probe bands: near %d (%.0f Hz), formant %d (%.0f Hz), far %d (%.0f Hz)\n",
                nearBand, nearHz, bandNear(static_cast<float>(near2Hz)), near2Hz, farBand, farHz);

    const float depths[] = { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f };
    std::vector<Render> renders;
    for (float d : depths)
        renders.push_back(renderGhostMask(carrier, mod, d, nearBand, farBand));

    const Render& ref = renders[0];
    const double pumpThresh = 1.0; // paired ratio: floor is 0 by construction
    std::printf("  paired pumping threshold %.2f dB p-p (ratio vs depth-0, same windows)\n", pumpThresh);

    LegResult leg;
    std::printf("  %6s | %10s %10s | %10s %10s %10s | %8s %8s | %s\n",
                "depth", "p-p", "p-p50", "near att", "form att", "far att", "maskNear", "maskFar", "usable?");
    for (size_t k = 0; k < renders.size(); ++k)
    {
        const Render& r = renders[k];
        const double pp = pumpingPpDb(r.out, ref.out);
        const double pp50 = pumpingPpDb(r.out, ref.out, kSr / 20); // 50 ms windows
        const double nearAtt = (k == 0) ? 0.0 : -pocketDb(r.out, ref.out, nearHz);
        const double near2Att = (k == 0) ? 0.0 : -pocketDb(r.out, ref.out, near2Hz);
        const double farAtt = (k == 0) ? 0.0 : -pocketDb(r.out, ref.out, farHz);
        const bool pocketOk = nearAtt >= 3.0;
        const bool pumpOk = pp <= pumpThresh;
        const bool usable = pocketOk && pumpOk;
        if (usable)
        {
            if (leg.usableLo < 0.0)
                leg.usableLo = depths[k];
            leg.usableHi = depths[k];
        }
        std::printf("  %6.2f | %8.2f dB %8.2f dB | %8.2f dB %8.2f dB %8.2f dB | %8.3f %8.3f | %s%s\n",
                    depths[k], pp, pp50, nearAtt, near2Att, farAtt,
                    r.meanMaskNear, r.meanMaskFar,
                    pocketOk ? "pocket " : "       ",
                    pumpOk ? "no-pump" : "PUMPS");
    }
    if (leg.usableLo >= 0.0)
        std::printf("  usable depth range (%s, Ghost): %.2f - %.2f\n", name, leg.usableLo, leg.usableHi);
    else
        std::printf("  usable depth range (%s, Ghost): NONE -- no depth is both audible and pump-free\n", name);
    return leg;
}

} // namespace

int main(int argc, char** argv)
{
    std::printf("FreeVox8 Ghost Mask pumping test\n");
    printHardware();

    const std::vector<float> carrier = makeCarrier();
    std::printf("carrier: saw chord 110 + 164.81 + 220 Hz, RMS %.3f, peak %.3f\n",
                rms(carrier), peakAbs(carrier));

    const char* shoutPath = (argc > 1) ? argv[1]
        : "/home/hatch/workspace/vox8-bench/corpus/cmu_us_bdl_arctic/wav/arctic_a0108.wav";
    const std::vector<float> shout = makeShout(shoutPath);
    if (shout.empty())
    {
        std::printf("\n-- shout leg SKIPPED: corpus file not found: %s --\n", shoutPath);
    }
    else
    {
        runLeg("shout", carrier, shout);
    }
    runLeg("drum", carrier, makeDrum());

    std::printf(g_failures == 0 ? "ALL CHECKS PASSED\n" : "%d CHECK(S) FAILED\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
