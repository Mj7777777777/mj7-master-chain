// Tests du moteur de mastering (sans JUCE) :  g++ -std=c++20 -O2 -I Source tests/dsp_tests.cpp -o t && ./t
#include "dsp/MasterAnalyzer.h"
#include <chrono>
#include <cstdio>
#include <random>

using namespace mj7;
static int failures = 0;
#define CHECK(cond, ...) do { if (! (cond)) { ++failures; std::printf ("  ECHEC: "); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)
static std::mt19937 rng (5);
static float noise() { return std::uniform_real_distribution<float> (-1.0f, 1.0f) (rng); }
static bool finite (const std::vector<float>& v) { for (float s : v) if (! std::isfinite (s)) return false; return true; }

/** "Mix" de synthese : kick, basse, accords, charleston, voix, stereo, avec des passages forts et calmes. */
static void makeMix (std::vector<float>& L, std::vector<float>& R, float sr, float seconds, float level)
{
    const int n = (int) (seconds * sr); L.assign ((size_t) n, 0.0f); R.assign ((size_t) n, 0.0f);
    const float bpm = 100.0f, beat = 60.0f / bpm * sr;
    Biquad hat; hat.setHighpass (7000.0f, 0.7f, sr); Biquad hatR = hat;
    double ph[6] = {};
    for (int i = 0; i < n; ++i)
    {
        const float t = (float) i / sr, pos = std::fmod ((float) i, beat) / beat;
        const float sect = std::fmod (t, 8.0f) < 4.0f ? 1.0f : 0.45f;                    // couplet / refrain
        const float kick = std::sin (2.0f * kPi * (50.0f + 80.0f * std::exp (-pos * 30.0f)) * pos * 60.0f / bpm) * std::exp (-pos * 9.0f);
        ph[0] += 2.0 * 3.14159265 * 55.0 / sr; const float bass = 0.5f * (float) std::sin (ph[0]) + 0.15f * (float) std::sin (2.0 * ph[0]);
        float chord = 0.0f; const float notes[3] = { 220.0f, 277.2f, 329.6f };
        for (int k = 0; k < 3; ++k) { ph[1 + k] += 2.0 * 3.14159265 * notes[k] / sr; chord += 0.12f * (float) std::sin (ph[1 + k]); }
        ph[4] += 2.0 * 3.14159265 * (330.0 + 20.0 * std::sin (t * 5.0)) / sr;
        const float voice = 0.25f * (float) (std::sin (ph[4]) + 0.4 * std::sin (2 * ph[4]) + 0.2 * std::sin (3 * ph[4])) * (std::fmod (t, 2.0f) < 1.6f ? 1.0f : 0.0f);
        const float hpos = std::fmod ((float) i, beat * 0.5f) / (beat * 0.5f), hn = noise() * std::exp (-hpos * 40.0f) * 0.2f;
        const float m = level * sect * (0.9f * kick + 0.6f * bass + chord + voice);
        L[(size_t) i] = m + level * sect * (0.08f * chord + hat.process (hn));
        R[(size_t) i] = m - level * sect * 0.08f * chord + level * sect * hatR.process (hn * 0.7f);
    }
}

