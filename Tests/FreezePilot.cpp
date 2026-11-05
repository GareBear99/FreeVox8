// FreeVox8 freeze pilot (Step 2) — standalone, no JUCE, no DAW.
//
// Question: does the WASPAA 2001 beta/lambda correction have audible value
// in the freeze path? The pilot measures FIRST, implements SECOND.
//
// Method: faithful extraction of FreeVox8SpectralEngine::analyseBins()
// (same bandFreq, same Hann window, same Goertzel accumulation, same
// envelope follower, same frozen[] leaky update), INCLUDING the per-band
// stride fix of 2026-11-05: the shipped stride-4 accumulation aliased
// every band above sr/8 into mirrors of low-frequency content (a pure
// 676 Hz sine lit band 117 at 11701 Hz stronger than its true band 55).
// The aliasProbe() below verifies the fix: with per-band stride the
// mirror must be gone. Feed it a synthetic sustained vowel (harmonic
// series with /a/-like formants) with Sundberg vibrato (6 Hz, +/-0.5
// semitone; +/-1.0 semitone as a stress case), freeze mid-vibrato, and
// measure the frozen snapshot's peak-bin energy concentration.
//
// Metric: for the peak band, R = E_peak / (E_peak-2 + E_peak-1 + E_peak+1
// + E_peak+2), in dB, plus C = E_peak / sum(E_peak-2..peak+2). A stationary
// (no-vibrato) control gives R_stationary; the vibrato smear is
// R_stationary - R_vibrato.
//
// DECISION RULE (written in advance, before the correction is implemented):
// implement the beta/lambda correction in analyseBins() ONLY if the
// corrected frozen snapshot improves R by >= 3 dB over the uncorrected
// snapshot on the +/-0.5 semitone test signal. If the improvement is
// < 3 dB, the correction stays out and the freeze blur stays a known,
// bounded limitation. A second gate: the correction must not degrade R on
// the stationary control by more than 0.5 dB.
//
// Why 3 dB: a 3 dB rise in peak-to-adjacent energy ratio is a doubling of
// the peak's dominance over its neighbours -- the smallest change that
// survives the resynthesis chain as an audible tightening of a frozen
// pitch rather than measurement noise. The ABX listening check (Gary's
// ears, Step 8) is the final gate; this pilot only decides whether the
// correction is worth building at all.
//
// Note on the WASPAA method: eq. 9-11 (verified against the full text,
// ResearchGate, 2026-11-05) estimate beta/lambda from the COMPLEX log
// spectrum at neighbouring bins under a GAUSSIAN window. analyseBins()
// keeps no phase and uses Hann, so the correction is not a drop-in on its
// re/im accumulators; the "with correction" half of this pilot needs a
// complex-spectrum implementation, built only if the baseline below shows
// a smear worth correcting.
//
// Build:  g++ -std=c++17 -O2 Tests/FreezePilot.cpp -o build-test/FreeVox8_FreezePilot
// Run:    build-test/FreeVox8_FreezePilot

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
constexpr int kFftSize = 1024;
constexpr int kHopSize = 256;
constexpr int kDisplayBins = 128;
constexpr double kSr = 44100.0;

float bandFreq(int b) noexcept
{
    const float t = static_cast<float>(b) / static_cast<float>(kDisplayBins - 1);
    return 55.0f * std::pow(18500.0f / 55.0f, t);
}

// --- faithful extraction of FreeVox8SpectralEngine::analyseBins() ---
struct GhostAnalyser
{
    std::vector<float> modRing = std::vector<float>(kFftSize, 0.0f);
    std::vector<float> window = std::vector<float>(kFftSize, 0.0f);
    std::vector<float> env = std::vector<float>(kDisplayBins, 0.0f);
    std::vector<float> frozen = std::vector<float>(kDisplayBins, 0.0f);
    int writeIndex = 0;
    int hopCountdown = kHopSize;

    GhostAnalyser()
    {
        for (int i = 0; i < kFftSize; ++i)
        {
            const double t = static_cast<double>(i) / static_cast<double>(kFftSize - 1);
            window[static_cast<size_t>(i)] =
                static_cast<float>(0.5 - 0.5 * std::cos(2.0 * 3.141592653589793 * t));
        }
    }

