// MJ7 Master Chain - mesure de loudness ITU-R BS.1770 / EBU R128 et true peak.
#pragma once
#include "Oversampler.h"

namespace mj7
{
/** Loudness momentanee (400 ms), court terme (3 s) et integree (portes absolue -70 et relative -10 LU).
    L'integre utilise un histogramme a 0,1 dB : memoire fixe, aucune allocation pendant la lecture. */
class LoudnessMeter
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate; sub = std::max (1, (int) std::lround (0.1f * sr));
        for (int c = 0; c < 2; ++c)
        {
            const double fs = sr;
            double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
            double K = std::tan (3.141592653589793 * f0 / fs), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
            double a0 = 1.0 + K / Q + K * K;
            shelf[c].set ((float) ((Vh + Vb * K / Q + K * K) / a0), (float) (2.0 * (K * K - Vh) / a0), (float) ((Vh - Vb * K / Q + K * K) / a0),
                          1.0f, (float) (2.0 * (K * K - 1.0) / a0), (float) ((1.0 - K / Q + K * K) / a0));
            f0 = 38.13547087602444; Q = 0.5003270373238773; K = std::tan (3.141592653589793 * f0 / fs);
            a0 = 1.0 + K / Q + K * K;
            hp[c].set (1.0f, -2.0f, 1.0f, 1.0f, (float) (2.0 * (K * K - 1.0) / a0), (float) ((1.0 - K / Q + K * K) / a0));
        }
        reset();
    }
    void reset() noexcept
    {
        for (int c = 0; c < 2; ++c) { shelf[c].reset(); hp[c].reset(); }
        std::fill (std::begin (ring), std::end (ring), 0.0); acc = 0.0; count = 0; filled = 0; ri = 0;
        std::fill (std::begin (histN), std::end (histN), 0.0); std::fill (std::begin (histE), std::end (histE), 0.0);
        momentary = shortTerm = -120.0f;
    }
    void resetIntegrated() noexcept { std::fill (std::begin (histN), std::end (histN), 0.0); std::fill (std::begin (histE), std::end (histE), 0.0); }

    void process (const float* L, const float* R, int n) noexcept
    {
        for (int i = 0; i < n; ++i)
        {
            const float l = hp[0].process (shelf[0].process (L[i])), r = hp[1].process (shelf[1].process (R[i]));
            acc += (double) l * l + (double) r * r;
            if (++count >= sub)
            {
                ring[ri] = acc / (double) sub; ri = (ri + 1) % 30; acc = 0.0; count = 0; filled = std::min (filled + 1, 30);
                double m = 0.0, s = 0.0;
                for (int k = 0; k < 4; ++k) m += ring[(ri + 29 - k) % 30];
                for (int k = 0; k < 30; ++k) s += ring[k];
                m /= 4.0; s /= 30.0;
                momentary = filled >= 4 ? toLufs (m) : -120.0f;
                shortTerm = filled >= 30 ? toLufs (s) : (filled >= 4 ? toLufs (s * 30.0 / filled) : -120.0f);
                if (filled >= 4)                                     // bloc de 400 ms toutes les 100 ms
                {
                    const float lu = toLufs (m);
                    if (lu > -70.0f) { const int b = std::clamp ((int) ((lu + 70.0f) * 10.0f), 0, kBins - 1); histN[b] += 1.0; histE[b] += m; }
                }
            }
        }
    }
    float integrated() const noexcept
    {
        double n = 0.0, e = 0.0;
        for (int b = 0; b < kBins; ++b) { n += histN[b]; e += histE[b]; }
        if (n <= 0.0) return -120.0f;
        const float gate = toLufs (e / n) - 10.0f;
        n = 0.0; e = 0.0;
        for (int b = 0; b < kBins; ++b)
            if (-70.0f + ((float) b + 0.5f) * 0.1f > gate) { n += histN[b]; e += histE[b]; }
        return n > 0.0 ? toLufs (e / n) : -120.0f;
    }
    float momentary = -120.0f, shortTerm = -120.0f;

private:
    static float toLufs (double ms) noexcept { return (float) (-0.691 + 10.0 * std::log10 (std::max (ms, 1.0e-12))); }
    static constexpr int kBins = 800;
    float sr = 48000.0f; int sub = 4800, count = 0, filled = 0, ri = 0; double acc = 0.0, ring[30] = {};
    double histN[kBins] = {}, histE[kBins] = {};
    Biquad shelf[2], hp[2];
};

/** Crete vraie (inter-echantillons) par surechantillonnage x4. */
class TruePeakMeter
{
public:
    void prepare() { os[0].prepare(); os[1].prepare(); reset(); }
    void reset() noexcept { os[0].reset(); os[1].reset(); maxPeak = 0.0f; blockPeak = 0.0f; }
    void process (const float* L, const float* R, int n) noexcept
    {
        float pk = 0.0f, buf[4];
        for (int i = 0; i < n; ++i)
        {
            os[0].up (L[i], buf); for (float v : buf) pk = std::max (pk, std::abs (v));
            os[1].up (R[i], buf); for (float v : buf) pk = std::max (pk, std::abs (v));
        }
        blockPeak = pk; maxPeak = std::max (maxPeak, pk);
    }
    float maxPeak = 0.0f, blockPeak = 0.0f;
private:
    Oversampler4x os[2];
};
} // namespace mj7