int main()
{
    for (float sr : { 44100.0f, 48000.0f, 96000.0f })
    {
        std::printf ("== %.0f Hz ==\n", sr);
        // --- surechantillonneur : transparent et latence entiere ---
        {
            Oversampler4x os; os.prepare(); const int lat = os.latency();
            std::vector<float> in ((size_t) sr / 4), out (in.size()); float u[4];
            for (size_t i = 0; i < in.size(); ++i) in[i] = 0.5f * std::sin (2.0f * kPi * 1000.0f * (float) i / sr);
            for (size_t i = 0; i < in.size(); ++i) { os.up (in[i], u); out[i] = os.down (u); }
            float err = 0; for (size_t i = 2000; i < in.size(); ++i) err = std::max (err, std::abs (out[i] - in[i - (size_t) lat]));
            std::printf ("  surechantillonnage : latence %d, ecart %.6f\n", lat, err);
            CHECK (err < 2.0e-3f, "Surechantillonnage pas transparent (%.5f)", err);
        }
        // --- mesure de loudness : sinus 1 kHz a -20 dBFS sur deux canaux = -20 LUFS (+ 0,0 selon la norme a 997 Hz) ---
        {
            std::vector<float> s ((size_t) (10 * sr)); for (size_t i = 0; i < s.size(); ++i) s[i] = 0.1f * std::sin (2.0f * kPi * 997.0f * (float) i / sr);
            const float lu = detail::loudnessOf (s, s, sr);
            CHECK (std::abs (lu + 20.0f) < 0.2f, "LUFS integre %.2f au lieu de -20", lu);
            // true peak : sinus a fs/4 dephase de 45 degres, crete vraie 0 dBFS, crete echantillon -3 dB
            std::vector<float> tpv ((size_t) sr); for (size_t i = 0; i < tpv.size(); ++i) tpv[i] = std::sin (kPi * 0.5f * (float) i + kPi * 0.25f);
            float tp = 0; detail::loudnessOf (tpv, tpv, sr, &tp);
            std::printf ("  true peak d'un sinus a fs/4 : %.2f dBTP (attendu ~0, crete echantillon -3)\n", tp);
            CHECK (tp > -0.6f && tp < 0.3f, "True peak %.2f", tp);
            // porte relative : 10 s a -20 puis 10 s a -50 => integre ~ -20 (la partie calme est ecartee)
            std::vector<float> g ((size_t) (20 * sr)); for (size_t i = 0; i < g.size(); ++i) g[i] = (i < g.size() / 2 ? 0.1f : 0.00316f) * std::sin (2.0f * kPi * 997.0f * (float) i / sr);
            const float gated = detail::loudnessOf (g, g, sr);
            CHECK (std::abs (gated + 20.0f) < 0.3f, "Porte relative : %.2f au lieu de -20", gated);
        }
        // --- moteur : stable, plafond true peak respecte, latence exacte ---
        {
            std::vector<float> L, R; makeMix (L, R, sr, 6.0f, 0.8f);
            for (auto& v : L) v *= 2.0f;                                           // mix trop fort expres
            MasterParams p; p.limGain = 6.0f; p.ceiling = -1.0f; p.width = 200.0f; p.satDrive = 18.0f; p.satMode = 2;
            auto e = std::make_unique<MasterEngine>(); e->prepare (sr); e->setParams (p); e->reset();
            for (size_t i = 0; i < L.size(); i += 333) { const int len = (int) std::min ((size_t) 333, L.size() - i); e->process (L.data() + i, R.data() + i, len); }
            float tp = 0; const float lu = detail::loudnessOf (L, R, sr, &tp);
            std::printf ("  moteur : %.1f LUFS, true peak %.2f dBTP (plafond -1)\n", lu, tp);
            CHECK (finite (L) && finite (R), "Valeur non finie");
            CHECK (tp <= -0.8f, "True peak %.2f au-dessus du plafond", tp);

            MasterParams z; z.mbOn = z.glueOn = z.satOn = z.stOn = false; z.eqOn = false; z.limOn = false;
            e->setParams (z); e->reset();
            std::vector<float> a ((size_t) sr, 0.0f), b ((size_t) sr, 0.0f); a[1000] = b[1000] = 0.5f;
            for (int i = 0; i < (int) a.size(); i += 512) e->process (a.data() + i, b.data() + i, std::min (512, (int) a.size() - i));
            size_t pk = 0; for (size_t i = 0; i < a.size(); ++i) if (std::abs (a[i]) > std::abs (a[pk])) pk = i;
            CHECK ((int) pk - 1000 == e->latency(), "Latence mesuree %d, declaree %d", (int) pk - 1000, e->latency());
            CHECK (std::abs (a[pk] - 0.5f) < 0.02f, "Chaine neutre pas transparente (%.3f)", a[pk]);
            // separation 3 bandes : la somme garde un spectre plat
            Crossover3 xo; xo.prepare (sr); xo.set (150.0f, 4000.0f, true);
            for (float f : { 50.0f, 150.0f, 1000.0f, 4000.0f, 12000.0f })
            {
                double ein = 0, eout = 0; for (int i = 0; i < (int) sr; ++i)
                { const float x = std::sin (2.0f * kPi * f * (float) i / sr); float l, m, h; xo.split (0, x, l, m, h); if (i > sr / 2) { ein += x * x; eout += (double) (l + m + h) * (l + m + h); } }
                CHECK (std::abs (10.0 * std::log10 (eout / ein)) < 0.1, "Separation des bandes pas plate a %.0f Hz", f);
            }
        }
    }

    // --- analyse : atteint la cible de loudness pour plusieurs plateformes ---
    {
        const float sr = 48000.0f; std::vector<float> L, R; makeMix (L, R, sr, 20.0f, 0.25f);
        GenreTarget gt; MasterParams base;
        for (float target : { -14.0f, -9.0f, -23.0f })
        {
            const auto t0 = std::chrono::steady_clock::now();
            const auto r = analyseMaster (L.data(), R.data(), (int) L.size(), sr, gt, target, base);
            const double secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            std::printf ("== analyse, cible %.0f LUFS (%.1f s de calcul) ==\n", target, secs);
            std::printf ("  entree %.1f LUFS, %.1f dBTP | bandes sub %.1f basses %.1f bas-med %.1f pres %.1f air %.1f | correl. graves %.2f, cote %.1f dB\n",
                         r.inLufs, r.inTp, r.bands[0], r.bands[1], r.bands[2], r.bands[3], r.bands[4], r.corrLow, r.sideDb);
            std::printf ("  EQ basses %+.1f boue %+.1f presence %+.1f air %+.1f | seuils MB %.1f %.1f %.1f glue %.1f | largeur %.0f %%, mono sous %.0f Hz\n",
                         r.lowG, r.mudG, r.presG, r.airG, r.thr[0], r.thr[1], r.thr[2], r.glueThr, r.width, r.monoBelow);
            std::printf ("  gain limiteur %+.1f dB -> %.2f LUFS, %.2f dBTP, reduction moyenne %.1f dB, max %.1f dB\n",
                         r.limGain, r.outLufs, r.outTp, r.limAvgGr, r.limMaxGr);
            CHECK (r.valid && std::abs (r.outLufs - target) < 0.3f, "Cible %.0f non atteinte (%.2f)", target, r.outLufs);
            CHECK (r.outTp <= -0.8f, "True peak %.2f apres calage", r.outTp);
        }
        std::vector<float> s ((size_t) (20 * sr), 0.0f);
        CHECK (! analyseMaster (s.data(), s.data(), (int) s.size(), sr, gt, -14.0f, base).valid, "Le silence ne doit pas etre analyse");
    }
    std::printf (failures == 0 ? "\nTOUS LES TESTS PASSENT\n" : "\n%d ECHEC(S)\n", failures);
    return failures == 0 ? 0 : 1;
}