    void feed(float s, bool freeze)
    {
        modRing[static_cast<size_t>(writeIndex)] = s;
        writeIndex = (writeIndex + 1) & (kFftSize - 1);
        if (--hopCountdown <= 0)
        {
            hopCountdown = kHopSize;
            analyseBins(freeze);
        }
    }

    void analyseBins(bool fr)
    {
        for (int b = 0; b < kDisplayBins; ++b)
        {
            const float freq = bandFreq(b);
            const double w = 2.0 * 3.141592653589793 * static_cast<double>(freq) / kSr;
            double re = 0.0, im = 0.0;
            // Per-band stride (mirrors the shipped fix): stride 4 is
            // alias-free only below sr/8; higher bands step finer.
            int stride = 4;
            while (stride > 1 && kSr / (2.0 * stride) <= freq)
                --stride;
            for (int i = 0; i < kFftSize; i += stride)
            {
                const int idx = (writeIndex + i) & (kFftSize - 1);
                const double a = w * static_cast<double>(i);
                const float s = modRing[static_cast<size_t>(idx)] * window[static_cast<size_t>(i)];
                re += s * std::cos(a);
                im -= s * std::sin(a);
            }
            const float mag = static_cast<float>(std::sqrt(re * re + im * im)) * 0.0018f * static_cast<float>(stride) * 0.25f;
            float& e = env[static_cast<size_t>(b)];
            e = mag > e ? 0.24f * e + 0.76f * mag : 0.92f * e + 0.08f * mag;
            if (! fr)
                frozen[static_cast<size_t>(b)] = 0.998f * frozen[static_cast<size_t>(b)] + 0.002f * e;
        }
    }
};

// --- synthetic sustained vowel: harmonic series, /a/-like formants ---
// F0 with vibrato: f0(t) = F0 * 2^(depthSt/12 * sin(2*pi*fVib*t)).
// Harmonic amplitudes shaped by two formant resonances (730 Hz, 1090 Hz).
float vowelSample(double t, double f0, double vibHz, double depthSt)
{
    const double vib = depthSt == 0.0 ? 0.0 : std::sin(2.0 * 3.141592653589793 * vibHz * t);
    const double f = f0 * std::pow(2.0, depthSt / 12.0 * vib);
    double s = 0.0;
    for (int h = 1; h <= 20; ++h)
    {
        const double fh = f * h;
        if (fh > 8000.0)
            break;
        const double formant = 1.0 / (1.0 + std::pow((fh - 730.0) / 120.0, 2.0))
                             + 0.8 / (1.0 + std::pow((fh - 1090.0) / 140.0, 2.0))
                             + 0.15;
        const double amp = formant / (1.0 + 0.15 * h);
        s += amp * std::sin(2.0 * 3.141592653589793 * fh * t + 0.7 * h);
    }
    return static_cast<float>(s * 0.25);
}

struct Snapshot
{
    int peakBand = -1;
    double peakFreq = 0.0;
    double ratioDb = 0.0; // peak energy vs +-1,+-2 neighbours
    double conc = 0.0;    // peak share of peak+-2 window
};

Snapshot measure(const std::vector<float>& frozen)
{
    Snapshot out;
    // Peak search restricted to the alias-free range: analyseBins() steps
    // the ring by 4, so its effective sample rate is sr/4 and bands above
    // sr/8 (~5512 Hz at 44.1 kHz) are mirrors of low-frequency content
    // (see aliasProbe() below). The strongest band above that limit is
    // reported separately as an artefact, not as a peak.
    int peak = 0;
    for (int b = 1; b < kDisplayBins - 1; ++b)
    {
        if (bandFreq(b) >= static_cast<float>(kSr / 8.0))
            break;
        if (frozen[static_cast<size_t>(b)] > frozen[static_cast<size_t>(peak)])
            peak = b;
    }
    out.peakBand = peak;
    out.peakFreq = bandFreq(peak);
    const auto e = [&](int b) {
        if (b < 0 || b >= kDisplayBins)
            return 0.0;
        const double v = frozen[static_cast<size_t>(b)];
        return v * v;
    };
    const double ep = e(peak);
    const double adj = e(peak - 2) + e(peak - 1) + e(peak + 1) + e(peak + 2);
    out.ratioDb = 10.0 * std::log10(ep / std::max(adj, 1e-12));
    out.conc = ep / std::max(ep + adj, 1e-12);
    return out;
}

