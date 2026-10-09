// MJ7 Master Chain - moteur de mastering (sans JUCE) : utilisable dans le plugin et pour la simulation de l'analyse.
#pragma once
#include "Dynamics.h"
#include "Effects.h"
#include "Loudness.h"

namespace mj7
{
/** Filtre Linkwitz-Riley 4e ordre (deux Butterworth en cascade) : passe-bas + passe-haut = passe-tout. */
struct LR4
{
    Biquad a, b;
    void setLow (float f, float sr) noexcept  { a.setLowpass (f, 0.70710678f, sr);  b.copyCoefs (a); }
    void setHigh (float f, float sr) noexcept { a.setHighpass (f, 0.70710678f, sr); b.copyCoefs (a); }
    void reset() noexcept { a.reset(); b.reset(); }
    float process (float x) noexcept { return b.process (a.process (x)); }
};

/** Separation en 3 bandes a phase coherente (la somme des bandes est un passe-tout). */
class Crossover3
{
public:
    void prepare (float sampleRate) { sr = sampleRate; set (150.0f, 4000.0f, true); }
    void set (float lowHz, float highHz, bool force = false) noexcept
    {
        highHz = std::max (highHz, lowHz * 2.0f);
        if (! force && lowHz == f1 && highHz == f2) return;
        f1 = lowHz; f2 = highHz;
        for (int c = 0; c < 2; ++c)
        {
            lp1[c].setLow (f1, sr); hp1[c].setHigh (f1, sr);
            lp2[c].setLow (f2, sr); hp2[c].setHigh (f2, sr);
            apL[c].setLow (f2, sr); apH[c].setHigh (f2, sr);
        }
    }
    void reset() noexcept { for (int c = 0; c < 2; ++c) { lp1[c].reset(); hp1[c].reset(); lp2[c].reset(); hp2[c].reset(); apL[c].reset(); apH[c].reset(); } }
    void split (int ch, float x, float& low, float& mid, float& high) noexcept
    {
        const float l = lp1[ch].process (x), rest = hp1[ch].process (x);
        mid = lp2[ch].process (rest); high = hp2[ch].process (rest);
        low = apL[ch].process (l) + apH[ch].process (l);     // meme rotation de phase que mid + high
    }
private:
    float sr = 48000.0f, f1 = 0.0f, f2 = 0.0f;
    LR4 lp1[2], hp1[2], lp2[2], hp2[2], apL[2], apH[2];
};

/** Limiteur true peak : detection sur le signal surechantillonne x4, anticipation 1,5 ms, retour reglable. */
class TruePeakLimiter
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        det[0].prepare(); det[1].prepare();
        D = 20;                                              // retard de la detection (interpolation x4)
        la = std::max (8, (int) std::lround (0.0015f * sr));
        H = la + 3;
        dl.prepare (D + la + 2); dr.prepare (D + la + 2); dl.setDelay (D + la); dr.setDelay (D + la);
        req.assign ((size_t) H, 1.0f); avg.assign ((size_t) la, 1.0f);
        dq.assign ((size_t) H + 1, 0); reset();
    }
    int latency() const noexcept { return D + la; }
    void reset() noexcept
    {
        det[0].reset(); det[1].reset(); dl.reset(); dr.reset();
        std::fill (req.begin(), req.end(), 1.0f); std::fill (avg.begin(), avg.end(), 1.0f);
        idx = 0; ap = 0; avgSum = (float) la; rel = 1.0f; head = tail = 0;
    }
    void setParams (float ceilingDb, float releaseMs, bool enabled_) noexcept
    {
        ceiling = dbToGain (ceilingDb); enabled = enabled_; relCoef = 1.0f - coefFromMs (releaseMs, sr);
    }
    void process (float* L, float* R, int n) noexcept
    {
        float worst = 1.0f, buf[4]; double sumGr = 0.0;
        for (int i = 0; i < n; ++i)
        {
            float tp = 0.0f;
            det[0].up (L[i], buf); for (float v : buf) tp = std::max (tp, std::abs (v));
            det[1].up (R[i], buf); for (float v : buf) tp = std::max (tp, std::abs (v));
            const float g = (enabled && tp > ceiling) ? ceiling / tp : 1.0f;

            // minimum glissant sur H echantillons (file monotone)
            const long long k = idx++;
            req[(size_t) (k % H)] = g;
            while (head != tail && req[(size_t) (dq[(size_t) ((tail + dq.size() - 1) % dq.size())] % H)] >= g) tail = (tail + dq.size() - 1) % dq.size();
            dq[tail] = k; tail = (tail + 1) % dq.size();
            while (dq[head] <= k - H) head = (head + 1) % dq.size();
            const float mn = req[(size_t) (dq[head] % H)];

            rel = std::min (mn, rel + (1.0f - rel) * relCoef);
            avgSum += rel - avg[(size_t) ap]; avg[(size_t) ap] = rel; if (++ap >= la) ap = 0;
            if ((k & 2047) == 0) { avgSum = 0.0f; for (float v : avg) avgSum += v; }
            const float gain = std::min (1.0f, avgSum / (float) la);
            float l = dl.process (L[i]) * gain, r = dr.process (R[i]) * gain;
            if (enabled) { l = clampf (l, -ceiling, ceiling); r = clampf (r, -ceiling, ceiling); }
            L[i] = l; R[i] = r;
            worst = std::min (worst, gain); sumGr += gainToDb (gain);
        }
        lastGr = gainToDb (worst); lastAvgGr = n > 0 ? (float) (sumGr / n) : 0.0f;
    }
    float lastGr = 0.0f, lastAvgGr = 0.0f;
