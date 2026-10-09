// MJ7 Master Chain - analyse d'un extrait du mix et calage sur la cible de loudness (sans JUCE).
#pragma once
#include "Master.h"
#include "Spectrum.h"
#include <memory>

namespace mj7
{
/** Equilibre vise pour un genre : niveau moyen de chaque zone en dB par rapport aux mediums (400 Hz - 2 kHz).
    Points de depart regles a la main, a affiner a l'oreille. */
struct GenreTarget { float sub = 8, bass = 9, lowMid = 5, pres = -7, air = -15; };

struct MasterAnalysis
{
    bool valid = false;
    // --- mesures du mix d'origine ---
    float inLufs = -120, inTp = -120, inPlr = 0, corrLow = 1, sideDb = -20;
    float bands[5] = {};                 // grave-sub, basses, bas-medium, presence, air (dB relatifs aux mediums)
    // --- reglages proposes ---
    float lowG = 0, mudG = 0, presG = 0, airG = 0, monoBelow = 100, width = 100, thr[3] = { -20, -20, -20 }, glueThr = -18, limGain = 0;
    // --- resultat prevu par simulation ---
    float outLufs = -120, outTp = -120, limAvgGr = 0, limMaxGr = 0;
    bool reached = true, heavy = false, alreadyLoud = false;
};

namespace detail
{
inline float loudnessOf (const std::vector<float>& L, const std::vector<float>& R, float sr, float* tpOut = nullptr)
{
    auto m = std::make_unique<LoudnessMeter>(); m->prepare (sr);
    m->process (L.data(), R.data(), (int) L.size());
    if (tpOut != nullptr)
    {
        auto t = std::make_unique<TruePeakMeter>(); t->prepare();
        t->process (L.data(), R.data(), (int) L.size());
        *tpOut = gainToDb (t->maxPeak);
    }
    return m->integrated();
}
}

/** base : reglages actuels (style choisi). Les corrections s'ajoutent a l'EQ du style. */
inline MasterAnalysis analyseMaster (const float* L, const float* R, int n, float sr, const GenreTarget& t,
                                     float targetLufs, const MasterParams& base)
{
    using namespace spec;
    MasterAnalysis r;
    const int N = nextPow2 ((int) (sr * 0.17f)), hop = N / 2, half = N / 2;
    if (n < 4 * N) return r;
    std::vector<float> vl (L, L + n), vr (R, R + n);
    r.inLufs = detail::loudnessOf (vl, vr, sr, &r.inTp);
    if (r.inLufs < -60.0f) return r;
    r.valid = true;

    // --- equilibre spectral (moyenne des trames actives, signal mono) ---
    {
        std::vector<float> win ((size_t) N), re ((size_t) N), im ((size_t) N);
        for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * kPi * (float) i / (float) N);
        std::vector<double> avg ((size_t) half, 0.0); std::vector<float> frameDb;
        const int frames = (n - N) / hop + 1;
        for (int f = 0; f < frames; ++f)
        {
            double e = 0.0; for (int i = 0; i < N; ++i) { const float m = 0.5f * (L[f * hop + i] + R[f * hop + i]); e += (double) m * m; }
            frameDb.push_back (powDb (e / N));
        }
        const float act = *std::max_element (frameDb.begin(), frameDb.end()) - 25.0f;
        for (int f = 0; f < frames; ++f)
        {
            if (frameDb[(size_t) f] < act) continue;
            for (int i = 0; i < N; ++i) { re[(size_t) i] = 0.5f * (L[f * hop + i] + R[f * hop + i]) * win[(size_t) i]; im[(size_t) i] = 0.0f; }
            fft (re, im);
            for (int k = 1; k < half; ++k) avg[(size_t) k] += (double) re[(size_t) k] * re[(size_t) k] + (double) im[(size_t) k] * im[(size_t) k];
        }
        const float binHz = sr / (float) N;
        auto band = [&] (float f1, float f2)
        {
            const int a = std::clamp ((int) (f1 / binHz), 1, half - 1), b = std::clamp ((int) (f2 / binHz), a, half - 1);
            double s = 0.0; for (int k = a; k <= b; ++k) s += avg[(size_t) k];
            return powDb (s / (double) (b - a + 1));
        };
        const float mid = band (400.0f, 2000.0f);
        r.bands[0] = band (30.0f, 60.0f) - mid;    r.bands[1] = band (60.0f, 150.0f) - mid;
        r.bands[2] = band (150.0f, 400.0f) - mid;  r.bands[3] = band (2000.0f, 6000.0f) - mid;
        r.bands[4] = band (6000.0f, std::min (16000.0f, 0.45f * sr)) - mid;
        const float k = 0.6f;
        r.lowG  = clampf (k * (0.5f * (t.sub + t.bass) - 0.5f * (r.bands[0] + r.bands[1])), -3.0f, 3.0f);
        r.mudG  = -clampf (k * (r.bands[2] - t.lowMid), 0.0f, 3.0f);
        r.presG = clampf (k * (t.pres - r.bands[3]), -3.0f, 3.0f);
        r.airG  = clampf (k * (t.air - r.bands[4]), -3.0f, 3.0f);
    }

