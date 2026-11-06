// FreeVox8FFT.h — self-contained radix-2 FFT, framework-free.
//
// Plain C++ (no JUCE, no iPlug2): the Ghost resynthesis redesign
// (FFT cross-synthesis, Vox8 paper §6 rank 9) needs an FFT, and the
// DSP core is framework-free by design — the same core must survive a
// move to iPlug2 (Vox8) or a language of our own. So the FFT lives
// here, not in juce::dsp::FFT. Iterative in-place radix-2 DIT,
// precomputed bit-reversal + twiddles (allocated once in the
// constructor — never on the audio thread). forward() and inverse()
// are in-place; inverse() includes the 1/N scale, so
// inverse(forward(x)) == x.
//
// Verified by Tests/DSPVerify.cpp testFFT(): impulse -> flat spectrum,
// sine at a bin -> single peak, round-trip identity to < 1e-5.
#pragma once

#include <cmath>
#include <complex>
#include <vector>

class FreeVox8FFT
{
public:
    explicit FreeVox8FFT(int size)
        : n(size)
    {
        // size must be a power of two >= 2.
        logN = 0;
        while ((1 << logN) < n)
            ++logN;
        rev.resize((size_t) n);
        for (int i = 0; i < n; ++i)
        {
            int r = 0;
            for (int b = 0; b < logN; ++b)
                if ((i >> b) & 1)
                    r |= 1 << (logN - 1 - b);
            rev[(size_t) i] = r;
        }
        // Forward twiddles W_N^k = exp(-2*pi*i*k/N), k = 0..N/2-1.
        tw.resize((size_t) (n / 2));
        const double pi = 3.14159265358979323846;
        for (int k = 0; k < n / 2; ++k)
        {
            const double a = -2.0 * pi * (double) k / (double) n;
            tw[(size_t) k] = std::complex<float>((float) std::cos(a), (float) std::sin(a));
        }
    }

    int size() const noexcept { return n; }

    // Forward FFT: X[k] = sum_n x[n] * exp(-2*pi*i*k*n/N). In-place.
    void forward(std::complex<float>* data) const { transform(data, false); }

    // Inverse FFT: x[n] = (1/N) * sum_k X[k] * exp(+2*pi*i*k*n/N).
    // In-place, includes the 1/N scale.
    void inverse(std::complex<float>* data) const { transform(data, true); }

private:
    int n = 0, logN = 0;
    std::vector<int> rev;
    std::vector<std::complex<float>> tw;

    void transform(std::complex<float>* a, bool inverse) const
    {
        for (int i = 0; i < n; ++i)
        {
            const int r = rev[(size_t) i];
            if (r > i)
                std::swap(a[i], a[r]);
        }
        for (int len = 2; len <= n; len <<= 1)
        {
            const int half = len / 2;
            const int step = n / len;
            for (int i = 0; i < n; i += len)
            {
                for (int j = 0; j < half; ++j)
                {
                    std::complex<float> w = tw[(size_t) (j * step)];
                    if (inverse)
                        w = std::conj(w);
                    const std::complex<float> u = a[i + j];
                    const std::complex<float> v = a[i + j + half] * w;
                    a[i + j] = u + v;
                    a[i + j + half] = u - v;
                }
            }
        }
        if (inverse)
        {
            const float s = 1.0f / (float) n;
            for (int i = 0; i < n; ++i)
                a[i] *= s;
        }
    }
};
