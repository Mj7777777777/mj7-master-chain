// MJ7 Master Chain - FFT et petits outils statistiques (sans JUCE).
#pragma once
#include "Common.h"

namespace mj7::spec
{
inline void fft (std::vector<float>& re, std::vector<float>& im)
{
    const int n = (int) re.size();
    for (int i = 1, j = 0; i < n; ++i)
    {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { std::swap (re[(size_t) i], re[(size_t) j]); std::swap (im[(size_t) i], im[(size_t) j]); }
    }
    for (int len = 2; len <= n; len <<= 1)
    {
        const double ang = -2.0 * 3.141592653589793 / (double) len;
        for (int i = 0; i < n; i += len)
            for (int k = 0; k < len / 2; ++k)
            {
                const float cr = (float) std::cos (ang * k), ci = (float) std::sin (ang * k);
                const size_t a = (size_t) (i + k), b = (size_t) (i + k + len / 2);
                const float tr = re[b] * cr - im[b] * ci, ti = re[b] * ci + im[b] * cr;
                re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
            }
    }
}
inline float percentile (std::vector<float> v, float p)
{
    if (v.empty()) return -120.0f;
    std::sort (v.begin(), v.end());
    return v[(size_t) clampf (p * (float) (v.size() - 1), 0.0f, (float) (v.size() - 1))];
}
inline float powDb (double p) { return (float) (10.0 * std::log10 (std::max (p, 1.0e-14))); }
} // namespace mj7::spec
