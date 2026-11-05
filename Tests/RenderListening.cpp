// RenderListening -- render the Rank 8 listening-test materials.
//
// Writes 16-bit PCM mono WAVs at 44.1 kHz into <outDir> (default
// ~/workspace/vox8-bench/listening), plus LISTENING.md labelling every
// file with what it is and what the listener is asked to judge. The
// subjective tests (word recognition, ABX preference, shifted-vowel
// identity by ear) need Gary's ears -- this program renders the
// materials and asks the questions; it does NOT answer them.
//
// Usage:
//   RenderListening <outDir> [arctic_a0108.wav]
//     arctic_a0108.wav : CMU ARCTIC bdl file for the Mask shout renders;
//       if it cannot be read, the three mask_shout_* files are skipped
//       with a note (everything else still renders).
//
// Materials:
//   * morph ABX: morph_midpoint.wav (two-envelope morph, alpha=0.5,
//     /a/ -> /i/) vs crossfade_midpoint.wav (0.5 x vocoder(/a/) +
//     0.5 x vocoder(/i/), same sources, same carrier), plus the two
//     endpoint references. Question: one hybrid sound, or two sounds
//     at once?
//   * formant shift: /a/ and /i/ at -12 / 0 / +12 st (filterbank).
//     Question: same vowel, only bigger/smaller -- or a different
//     vowel / chipmunk?
//   * fricative Air: "saa" dry + vocoded at air 0 / 0.5 / 1.0.
//     Question: does the /s/ survive, and does more Air help or hiss?
//   * freeze: filterbank frozen static vs vibrato /a/ (4 s hold),
//     plus the Ghost frozen vibrato (broken resynthesis -- a
//     reference for what the Ghost hold sounds like, not a vocoder).
//     Question: does the frozen pitch hold or wobble? NOTE: there is
//     NO freeze ABX pair here -- the WASPAA correction failed the
//     pilot, so no corrected freeze exists to compare against.
//   * mask pumping: shouted ARCTIC sentence ducking a saw chord at
//     Mask depths 0 / 0.25 / 0.5. Question: pocket vs whole-carrier
//     pumping? (Objective numbers: usable range EMPTY at the 1 dB bar.)
//   * transient: drum dry vs vocoded (clarity 0.55 / 1.0).
//     Question: attacks soft, doubled, or intact?
//   * musical noise: sparse /a/ bursts ducking a saw chord, Mask
//     depth 0.8 vs 0. Question: chirpy artefacts in the quiet gaps?
//   * word recognition: the fixed-corpus vocoded renders already in
//     ~/workspace/vox8-bench/corpus/renders/ (bdl + slt, 10 sentences
//     each) -- referenced in LISTENING.md, not re-rendered here.
//
// All renders: filterbank engine unless the filename says ghost;
// carrier MIDI 50 (D3) unless noted; outputGain -12 dB on the Mask /
// morph renders (keeps the tanh saturator out of the judgement).

#include "FreeVox8DSP.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>

namespace
{

constexpr double kSr = 44100.0;
constexpr int kBs = 512;

std::uint32_t g_rng = 0x12345678u;
float randUni()
{
    g_rng = 1664525u * g_rng + 1013904223u;
    return static_cast<float>(((g_rng >> 9) & 0x7FFFFF) / 4194304.0) - 1.0f;
}

// --- 16-bit PCM mono WAV writer (from Tests/RenderCorpus.cpp) ---
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
    w16(1);
    w16(1);
    w32(static_cast<std::uint32_t>(sr));
    w32(static_cast<std::uint32_t>(sr * 2));
    w16(2);
    w16(16);
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
        const float b = (i0 + 1 < x.size()) ? x[i0 + 1] : x.back();
        y.push_back(static_cast<float>(a + (b - a) * frac));
    }
    return y;
}

