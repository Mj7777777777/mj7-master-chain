// MJ7 Master Chain - surechantillonnage x4 par filtres demi-bande RIF (phase lineaire, latence entiere).
#pragma once
#include "Common.h"

namespace mj7
{
/** Filtre demi-bande RIF (sinus cardinal fenetre de Kaiser), longueur impaire. */
inline std::vector<float> designHalfband (int length, float beta)
{
    auto bessel0 = [] (double x) { double s = 1.0, t = 1.0; for (int k = 1; k < 30; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
    std::vector<float> h ((size_t) length);
    const int M = (length - 1) / 2; double sum = 0.0;
    for (int n = 0; n < length; ++n)
    {
        const double x = n - M;
        const double sinc = x == 0.0 ? 0.5 : std::sin (3.141592653589793 * x / 2.0) / (3.141592653589793 * x);
        const double r = (double) (n - M) / (double) M;
        const double w = bessel0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / bessel0 (beta);
        h[(size_t) n] = (float) (sinc * w); sum += sinc * w;
    }
    for (auto& v : h) v = (float) (v / sum);
    return h;
}

/** Une etape x2 : interpolation (up) et decimation (down). */
class Halfband2x
{
public:
    void prepare (int length, float beta)
    {
        h = designHalfband (length, beta); L = length;
        size = nextPow2 (L + 2); buf.assign ((size_t) size, 0.0f); w = 0;
    }
    void reset() noexcept { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
    /** Un echantillon en entree, deux en sortie. */
    void up (float x, float* out2) noexcept
    {
        push (2.0f * x); out2[0] = conv();
        push (0.0f);     out2[1] = conv();
    }
    /** Deux echantillons en entree, un en sortie. */
    float down (float a, float b) noexcept { push (a); const float y = conv(); push (b); return y; }
private:
    void push (float v) noexcept { buf[(size_t) w] = v; w = (w + 1) & (size - 1); }
    float conv() const noexcept
    {
        float s = 0.0f; const int m = size - 1;
        for (int k = 0; k < L; ++k) s += h[(size_t) k] * buf[(size_t) ((w - 1 - k) & m)];
        return s;
    }
    std::vector<float> h, buf; int L = 63, size = 64, w = 0;
};

/** Surechantillonnage x4 d'un canal : x2 (63 points) puis x2 (33 points). */
class Oversampler4x
{
public:
    void prepare()
    {
        s1u.prepare (63, 8.0f); s2u.prepare (33, 7.0f); s2d.prepare (33, 7.0f); s1d.prepare (63, 8.0f);
        // latence aller-retour mesuree avec une impulsion (entiere par construction : 31 + 8 = 39 echantillons)
        reset(); int best = 0; float bv = 0.0f; float buf4[4];
        for (int i = 0; i < 200; ++i) { up (i == 0 ? 1.0f : 0.0f, buf4); const float y = std::abs (down (buf4)); if (y > bv) { bv = y; best = i; } }
        lat = best; reset();
    }
    void reset() noexcept { s1u.reset(); s2u.reset(); s2d.reset(); s1d.reset(); }
    int latency() const noexcept { return lat; }
    void up (float x, float* out4) noexcept
    {
        float a[2]; s1u.up (x, a);
        s2u.up (a[0], out4); s2u.up (a[1], out4 + 2);
    }
    float down (const float* in4) noexcept
    {
        const float a = s2d.down (in4[0], in4[1]), b = s2d.down (in4[2], in4[3]);
        return s1d.down (a, b);
    }
private:
    Halfband2x s1u, s2u, s2d, s1d; int lat = 39;
};
} // namespace mj7
