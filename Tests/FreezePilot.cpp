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
#include <complex>
#include <cstdint>
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

// --- WASPAA beta/lambda correction (Step 2, second half) ---
// Gaussian window w(t) = exp(-pi*alpha*t^2), alpha = 10740 (sigma_t = 170
// samples, window value ~0.01 at the frame edges). Alpha 4725 (sigma_t =
// 256) was tried first and REJECTED: its 13.5% edge truncation convolves
// the spectrum with a sinc whose sidelobes dominate the log-spectrum fit
// -- a stationary 1000 Hz tone then read beta = +2355 Hz/s, f0 24 Hz low
// (diagnostic /tmp/waspaa_diag.cpp). At alpha = 10740 the same diagnostic
// recovers beta = 0.0 stationary, +391 for a +400 chirp, +5036 for +5000,
// +222 for a vibrato-scale +241 chirp at 220 Hz, f0 within 0.3 Hz. The
// fit math itself is exact on analytic eq. 7 spectra at either alpha.
// (WASPAA 2001 eq. 6-7): X(f) = c/sqrt(alpha - j beta) *
// exp(-pi (f - f0 + j lambda)^2 / (alpha - j beta)).
// eq. 9: log[X(f+df)/X(f)] = A f + B; eq. 10: A = -2 pi (alpha + j beta) /
// (alpha^2 + beta^2) df, B = A (df/2 - f0 + j lambda); eq. 11: beta =
// Im[A]/Re[A] * alpha, f0 = df/2 - Re[B/A], lambda = Im[B/A].
// De-sweep transfer (eq. 15 with beta_y = 0, lambda_y = lambda):
// H(f) = sqrt((alpha - j beta)/alpha) * exp(pi[(f-f0+j lambda)^2 /
// (alpha - j beta) - (f-f0+j lambda)^2/alpha]).
// Time origin is the frame centre, so the DFT phase is smooth in f and
// the principal-branch complex log needs no unwrapping across df steps.
// Equation text re-verified against the ResearchGate full text 2026-11-05.
constexpr double kGaussAlpha = 10740.0;

struct PeakEst
{
    int band = -1;
    double fPeak = 0.0, beta = 0.0, f0 = 0.0, lambda = 0.0, mag = 0.0;
    bool valid = false;
};

using Cplx = std::complex<double>;

Cplx gaussDft(const std::vector<float>& frame, double freq)
{
    // frame[0..1023] oldest -> newest; t measured from frame centre.
    Cplx sum(0.0, 0.0);
    for (int i = 0; i < kFftSize; ++i)
    {
        const double t = (static_cast<double>(i) - (kFftSize - 1) / 2.0) / kSr;
        const double w = std::exp(-3.141592653589793 * kGaussAlpha * t * t);
        const double a = -2.0 * 3.141592653589793 * freq * t;
        sum += static_cast<double>(frame[static_cast<size_t>(i)]) * w * Cplx(std::cos(a), std::sin(a));
    }
    return sum;
}

