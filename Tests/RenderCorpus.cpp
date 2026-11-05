// RenderCorpus -- headless FreeVox8 renderer for the STOI corpus.
//
// Renders one WAV file through the filterbank engine (Vocoder mode) and
// writes the vocoded output plus the resampled clean reference, so the
// ARC-AudioBench STOI tool can score them (clean ref = exactly what the
// DSP received, degraded = vocoded output). The filterbank has no frame
// delay, but the envelope followers lag the input by roughly their attack
// time, so the tool also measures the best alignment lag (normalised
// cross-correlation over +/-50 ms) and prints it; the alignment itself
// is applied by the caller (see the STOI run notes), so the written
// output is the raw render, not a shifted copy.
//
// Usage:
//   RenderCorpus <in.wav> <out.wav> <clean44.wav> [--note N] [--sr SR]
//     in.wav   : 16-bit PCM WAV, any sample rate, mono or stereo (left
//                channel taken). CMU ARCTIC files are 16 kHz mono.
//     out.wav  : 16-bit PCM mono WAV at SR, the vocoded output.
//     clean44.wav : 16-bit PCM mono WAV at SR, the resampled input.
//     --note N : fixed carrier MIDI note (default 60, C4).
//     --sr SR  : render sample rate (default 44100).
//
// Fixed carrier (same note, same internal carrier blend, every run) is
// the STOI protocol in the Vox8 paper §7: the carrier is a constant of
// the measurement, not a variable.
#include "FreeVox8DSP.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace
{

// --- Minimal 16-bit PCM WAV reader (RIFF chunks, mono/stereo) ---
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
    read32(); // chunk size
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
            read32(); // byte rate
            read16(); // block align
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

// --- 16-bit PCM mono WAV writer ---
bool writeWav16(const char* path, const std::vector<float>& x, int sr)
{
    std::FILE* f = std::fopen(path, "wb");
    if (f == nullptr)
        return false;
    const std::uint32_t n = static_cast<std::uint32_t>(x.size());
    const std::uint32_t bytes = n * 2;
    const auto w32 = [&](std::uint32_t v) { std::fwrite(&v, 4, 1, f); };
    const auto w16 = [&](std::uint16_t v) { std::fwrite(&v, 2, 1, f); };
    std::fwrite("RIFF", 1, 4, f);
    w32(36 + bytes);
    std::fwrite("WAVE", 1, 4, f);
    std::fwrite("fmt ", 1, 4, f);
    w32(16);
    w16(1);      // PCM
    w16(1);      // mono
    w32(static_cast<std::uint32_t>(sr));
    w32(static_cast<std::uint32_t>(sr * 2));
    w16(2);      // block align
    w16(16);     // bits
    std::fwrite("data", 1, 4, f);
    w32(bytes);
    for (float v : x)
    {
        const float c = std::max(-1.0f, std::min(1.0f, v));
        const std::int16_t s = static_cast<std::int16_t>(std::lrint(c * 32767.0f));
        w16(static_cast<std::uint16_t>(s));
    }
    std::fclose(f);
    return true;
}

// Linear-interpolation resampler to the render rate.
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
        const float b = (i0 + 1 < x.size()) ? x[i0 + 1] : x.back();
        y.push_back(static_cast<float>(a + (b - a) * frac));
    }
    return y;
}

// Best alignment lag of `degraded` relative to `clean`, in samples,
// by normalised cross-correlation over +/-maxLag. Positive lag means
// the degraded signal is late (output lags input).
int bestLag(const std::vector<float>& clean, const std::vector<float>& degraded, int maxLag)
{
    double bestScore = -1e30;
    int best = 0;
    const size_t n = std::min(clean.size(), degraded.size());
    for (int lag = -maxLag; lag <= maxLag; ++lag)
    {
        double num = 0.0, denC = 0.0, denD = 0.0;
        // aligned[i] = degraded[i + lag]; lag > 0 means the degraded
        // signal is late by `lag` samples and must be shifted earlier.
        const size_t start = static_cast<size_t>(std::max(0, -lag));
        const size_t end = std::min(n, n - static_cast<size_t>(std::max(0, lag)));
        for (size_t i = start; i < end; ++i)
        {
            const double c = clean[i];
            const double d = degraded[i + static_cast<size_t>(lag)];
            num += c * d;
            denC += c * c;
            denD += d * d;
        }
        const double score = num / std::sqrt(denC * denD + 1e-18);
        if (score > bestScore)
        {
            bestScore = score;
            best = lag;
        }
    }
    std::printf("  alignment: best lag %+d samples (%+.2f ms), corr %.4f\n",
                best, best * 1000.0 / 44100.0, bestScore);
    return best;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        std::fprintf(stderr, "usage: %s <in.wav> <out.wav> <clean44.wav> [--note N] [--sr SR]\n", argv[0]);
        return 2;
    }
    int note = 60, sr = 44100;
    for (int i = 4; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--note") == 0 && i + 1 < argc)
            note = std::atoi(argv[++i]);
        else if (std::strcmp(argv[i], "--sr") == 0 && i + 1 < argc)
            sr = std::atoi(argv[++i]);
    }

    std::vector<float> in;
    int srIn = 0;
    if (!readWav16(argv[1], in, srIn))
    {
        std::fprintf(stderr, "cannot read %s (need 16-bit PCM WAV)\n", argv[1]);
        return 1;
    }
    const std::vector<float> clean = resampleLinear(in, srIn, sr);
    std::printf("render: %s (%d Hz, %.2f s) -> %d Hz, carrier MIDI %d, %d bands\n",
                argv[1], srIn, static_cast<double>(in.size()) / srIn, sr, note,
                FreeVox8DSP::kNumBands);

    FreeVox8DSP dsp;
    dsp.prepare(static_cast<double>(sr), 512, 2);
    FreeVox8DSP::Parameters p;
    p.engineMode = 0; // filterbank
    p.mode = FreeVox8DSP::Mode::Vocoder;
    p.mix = 1.0f;
    p.carrierBlend = 1.0f; // pure internal carrier
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
    dsp.setParameters(p);

    const int bs = 512;
    juce::AudioBuffer<float> buf(2, bs);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.9f), 0);
    std::vector<float> out;
    out.reserve(clean.size());
    for (size_t start = 0; start < clean.size(); start += bs)
    {
        const int n = static_cast<int>(std::min<size_t>(bs, clean.size() - start));
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
                buf.getWritePointer(ch)[i] = clean[start + static_cast<size_t>(i)];
        for (int ch = 0; ch < 2; ++ch)
            for (int i = n; i < bs; ++i)
                buf.getWritePointer(ch)[i] = 0.0f;
        dsp.process(buf, nullptr, midi);
        for (int i = 0; i < n; ++i)
            out.push_back(buf.getReadPointer(0)[i]);
    }

    if (!writeWav16(argv[2], out, sr) || !writeWav16(argv[3], clean, sr))
    {
        std::fprintf(stderr, "cannot write output WAVs\n");
        return 1;
    }
    double peak = 0.0;
    for (float v : out)
        peak = std::max(peak, static_cast<double>(std::abs(v)));
    std::printf("  wrote %s (%.2f s, peak %.3f) and %s\n",
                argv[2], static_cast<double>(out.size()) / sr, peak, argv[3]);
    bestLag(clean, out, sr / 20); // +/-50 ms
    return 0;
}