void aliasProbe()
{
    // Pure 676 Hz sine through the shipped analyseBins(). If the stride-4
    // accumulation aliases, bands above sr/8 will light up as mirrors of
    // 676 Hz (mirror at |676 - 11025| = 10349 Hz, etc.).
    GhostAnalyser an;
    for (int n = 0; n < static_cast<int>(1.0 * kSr); ++n)
    {
        const double t = n / kSr;
        an.feed(static_cast<float>(0.8 * std::sin(2.0 * 3.141592653589793 * 676.0 * t)), false);
    }
    std::vector<int> idx(kDisplayBins);
    for (int b = 0; b < kDisplayBins; ++b)
        idx[static_cast<size_t>(b)] = b;
    std::sort(idx.begin(), idx.end(), [&](int a, int b) {
        return an.frozen[static_cast<size_t>(a)] > an.frozen[static_cast<size_t>(b)];
    });
    std::printf("alias probe (pure 676 Hz sine): top bands by frozen energy:\n");
    for (int k = 0; k < 6; ++k)
    {
        const int b = idx[static_cast<size_t>(k)];
        std::printf("  band %3d (%.0f Hz)  env=%.4f%s\n", b, bandFreq(b),
                    an.frozen[static_cast<size_t>(b)],
                    bandFreq(b) >= static_cast<float>(kSr / 8.0) ? "  <-- ABOVE sr/8: alias mirror" : "");
    }
}

void runCase(const char* name, double f0, double vibHz, double depthSt)
{
    GhostAnalyser an;
    const int total = static_cast<int>(2.0 * kSr);       // 2 s of vowel
    const int freezeAt = static_cast<int>(1.0 * kSr);    // freeze mid-vibrato
    for (int n = 0; n < total; ++n)
    {
        const double t = n / kSr;
        an.feed(vowelSample(t, f0, vibHz, depthSt), n >= freezeAt);
    }
    // Let a few more frames render with freeze held (frozen[] must not move).
    const std::vector<float> held = an.frozen;
    for (int n = 0; n < static_cast<int>(0.25 * kSr); ++n)
        an.feed(vowelSample((total + n) / kSr, f0, vibHz, depthSt), true);
    double maxDrift = 0.0;
    for (int b = 0; b < kDisplayBins; ++b)
        maxDrift = std::max(maxDrift, std::abs(static_cast<double>(an.frozen[static_cast<size_t>(b)]) - held[static_cast<size_t>(b)]));
    const Snapshot s = measure(an.frozen);
    std::printf("%-28s peak band %3d (%.0f Hz)  R=%+6.2f dB  C=%.3f  freeze-hold drift=%.2g\n",
                name, s.peakBand, s.peakFreq, s.ratioDb, s.conc, maxDrift);
}

} // namespace

int main()
{
    std::printf("FreeVox8 freeze pilot -- baseline (NO correction implemented yet)\n");
    std::printf("Test signal: sustained /a/ vowel, vibrato 6 Hz (Sundberg 5-7 Hz)\n");
    std::printf("Decision rule: implement beta/lambda only if corrected R improves by >= 3 dB\n\n");
    aliasProbe();
    std::printf("\n");
    runCase("stationary control F0=220", 220.0, 6.0, 0.0);
    runCase("vibrato +-0.5 st F0=220", 220.0, 6.0, 0.5);
    runCase("vibrato +-1.0 st F0=220", 220.0, 6.0, 1.0);
    runCase("stationary control F0=250", 250.0, 6.0, 0.0);
    runCase("vibrato +-0.5 st F0=250", 250.0, 6.0, 0.5);
    runCase("vibrato +-1.0 st F0=250", 250.0, 6.0, 1.0);
    return 0;
}