// --- Test signals (same fixtures as Tests/ArtefactTaxonomy.cpp) ---
float vowelSample(double t, double f0, int vowel, double vibRate = 0.0, double vibDepthSt = 0.0)
{
    double f = f0;
    if (vibRate > 0.0)
        f = f0 * std::pow(2.0, vibDepthSt * std::sin(2.0 * 3.14159265358979 * vibRate * t) / 12.0);
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

float drumSample(double t)
{
    const double beat = 0.5;
    const double ph = std::fmod(t, beat);
    const int beatIdx = static_cast<int>(t / beat);
    if (beatIdx >= 8 || ph > 0.35)
        return 0.0f;
    const double f = 48.0 + 112.0 * std::exp(-ph / 0.04);
    const double kick = 0.9 * std::exp(-ph / 0.09) * std::sin(2.0 * 3.14159265358979 * f * ph);
    double click = 0.0;
    if (ph < 0.003)
    {
        const std::uint32_t s = static_cast<std::uint32_t>(t * kSr) * 2654435761u;
        click = 0.3 * (static_cast<double>((s >> 9) & 0x7FFFFF) / 4194304.0 - 1.0);
    }
    return static_cast<float>(kick + click);
}

float sawChordSample(double t)
{
    double v = 0.0;
    for (double f : { 110.0, 164.81, 220.0 })
    {
        const double ph = std::fmod(f * t, 1.0);
        v += 0.22 * (2.0 * ph - 1.0);
    }
    return static_cast<float>(v);
}

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

FreeVox8DSP::Parameters baseParams()
{
    FreeVox8DSP::Parameters p;
    p.mode = FreeVox8DSP::Mode::Vocoder;
    p.mix = 1.0f;
    p.carrierBlend = 1.0f;
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

// Run the DSP over generated main/sidechain signals. genMain/genSc
// take t in seconds; genSc may be null. freezeAtSec < 0 = never.
std::vector<float> renderGen(const FreeVox8DSP::Parameters& params,
                             float (*genMain)(double), float (*genSc)(double),
                             double seconds, int midiNote, double freezeAtSec = -1.0)
{
    FreeVox8DSP dsp;
    dsp.prepare(kSr, kBs, 2);
    auto p = params;
    dsp.setParameters(p);
    const int total = static_cast<int>(seconds * kSr);
    juce::AudioBuffer<float> buf(2, kBs), sc(2, kBs);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, midiNote, 0.9f), 0);
    std::vector<float> out;
    out.reserve(static_cast<size_t>(total));
    bool frozen = false;
    for (int start = 0; start < total; start += kBs)
    {
        if (freezeAtSec >= 0.0 && ! frozen && start >= freezeAtSec * kSr)
        {
            p.freeze = 1.0f;
            dsp.setParameters(p);
            frozen = true;
        }
        for (int ch = 0; ch < 2; ++ch)
            for (int n = 0; n < kBs; ++n)
            {
                const double t = (start + n) / kSr;
                buf.getWritePointer(ch)[n] = genMain(t);
                if (genSc != nullptr)
                    sc.getWritePointer(ch)[n] = genSc(t);
            }
        dsp.process(buf, genSc != nullptr ? &sc : nullptr, midi);
        for (int n = 0; n < kBs; ++n)
            out.push_back(buf.getReadPointer(0)[n]);
    }
    return out;
}

// Run the DSP over a precomputed mono signal (main) with an optional
// precomputed sidechain signal (looped if shorter than the render).
std::vector<float> renderVec(const FreeVox8DSP::Parameters& params,
                             const std::vector<float>& main,
                             const std::vector<float>* sidechain,
                             int midiNote)
{
    FreeVox8DSP dsp;
    dsp.prepare(kSr, kBs, 2);
    dsp.setParameters(params);
    const int total = static_cast<int>(main.size());
    juce::AudioBuffer<float> buf(2, kBs), sc(2, kBs);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, midiNote, 0.9f), 0);
    std::vector<float> out;
    out.reserve(main.size());
    for (int start = 0; start < total; start += kBs)
    {
        const int n = std::min(kBs, total - start);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < n; ++i)
            {
                buf.getWritePointer(ch)[i] = main[static_cast<size_t>(start + i)];
                if (sidechain != nullptr)
                    sc.getWritePointer(ch)[i] = (*sidechain)[(static_cast<size_t>(start + i)) % sidechain->size()];
            }
        for (int ch = 0; ch < 2; ++ch)
            for (int i = n; i < kBs; ++i)
            {
                buf.getWritePointer(ch)[i] = 0.0f;
                if (sidechain != nullptr)
                    sc.getWritePointer(ch)[i] = 0.0f;
            }
        dsp.process(buf, sidechain != nullptr ? &sc : nullptr, midi);
        for (int i = 0; i < n; ++i)
            out.push_back(buf.getReadPointer(0)[i]);
    }
    return out;
}