    // --- image stereo : correlation des graves, part du cote ---
    {
        LR4 lpL, lpR; lpL.setLow (120.0f, sr); lpR.setLow (120.0f, sr);
        double ll = 0, rr = 0, lr = 0, em = 0, es = 0;
        for (int i = 0; i < n; ++i)
        {
            const float a = lpL.process (L[i]), b = lpR.process (R[i]);
            ll += (double) a * a; rr += (double) b * b; lr += (double) a * b;
            const float m = 0.5f * (L[i] + R[i]), s = 0.5f * (L[i] - R[i]); em += (double) m * m; es += (double) s * s;
        }
        r.corrLow = (float) (lr / std::sqrt (std::max (1.0e-12, ll * rr)));
        r.sideDb = powDb (es / std::max (1.0e-12, em));
        r.monoBelow = r.corrLow < 0.9f ? 120.0f : base.monoBelow;
        r.width = r.sideDb < -20.0f ? 115.0f : (r.sideDb > -6.0f ? 90.0f : base.width);
    }

    // --- seuils des compresseurs : ~2 dB de reduction sur les passages forts ---
    {
        Crossover3 xo; xo.prepare (sr); xo.set (base.xLow, base.xHigh, true);
        const int w = std::max (1, (int) (0.05f * sr));
        std::vector<float> pk[4]; float cur[4] = {}; int c = 0;
        const float g = dbToGain (base.inGain);
        for (int i = 0; i < n; ++i)
        {
            float b[2][3];
            xo.split (0, L[i] * g, b[0][0], b[0][1], b[0][2]); xo.split (1, R[i] * g, b[1][0], b[1][1], b[1][2]);
            for (int k = 0; k < 3; ++k) cur[k] = std::max (cur[k], std::max (std::abs (b[0][k]), std::abs (b[1][k])));
            cur[3] = std::max (cur[3], std::max (std::abs (L[i] * g), std::abs (R[i] * g)));
            if (++c >= w) { for (int k = 0; k < 4; ++k) { pk[k].push_back (gainToDb (cur[k])); cur[k] = 0.0f; } c = 0; }
        }
        for (int k = 0; k < 3; ++k)
            r.thr[k] = clampf (percentile (pk[k], 0.9f) - 2.0f / (1.0f - 1.0f / std::max (1.2f, base.ratio[k])), -40.0f, 0.0f);
        r.glueThr = clampf (percentile (pk[3], 0.9f) - 1.5f / (1.0f - 1.0f / std::max (1.2f, base.glueRatio)), -40.0f, 0.0f);
    }

    // --- simulation : gain du limiteur qui atteint la cible ---
    {
        MasterParams q = base;
        q.lowG = clampf (base.lowG + r.lowG, -6.0f, 6.0f); q.mudG = clampf (base.mudG + r.mudG, -6.0f, 3.0f);
        q.presG = clampf (base.presG + r.presG, -6.0f, 6.0f); q.airG = clampf (base.airG + r.airG, -6.0f, 6.0f);
        q.monoBelow = r.monoBelow; q.width = r.width;
        for (int k = 0; k < 3; ++k) q.thr[k] = r.thr[k];
        q.glueThr = r.glueThr; q.limGain = 0.0f;

        auto eng = std::make_unique<MasterEngine>(); eng->prepare (sr); eng->setParams (q); eng->reset();
        std::vector<float> pl (L, L + n), pr (R, R + n);
        for (int i = 0; i < n; i += MasterEngine::kMaxBlock) eng->processPre (pl.data() + i, pr.data() + i, std::min (MasterEngine::kMaxBlock, n - i));
        const float preLufs = detail::loudnessOf (pl, pr, sr);

        std::vector<float> ol, orr;
        auto eval = [&] (float g, float* tp, float* avgGr, float* maxGr)
        {
            auto e = std::make_unique<MasterEngine>(); MasterParams qq = q; qq.limGain = g;
            e->prepare (sr); e->setParams (qq); e->reset();
            ol = pl; orr = pr; double sumGr = 0.0; int cnt = 0; float mx = 0.0f;
            for (int i = 0; i < n; i += MasterEngine::kMaxBlock)
            {
                const int len = std::min (MasterEngine::kMaxBlock, n - i);
                e->processLimiter (ol.data() + i, orr.data() + i, len);
                sumGr += e->limiterAvgGr(); ++cnt; mx = std::min (mx, e->limiterGr());
            }
            *avgGr = cnt > 0 ? (float) (sumGr / cnt) : 0.0f; *maxGr = mx;
            return detail::loudnessOf (ol, orr, sr, tp);
        };

        float g = clampf (targetLufs - preLufs, -24.0f, 24.0f), tp = 0, avgGr = 0, maxGr = 0;
        float out = eval (g, &tp, &avgGr, &maxGr);
        float gPrev = g, outPrev = out;
        for (int it = 0; it < 8 && std::abs (out - targetLufs) > 0.15f; ++it)
        {
            float next;
            if (it == 0 || std::abs (out - outPrev) < 1.0e-3f) next = g + (targetLufs - out);
            else next = g + (targetLufs - out) * (g - gPrev) / (out - outPrev);           // secante
            next = clampf (next, -24.0f, 24.0f);
            if (std::abs (next - g) < 0.01f) break;
            gPrev = g; outPrev = out; g = next;
            out = eval (g, &tp, &avgGr, &maxGr);
        }
        r.limGain = g; r.outLufs = out; r.outTp = tp; r.limAvgGr = avgGr; r.limMaxGr = maxGr;
        r.reached = std::abs (out - targetLufs) < 0.5f;
        r.heavy = avgGr < -4.0f;
        r.alreadyLoud = preLufs > targetLufs + 0.5f;
    }
    r.inPlr = r.inTp - r.inLufs;
    return r;
}
} // namespace mj7