private:
    float sr = 48000.0f, ceiling = 0.89f, relCoef = 0.01f, rel = 1.0f, avgSum = 0.0f;
    bool enabled = true; int D = 20, la = 72, H = 75, ap = 0; long long idx = 0;
    Oversampler4x det[2]; FixedDelay dl, dr; std::vector<float> req, avg;
    std::vector<long long> dq; size_t head = 0, tail = 0;
};

/** Tous les reglages du moteur, en unites reelles. */
struct MasterParams
{
    float inGain = 0.0f;
    bool eqOn = true; float lowCut = 25.0f, lowF = 100.0f, lowG = 0.0f, mudF = 250.0f, mudG = 0.0f, mudQ = 1.0f;
    float presF = 3000.0f, presG = 0.0f, airF = 10000.0f, airG = 0.0f;
    bool mbOn = true; float xLow = 150.0f, xHigh = 4000.0f, thr[3] = { -20, -20, -20 }, ratio[3] = { 2, 2, 2 }, bandGain[3] = { 0, 0, 0 };
    bool glueOn = true; float glueThr = -18.0f, glueRatio = 2.0f, glueAttack = 10.0f, glueRelease = 150.0f, glueMakeup = 0.0f, glueMix = 100.0f;
    bool satOn = true; int satMode = 1; float satDrive = 3.0f, satMix = 50.0f;
    bool stOn = true; float width = 100.0f, monoBelow = 100.0f;
    bool limOn = true; float limGain = 0.0f, ceiling = -1.0f, limRelease = 120.0f;
};

/** La chaine complete. process() = chaine entiere ; processPre() + processLimiter() = la meme chose en deux temps
    (l'analyse calcule une fois la partie avant le limiteur, puis essaie plusieurs gains). */
class MasterEngine
{
public:
    void prepare (float sampleRate)
    {
        sr = sampleRate;
        xo.prepare (sr);
        for (auto& c : bandComp) c.prepare (sr);
        glue.prepare (sr);
        for (auto& o : os) o.prepare();
        sat.prepare (sr * 4.0f);
        satDelayL.prepare (os[0].latency() + 1); satDelayR.prepare (os[0].latency() + 1);
        satDelayL.setDelay (os[0].latency()); satDelayR.setDelay (os[0].latency());
        limiter.prepare (sr);
        inSm.setTime (30.0f, sr); limSm.setTime (30.0f, sr);
        reset();
    }
    void reset() noexcept
    {
        for (auto* f : { &cut, &low, &mud, &pres, &air }) f->reset();
        xo.reset(); for (auto& c : bandComp) c.reset(); glue.reset();
        os[0].reset(); os[1].reset(); sat.reset(); satDelayL.reset(); satDelayR.reset();
        sideHp.reset(); limiter.reset();
        inSm.reset (dbToGain (p.inGain)); limSm.reset (dbToGain (p.limGain)); widthSm.reset (p.width * 0.01f);
    }
    int latency() const noexcept { return os[0].latency() + limiter.latency(); }
    int limiterLatency() const noexcept { return limiter.latency(); }