void writeOut(const std::string& dir, const std::string& name, const std::vector<float>& x)
{
    const std::string path = dir + "/" + name;
    double peak = 0.0;
    for (float v : x)
        peak = std::max(peak, static_cast<double>(std::abs(v)));
    if (! writeWav16(path.c_str(), x, static_cast<int>(kSr)))
        std::fprintf(stderr, "cannot write %s\n", path.c_str());
    else
        std::printf("  wrote %s (%.2f s, peak %.3f)\n", path.c_str(), x.size() / kSr, peak);
}

// --- Signal shims (function pointers for renderGen) ---
float genVowelA(double t) { return vowelSample(t, 220.0, 0); }
float genVowelI(double t) { return vowelSample(t, 220.0, 1); }
float genVowelAVib(double t) { return vowelSample(t, 220.0, 0, 6.0, 0.5); }
float genDrum(double t) { return drumSample(t); }
float genSawChord(double t) { return sawChordSample(t); }
float genSparseA(double t)
{
    const double ph = std::fmod(t, 0.8);
    return (ph < 0.2) ? vowelSample(t, 220.0, 0) * 0.5f : 0.0f;
}

} // namespace

int main(int argc, char** argv)
{
    const std::string outDir = argc > 1 ? argv[1]
        : std::string(std::getenv("HOME") ? std::getenv("HOME") : "") + "/workspace/vox8-bench/listening";
    const std::string arcticPath = argc > 2 ? argv[2]
        : std::string(std::getenv("HOME") ? std::getenv("HOME") : "")
          + "/workspace/vox8-bench/corpus/cmu_us_bdl_arctic/wav/arctic_a0108.wav";
    std::printf("RenderListening -> %s\n", outDir.c_str());

    // --- Morph ABX: midpoint vs crossfade, plus endpoints ---
    {
        auto p = baseParams();
        p.outputGainDb = -12.0f;
        const std::vector<float> epA = renderGen(p, genVowelA, nullptr, 3.0, 50);
        const std::vector<float> epI = renderGen(p, genVowelI, nullptr, 3.0, 50);
        writeOut(outDir, "morph_endpoint_a.wav", epA);
        writeOut(outDir, "morph_endpoint_i.wav", epI);
        auto pm = baseParams();
        pm.mode = FreeVox8DSP::Mode::Morph;
        pm.ghost = 0.5f;
        pm.outputGainDb = -12.0f;
        writeOut(outDir, "morph_midpoint.wav", renderGen(pm, genVowelA, genVowelI, 3.0, 50));
        std::vector<float> cross(epA.size(), 0.0f);
        for (size_t i = 0; i < cross.size(); ++i)
            cross[i] = 0.5f * (epA[i] + epI[i]);
        writeOut(outDir, "crossfade_midpoint.wav", cross);
    }

    // --- Formant shift: /a/ and /i/ at -12 / 0 / +12 st ---
    for (int vowel = 0; vowel <= 1; ++vowel)
    {
        for (float shift : { -12.0f, 0.0f, 12.0f })
        {
            auto p = baseParams();
            p.formantShift = shift;
            const std::string fname = std::string("formant_") + (vowel == 0 ? "a" : "i")
                + (shift < 0 ? "_minus12.wav" : (shift > 0 ? "_plus12.wav" : "_zero.wav"));
            writeOut(outDir, fname,
                     renderGen(p, vowel == 0 ? genVowelA : genVowelI, nullptr, 3.0, 50));
        }
    }

    // --- Fricative Air: "saa" dry + vocoded air 0 / 0.5 / 1.0 ---
    {
        const std::vector<float> s = fricative(4000, 8000, 0.4);
        std::vector<float> saa;
        saa.reserve(static_cast<size_t>(1.5 * kSr));
        for (float v : s)
            saa.push_back(v);
        for (int i = static_cast<int>(0.4 * kSr); i < static_cast<int>(1.5 * kSr); ++i)
            saa.push_back(vowelSample(i / kSr, 220.0, 0));
        writeOut(outDir, "fricative_saa_dry.wav", saa);
        for (float air : { 0.0f, 0.5f, 1.0f })
        {
            auto p = baseParams();
            p.air = air;
            const std::string fname = std::string("fricative_saa_air")
                + (air == 0.0f ? "0" : (air == 0.5f ? "05" : "1")) + ".wav";
            writeOut(outDir, fname, renderVec(p, saa, nullptr, 60));
        }
    }

    // --- Freeze: filterbank static vs vibrato, Ghost vibrato ---
    {
        auto p = baseParams();
        writeOut(outDir, "freeze_static_filterbank.wav",
                 renderGen(p, genVowelA, nullptr, 5.0, 50, 1.0));
        writeOut(outDir, "freeze_vibrato_filterbank.wav",
                 renderGen(p, genVowelAVib, nullptr, 5.0, 50, 1.0));
        auto pg = baseParams();
        pg.engineMode = 1;
        writeOut(outDir, "freeze_vibrato_ghost.wav",
                 renderGen(pg, genVowelAVib, nullptr, 5.0, 50, 1.0));
    }

    // --- Mask pumping: shouted ARCTIC sentence ducking a saw chord ---
    {
        std::vector<float> arctic;
        int srIn = 0;
        if (readWav16(arcticPath.c_str(), arctic, srIn))
        {
            std::vector<float> shout = resampleLinear(arctic, srIn, static_cast<int>(kSr));
            for (float& v : shout)
                v = std::tanh(v * 3.0f);
            // Loop to 8 s.
            std::vector<float> mod;
            mod.reserve(static_cast<size_t>(8.0 * kSr));
            while (mod.size() < static_cast<size_t>(8.0 * kSr))
                mod.insert(mod.end(), shout.begin(), shout.end());
            mod.resize(static_cast<size_t>(8.0 * kSr));
            std::vector<float> chord;
            chord.reserve(mod.size());
            for (size_t i = 0; i < mod.size(); ++i)
                chord.push_back(sawChordSample(i / kSr));
            for (float depth : { 0.0f, 0.25f, 0.5f })
            {
                auto p = baseParams();
                p.mode = FreeVox8DSP::Mode::Mask;
                p.maskDepth = depth;
                p.outputGainDb = -12.0f;
                char name[64];
                std::snprintf(name, sizeof(name), "mask_shout_depth%02d.wav",
                              static_cast<int>(depth * 100.0f));
                writeOut(outDir, name, renderVec(p, chord, &mod, 50));
            }
        }
        else
        {
            std::printf("  NOTE: cannot read %s -- mask_shout_* files skipped\n", arcticPath.c_str());
        }
    }

    // --- Transient: drum dry vs vocoded (clarity 0.55 / 1.0) ---
    {
        std::vector<float> dry;
        for (int i = 0; i < static_cast<int>(4.5 * kSr); ++i)
            dry.push_back(drumSample(i / kSr));
        // The dry drum peaks at 1.17 (kick + click sum) -- scale the
        // listening file x0.8 so the WAV does not clip. The measurement
        // (Tests/ArtefactTaxonomy.cpp) used the unclipped signal.
        std::vector<float> dryScaled = dry;
        for (float& v : dryScaled)
            v *= 0.8f;
        writeOut(outDir, "transient_drum_dry.wav", dryScaled);
        for (float clarity : { 0.55f, 1.0f })
        {
            auto p = baseParams();
            p.clarity = clarity;
            char name[64];
            std::snprintf(name, sizeof(name), "transient_drum_vocoded%s.wav",
                          clarity > 0.9f ? "_fast" : "");
            writeOut(outDir, name, renderVec(p, dry, nullptr, 50));
        }
    }

    // --- Musical noise: sparse bursts ducking a saw chord, Mask 0.8 vs 0 ---
    {
        std::vector<float> chord, sc;
        for (int i = 0; i < static_cast<int>(8.0 * kSr); ++i)
        {
            const double t = i / kSr;
            chord.push_back(sawChordSample(t));
            sc.push_back(genSparseA(t));
        }
        for (float depth : { 0.8f, 0.0f })
        {
            auto p = baseParams();
            p.mode = FreeVox8DSP::Mode::Mask;
            p.maskDepth = depth;
            p.outputGainDb = -12.0f;
            char name[64];
            std::snprintf(name, sizeof(name), "sparse_mask_depth%02d.wav",
                          static_cast<int>(depth * 100.0f));
            writeOut(outDir, name, renderVec(p, chord, &sc, 50));
        }
    }

    std::printf("done. See LISTENING.md in %s for what to judge per file.\n", outDir.c_str());
    return 0;
}