// Estimate (beta, f0, lambda) for one spectral peak from the complex
// Gaussian spectrum around fPeak, using a least-squares fit of
// g(f) = log[X(f+df)/X(f)] = A f + B over f = fPeak + k*df.
PeakEst estimatePeak(const std::vector<float>& frame, int band, double mag)
{
    PeakEst out;
    out.band = band;
    out.fPeak = bandFreq(band);
    out.mag = mag;
    const double df = kSr / static_cast<double>(kFftSize); // FFT bin spacing,
    // as the paper specifies -- NOT the local log-band spacing. The band
    // spacing at 220 Hz is ~10 Hz, a quarter of sigma_f (41 Hz): the fit
    // then reads tiny log differences and any spectral perturbation (the
    // neighbouring harmonics' truncation sidelobes) blows beta up to
    // thousands of Hz/s on vowel frames (first run: |beta_fund| mean 5660,
    // theory max ~240). With df = 43 Hz the fit spans +-2 sigma_f and is
    // well conditioned; the synthetic recovery still verifies.
    if (df <= 0.0)
        return out;
    // X at fPeak + k*df, k = -2..+2.
    Cplx X[5];
    for (int k = -2; k <= 2; ++k)
        X[k + 2] = gaussDft(frame, out.fPeak + k * df);
    // g(f) at f = fPeak + k*df, k = -2..+1.
    const int n = 4;
    double fs[4];
    Cplx gs[4];
    for (int k = 0; k < n; ++k)
    {
        fs[k] = out.fPeak + (k - 2) * df;
        if (std::abs(X[k + 1]) < 1e-12 || std::abs(X[k]) < 1e-12)
            return out;
        gs[k] = std::log(X[k + 1] / X[k]);
    }
    // Complex least squares: g = A f + B.
    double fMean = 0.0;
    Cplx gMean(0.0, 0.0);
    for (int k = 0; k < n; ++k)
    {
        fMean += fs[k];
        gMean += gs[k];
    }
    fMean /= n;
    gMean /= static_cast<double>(n);
    double sff = 0.0;
    Cplx sfg(0.0, 0.0);
    for (int k = 0; k < n; ++k)
    {
        const double d = fs[k] - fMean;
        sff += d * d;
        sfg += d * (gs[k] - gMean);
    }
    if (sff <= 0.0)
        return out;
    const Cplx A = sfg / sff;
    const Cplx B = gMean - A * fMean;
    if (A.real() >= 0.0)
        return out; // eq. 10 requires Re[A] < 0
    const double beta = A.imag() / A.real() * kGaussAlpha; // eq. 11
    const Cplx bOverA = B / A;
    const double f0 = 0.5 * df - bOverA.real();
    const double lambda = bOverA.imag();
    // Cross-check: alpha recovered from A must match the window alpha.
    const Cplx u = -A / (2.0 * 3.141592653589793 * df); // (alpha + j beta)/(alpha^2+beta^2)
    const double alphaEst = (1.0 / u).real();
    if (alphaEst < 0.5 * kGaussAlpha || alphaEst > 2.0 * kGaussAlpha)
        return out;
    if (std::abs(beta) > 2.0 * kGaussAlpha)
        return out;
    if (std::abs(f0 - out.fPeak) > 2.0 * df)
        return out;
    if (std::abs(lambda) > 100.0)
        return out;
    out.beta = beta;
    out.f0 = f0;
    out.lambda = lambda;
    out.valid = true;
    return out;
}

Cplx desweepTransfer(double f, const PeakEst& p)
{
    const Cplx alpha(kGaussAlpha, 0.0);
    const Cplx aX(kGaussAlpha, -p.beta); // alpha - j beta
    const Cplx d(f - p.f0, p.lambda);    // f - f0 + j lambda
    Cplx h = std::sqrt(aX / alpha) * std::exp(3.141592653589793 * (d * d / aX - d * d / alpha));
    const double m = std::abs(h);
    if (m > 2.0)
        h *= 2.0 / m;
    else if (m > 0.0 && m < 0.5)
        h *= 0.5 / m;
    return h;
}

// Gaussian-window analyser mirroring GhostAnalyser's ring/hop/follower/
// frozen discipline exactly, except the spectrum is the complex Gaussian
// DFT and, when correct == true, each peak's eq. 15 de-sweep transfer is
// applied before magnitudes feed the envelope follower.
struct CorrectedAnalyser
{
    std::vector<float> modRing = std::vector<float>(kFftSize, 0.0f);
    std::vector<float> gaussWin = std::vector<float>(kFftSize, 0.0f);
    std::vector<float> env = std::vector<float>(kDisplayBins, 0.0f);
    std::vector<float> frozen = std::vector<float>(kDisplayBins, 0.0f);
    std::vector<Cplx> spec = std::vector<Cplx>(kDisplayBins);
    std::vector<double> betaFundTrace;
    int writeIndex = 0;
    int hopCountdown = kHopSize;
    bool correct = false;
    int lastValidPeaks = 0;
    double lastBetaFund = 0.0;