    void setParams (const MasterParams& np) noexcept
    {
        p = np;
        cut.l.setHighpass (p.lowCut, 0.70710678f, sr); cut.sync();
        cut2.l.setHighpass (p.lowCut, 0.70710678f, sr); cut2.sync();
        low.l.setLowShelf (p.lowF, p.lowG, sr); low.sync();
        mud.l.setPeak (p.mudF, p.mudQ, p.mudG, sr); mud.sync();
        pres.l.setPeak (p.presF, 0.8f, p.presG, sr); pres.sync();
        air.l.setHighShelf (p.airF, p.airG, sr); air.sync();
        xo.set (p.xLow, p.xHigh);
        static const float att[3] = { 30.0f, 15.0f, 5.0f }, rel[3] = { 160.0f, 100.0f, 60.0f };
        for (int b = 0; b < 3; ++b) bandComp[b].setParams (p.thr[b], p.ratio[b], att[b], rel[b], p.bandGain[b], 1.0f);
        glue.setParams (p.glueThr, p.glueRatio, p.glueAttack, p.glueRelease, p.glueMakeup, p.glueMix * 0.01f);
        sat.setParams (p.satMode, p.satDrive, 18000.0f, p.satMix * 0.01f);
        sideHp.setHigh (std::max (20.0f, p.monoBelow), sr);
        limiter.setParams (p.ceiling, p.limRelease, p.limOn);
    }

    void process (float* L, float* R, int n) noexcept { processPre (L, R, n); processLimiter (L, R, n); }

    void processPre (float* L, float* R, int n) noexcept
    {
        const float gIn = dbToGain (p.inGain);
        for (int i = 0; i < n; ++i) { const float g = inSm.next (gIn); L[i] *= g; R[i] *= g; }

        if (p.eqOn)
        {
            cut.process (L, R, n); cut2.process (L, R, n);
            if (std::abs (p.lowG) > 0.01f)  low.process (L, R, n);
            if (std::abs (p.mudG) > 0.01f)  mud.process (L, R, n);
            if (std::abs (p.presG) > 0.01f) pres.process (L, R, n);
            if (std::abs (p.airG) > 0.01f)  air.process (L, R, n);
        }

        if (p.mbOn)
        {
            float* bl[3] = { b0l, b1l, b2l }; float* br[3] = { b0r, b1r, b2r };
            for (int i = 0; i < n; ++i)
            {
                xo.split (0, L[i], b0l[i], b1l[i], b2l[i]);
                xo.split (1, R[i], b0r[i], b1r[i], b2r[i]);
            }
            for (int b = 0; b < 3; ++b) { bandComp[b].process (bl[b], br[b], n); bandGr[b] = bandComp[b].lastGr; }
            for (int i = 0; i < n; ++i) { L[i] = b0l[i] + b1l[i] + b2l[i]; R[i] = b0r[i] + b1r[i] + b2r[i]; }
        }
        else bandGr[0] = bandGr[1] = bandGr[2] = 0.0f;

        if (p.glueOn) { glue.process (L, R, n); glueGr = glue.lastGr; } else glueGr = 0.0f;

        if (p.satOn)
        {
            float u[4];
            for (int i = 0; i < n; ++i)
            {
                os[0].up (L[i], u); sat.process (u, 4, 0); L[i] = os[0].down (u);
                os[1].up (R[i], u); sat.process (u, 4, 1); R[i] = os[1].down (u);
            }
        }
        else
            for (int i = 0; i < n; ++i) { L[i] = satDelayL.process (L[i]); R[i] = satDelayR.process (R[i]); }

        if (p.stOn)
        {
            const float target = p.width * 0.01f; const bool mono = p.monoBelow > 21.0f;
            for (int i = 0; i < n; ++i)
            {
                const float m = 0.5f * (L[i] + R[i]);
                float s = 0.5f * (L[i] - R[i]);
                if (mono) s = sideHp.process (s);          // le cote perd ses graves : basses en mono
                const float w = widthSm.next (target);
                L[i] = m + s * w; R[i] = m - s * w;
            }
        }
    }

    void processLimiter (float* L, float* R, int n) noexcept
    {
        const float g = dbToGain (p.limGain);
        for (int i = 0; i < n; ++i) { const float s = limSm.next (g); L[i] *= s; R[i] *= s; }
        limiter.process (L, R, n);
    }

    float bandGr[3] = {}, glueGr = 0.0f;
    float limiterGr() const noexcept { return limiter.lastGr; }
    float limiterAvgGr() const noexcept { return limiter.lastAvgGr; }
    static constexpr int kMaxBlock = 512;

private:
    float sr = 48000.0f; MasterParams p;
    StereoBiquad cut, cut2, low, mud, pres, air;
    Crossover3 xo; Compressor bandComp[3], glue;
    float b0l[kMaxBlock] = {}, b1l[kMaxBlock] = {}, b2l[kMaxBlock] = {}, b0r[kMaxBlock] = {}, b1r[kMaxBlock] = {}, b2r[kMaxBlock] = {};
    Oversampler4x os[2]; Saturator sat; FixedDelay satDelayL, satDelayR;
    LR4 sideHp;
    TruePeakLimiter limiter; Smooth inSm, limSm, widthSm;
};
} // namespace mj7