    explicit CorrectedAnalyser(bool doCorrect) : correct(doCorrect)
    {
        for (int i = 0; i < kFftSize; ++i)
        {
            const double t = (static_cast<double>(i) - (kFftSize - 1) / 2.0) / kSr;
            gaussWin[static_cast<size_t>(i)] = static_cast<float>(std::exp(-3.141592653589793 * kGaussAlpha * t * t));
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
        std::vector<float> frame(kFftSize);
        for (int i = 0; i < kFftSize; ++i)
            frame[static_cast<size_t>(i)] = modRing[static_cast<size_t>((writeIndex + i) & (kFftSize - 1))];
        for (int b = 0; b < kDisplayBins; ++b)
        {
            const double f = bandFreq(b);
            Cplx sum(0.0, 0.0);
            for (int i = 0; i < kFftSize; ++i)
            {
                const double t = (static_cast<double>(i) - (kFftSize - 1) / 2.0) / kSr;
                const double a = -2.0 * 3.141592653589793 * f * t;
                sum += static_cast<double>(frame[static_cast<size_t>(i)]) * gaussWin[static_cast<size_t>(i)] * Cplx(std::cos(a), std::sin(a));
            }
            spec[static_cast<size_t>(b)] = sum;
        }
        std::vector<PeakEst> peaks;
        if (correct)
        {
            double mx = 0.0;
            for (int b = 0; b < kDisplayBins; ++b)
                mx = std::max(mx, std::abs(spec[static_cast<size_t>(b)]));
            for (int b = 1; b < kDisplayBins - 1; ++b)
            {
                const double m = std::abs(spec[static_cast<size_t>(b)]);
                if (m < 0.08 * mx)
                    continue;
                if (m < std::abs(spec[static_cast<size_t>(b - 1)]) || m < std::abs(spec[static_cast<size_t>(b + 1)]))
                    continue;
                PeakEst p = estimatePeak(frame, b, m);
                if (p.valid)
                    peaks.push_back(p);
            }
            lastValidPeaks = static_cast<int>(peaks.size());
            lastBetaFund = 0.0;
            double bestD = 1e9;
            for (auto& p : peaks)
            {
                const double d = std::abs(p.f0 - 220.0);
                if (d < bestD)
                {
                    bestD = d;
                    lastBetaFund = p.beta;
                }
            }
            betaFundTrace.push_back(lastBetaFund);
        }
        for (int b = 0; b < kDisplayBins; ++b)
        {
            Cplx X = spec[static_cast<size_t>(b)];
            if (correct && !peaks.empty())
            {
                // Nearest peak in log frequency; apply its de-sweep
                // transfer only inside the peak's region (+-3 sigma_f).
                const PeakEst* best = nullptr;
                double bestD = 1e9;
                for (auto& p : peaks)
                {
                    const double d = std::abs(std::log(bandFreq(b) / p.f0));
                    if (d < bestD)
                    {
                        bestD = d;
                        best = &p;
                    }
                }
                if (best != nullptr)
                {
                    const double sigF = std::sqrt((kGaussAlpha * kGaussAlpha + best->beta * best->beta) / (2.0 * 3.141592653589793 * kGaussAlpha));
                    if (std::abs(bandFreq(b) - best->f0) <= 3.0 * sigF)
                        X *= desweepTransfer(bandFreq(b), *best);
                }
            }
            const float mag = static_cast<float>(std::abs(X)) * 0.02f;
            float& e = env[static_cast<size_t>(b)];
            e = mag > e ? 0.24f * e + 0.76f * mag : 0.92f * e + 0.08f * mag;
            if (!fr)
                frozen[static_cast<size_t>(b)] = 0.998f * frozen[static_cast<size_t>(b)] + 0.002f * e;
        }
    }
};

void verifyBeta()
{
    // Single-frame estimator check on known chirps. Frame centred at
    // t=0; x(tau) = sin(2 pi (f0 tau + beta tau^2/2)) * exp(2 pi lambda tau).
    auto chirpFrame = [](double f0, double beta, double lambda) {
        std::vector<float> fr(kFftSize);
        for (int i = 0; i < kFftSize; ++i)
        {
            const double tau = (static_cast<double>(i) - (kFftSize - 1) / 2.0) / kSr;
            const double ph = 2.0 * 3.141592653589793 * (f0 * tau + 0.5 * beta * tau * tau);
            fr[static_cast<size_t>(i)] = static_cast<float>(std::sin(ph) * std::exp(2.0 * 3.141592653589793 * lambda * tau));
        }
        return fr;
    };
    auto check = [&](const char* name, double f0, double beta, double lambda) {
        const std::vector<float> fr = chirpFrame(f0, beta, lambda);
        int pk = 0;
        double mx = -1.0;
        for (int b = 0; b < kDisplayBins; ++b)
        {
            const double m = std::abs(gaussDft(fr, bandFreq(b)));
            if (m > mx)
            {
                mx = m;
                pk = b;
            }
        }
        const PeakEst p = estimatePeak(fr, pk, mx);
        std::printf("  %-34s true beta=%+7.1f f0=%.1f lam=%+.1f | est beta=%+8.1f f0=%7.2f lam=%+6.2f valid=%d\n",
                    name, beta, f0, lambda, p.beta, p.f0, p.lambda, static_cast<int>(p.valid));
    };
    std::printf("beta recovery on synthetic swept sinusoids (single Gaussian frame):\n");
    check("stationary 1000 Hz", 1000.0, 0.0, 0.0);
    check("chirp 1000 Hz beta=+400", 1000.0, 400.0, 0.0);
    check("chirp 1000 Hz beta=-400", 1000.0, -400.0, 0.0);
    check("chirp 1000 Hz beta=+5000", 1000.0, 5000.0, 0.0);
    check("chirp 1000 Hz beta=+400 lam=+3", 1000.0, 400.0, 3.0);
    check("vibrato-scale 220 Hz beta=+241", 220.0, 241.0, 0.0);
}

void runCaseCorrected(const char* name, double f0, double vibHz, double depthSt)
{
    CorrectedAnalyser unc(false), cor(true);
    const int total = static_cast<int>(2.0 * kSr);
    const int freezeAt = static_cast<int>(1.0 * kSr);
    std::vector<double> rFrameUnc, rFrameCor;
    for (int n = 0; n < total; ++n)
    {
        const double t = n / kSr;
        const float s = vowelSample(t, f0, vibHz, depthSt);
        const bool fr = n >= freezeAt;
        unc.feed(s, fr);
        cor.feed(s, fr);
        if (fr && (n % 1102 == 0) && rFrameUnc.size() < 8)
        {
            rFrameUnc.push_back(measure(unc.env).ratioDb);
            rFrameCor.push_back(measure(cor.env).ratioDb);
        }
    }
    const Snapshot sU = measure(unc.frozen);
    const Snapshot sC = measure(cor.frozen);
    double mU = 0.0, mC = 0.0;
    for (size_t k = 0; k < rFrameUnc.size(); ++k)
    {
        mU += rFrameUnc[k];
        mC += rFrameCor[k];
    }
    mU /= std::max<size_t>(rFrameUnc.size(), 1);
    mC /= std::max<size_t>(rFrameCor.size(), 1);
    // Beta trace over the second half of the run (post-freeze frames).
    double bMean = 0.0, bMax = 0.0;
    size_t bN = 0;
    const size_t half = cor.betaFundTrace.size() / 2;
    for (size_t k = half; k < cor.betaFundTrace.size(); ++k)
    {
        bMean += std::abs(cor.betaFundTrace[k]);
        bMax = std::max(bMax, std::abs(cor.betaFundTrace[k]));
        ++bN;
    }
    bMean /= std::max<size_t>(bN, 1);
    std::printf("%-28s frozen R: gauss %+.2f dB -> corr %+.2f dB (d=%+.2f) | per-frame R: %+.2f -> %+.2f (d=%+.2f) | |beta_fund| mean %.0f max %.0f Hz/s, %d valid peaks/frame\n",
                name, sU.ratioDb, sC.ratioDb, sC.ratioDb - sU.ratioDb, mU, mC, mC - mU, bMean, bMax, cor.lastValidPeaks);
}

// --- Bench cross-check renders (Step 2, ARC-AudioBench freeze-blur) ---
// Render a frozen envelope to audio as a bank of sinusoids at the 128
// log-band frequencies with the frozen magnitudes, so the Bench
// freeze-blur test (WAV in, per-harmonic peak-vs-adjacent blur) can
// measure the same frozen snapshot the pilot's R metric measures. The
// resynthesis is a measurement instrument, like the Bench's own
// simulate_freeze -- it is NOT the Ghost resynthesis (which is broken;
// see Tests/DSPVerify.cpp testGhostResynthesis). Normalised to peak 0.5.
void writeWavMono16(const char* path, const std::vector<float>& x)
{
    std::FILE* f = std::fopen(path, "wb");
    if (f == nullptr)
    {
        std::printf("  cannot write %s\n", path);
        return;
    }
    const std::uint32_t n = static_cast<std::uint32_t>(x.size());
    const std::uint32_t sr = 44100;
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
    w32(sr);
    w32(sr * 2);
    w16(2);
    w16(16);
    std::fwrite("data", 1, 4, f);
    w32(bytes);
    for (float v : x)
    {
        const float c = std::max(-1.0f, std::min(1.0f, v));
        w16(static_cast<std::uint16_t>(static_cast<std::int16_t>(std::lrint(c * 32767.0f))));
    }
    std::fclose(f);
}

std::vector<float> renderFrozenPartials(const std::vector<float>& frozen, double seconds)
{
    const int n = static_cast<int>(seconds * kSr);
    std::vector<float> out(static_cast<size_t>(n), 0.0f);
    double peak = 0.0;
    for (int i = 0; i < n; ++i)
    {
        double s = 0.0;
        for (int b = 0; b < kDisplayBins; ++b)
            s += static_cast<double>(frozen[static_cast<size_t>(b)])
               * std::sin(2.0 * 3.141592653589793 * bandFreq(b) * i / kSr);
        out[static_cast<size_t>(i)] = static_cast<float>(s);
        peak = std::max(peak, std::abs(s));
    }
    const float g = peak > 0.0 ? static_cast<float>(0.5 / peak) : 0.0f;
    for (float& v : out)
        v *= g;
    return out;
}

void renderFreezeWavs()
{
    // Same protocol as the pilot runs: 2 s of vowel, freeze at 1 s.
    // F0=220, stationary control + 6 Hz vibrato +-0.5 st.
    const char* dir = "/home/hatch/workspace/vox8-bench/freeze/";
    auto run = [&](double f0, double depthSt, const char* tag) {
        GhostAnalyser hann;
        CorrectedAnalyser gauss(false), corr(true);
        const int total = static_cast<int>(2.0 * kSr);
        const int freezeAt = static_cast<int>(1.0 * kSr);
        for (int n = 0; n < total; ++n)
        {
            const double t = n / kSr;
            const float s = vowelSample(t, f0, 6.0, depthSt);
            const bool fr = n >= freezeAt;
            hann.feed(s, fr);
            gauss.feed(s, fr);
            corr.feed(s, fr);
        }
        char path[256];
        std::snprintf(path, sizeof(path), "%sfrozen_hann_%s.wav", dir, tag);
        writeWavMono16(path, renderFrozenPartials(hann.frozen, 1.0));
        std::snprintf(path, sizeof(path), "%sfrozen_gauss_%s.wav", dir, tag);
        writeWavMono16(path, renderFrozenPartials(gauss.frozen, 1.0));
        std::snprintf(path, sizeof(path), "%sfrozen_corr_%s.wav", dir, tag);
        writeWavMono16(path, renderFrozenPartials(corr.frozen, 1.0));
        std::printf("  rendered frozen snapshots (%s): hann / gauss / corr\n", tag);
    };
    run(220.0, 0.0, "static");
    run(220.0, 0.5, "vibrato");
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
    std::printf("\n--- Step 2 second half: WASPAA beta/lambda correction ---\n");
    std::printf("Gaussian window, eq. 9-11 fit per peak, eq. 15 de-sweep (beta_y=0).\n");
    std::printf("Decision rule: implement only if corrected frozen R improves by >= 3 dB\n");
    std::printf("on the +-0.5 st signal, without degrading the stationary control by > 0.5 dB.\n\n");
    verifyBeta();
    std::printf("\n");
    runCaseCorrected("stationary control F0=220", 220.0, 6.0, 0.0);
    runCaseCorrected("vibrato +-0.5 st F0=220", 220.0, 6.0, 0.5);
    runCaseCorrected("vibrato +-1.0 st F0=220", 220.0, 6.0, 1.0);
    runCaseCorrected("stationary control F0=250", 250.0, 6.0, 0.0);
    runCaseCorrected("vibrato +-0.5 st F0=250", 250.0, 6.0, 0.5);
    runCaseCorrected("vibrato +-1.0 st F0=250", 250.0, 6.0, 1.0);
    std::printf("\n--- Bench cross-check renders (frozen snapshots -> WAV) ---\n");
    renderFreezeWavs();
    return 0;
}
