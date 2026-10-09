// Test d'integration du MJ7 Master Chain (sans carte son) :
//   cmake -B build -DMJ7_BUILD_TESTS=ON && cmake --build build --target MJ7HostTest
#include "PluginEditor.h"
#include <iostream>

static int failures = 0;
#define CHECK(cond, msg) do { if (! (cond)) { ++failures; std::cout << "  ECHEC: " << msg << std::endl; } } while (0)
static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

/** Mix de synthese : kick, basse, accords, voix, charleston, couplets calmes et refrains forts. */
static juce::AudioBuffer<float> makeMix (double sr, double seconds, float level)
{
    juce::AudioBuffer<float> b (2, (int) (seconds * sr)); juce::Random rnd (4);
    const float beat = (float) (60.0 / 100.0 * sr); double ph[5] = {}; mj7::Biquad hat, hatR; hat.setHighpass (7000, 0.7f, (float) sr); hatR.copyCoefs (hat);
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const float t = (float) (i / sr), pos = std::fmod ((float) i, beat) / beat, sect = std::fmod (t, 8.0f) < 4.0f ? 1.0f : 0.45f;
        const float kick = std::sin (2.0f * mj7::kPi * (50.0f + 80.0f * std::exp (-pos * 30.0f)) * pos * 0.6f) * std::exp (-pos * 9.0f);
        ph[0] += 2.0 * 3.14159265 * 55.0 / sr; const float bass = 0.5f * (float) std::sin (ph[0]);
        float chord = 0.0f; const float notes[3] = { 220.0f, 277.2f, 329.6f };
        for (int k = 0; k < 3; ++k) { ph[1 + k] += 2.0 * 3.14159265 * notes[k] / sr; chord += 0.12f * (float) std::sin (ph[1 + k]); }
        ph[4] += 2.0 * 3.14159265 * 330.0 / sr; const float voice = 0.25f * (float) std::sin (ph[4]) * (std::fmod (t, 2.0f) < 1.6f ? 1.0f : 0.0f);
        const float hn = (rnd.nextFloat() * 2.0f - 1.0f) * std::exp (-std::fmod ((float) i, beat * 0.5f) / (beat * 0.5f) * 40.0f) * 0.2f;
        const float m = level * sect * (0.9f * kick + 0.6f * bass + chord + voice);
        b.setSample (0, i, m + level * sect * (0.08f * chord + hat.process (hn)));
        b.setSample (1, i, m - level * sect * 0.08f * chord + level * sect * hatR.process (hn * 0.7f));
    }
    return b;
}

static juce::AudioBuffer<float> run (MJ7MasterProcessor& p, const juce::AudioBuffer<float>& in, juce::Random& rnd)
{
    juce::AudioBuffer<float> out (2, in.getNumSamples()); juce::MidiBuffer midi;
    for (int pos = 0; pos < in.getNumSamples();)
    {
        const int n = juce::jmin (in.getNumSamples() - pos, 1 + rnd.nextInt (1024));
        juce::AudioBuffer<float> block (2, n);
        block.copyFrom (0, 0, in, 0, pos, n); block.copyFrom (1, 0, in, 1, pos, n);
        p.processBlock (block, midi);
        out.copyFrom (0, pos, block, 0, 0, n); out.copyFrom (1, pos, block, 1, 0, n);
        pos += n;
    }
    return out;
}

static bool allFinite (const juce::AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c) for (int i = 0; i < b.getNumSamples(); ++i) if (! std::isfinite (b.getSample (c, i))) return false;
    return true;
}

static float measure (const juce::AudioBuffer<float>& b, double sr, float* tp)
{
    std::vector<float> l (b.getReadPointer (0), b.getReadPointer (0) + b.getNumSamples()), r (b.getReadPointer (1), b.getReadPointer (1) + b.getNumSamples());
    return mj7::detail::loudnessOf (l, r, (float) sr, tp);
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File outDir = argc > 1 ? juce::File (argv[1]) : juce::File::getCurrentWorkingDirectory();
    juce::Random rnd (11);

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        std::cout << "== " << sr << " Hz ==" << std::endl;
        MJ7MasterProcessor p; p.setRateAndBufferSizeDetails (sr, 512); p.prepareToPlay (sr, 512);
        const auto mix = makeMix (sr, 4.0, 0.35f);
        for (int i = 0; i < (int) mj7::factoryPresets().size(); ++i)
        {
            p.loadFactoryPreset (i);
            p.apvts.getParameter ("lim_gain")->setValueNotifyingHost (p.apvts.getParameter ("lim_gain")->convertTo0to1 (12.0f));
            const auto out = run (p, mix, rnd);
            float tp = 0; const float lu = measure (out, sr, &tp);
            std::cout << "  " << juce::String (juce::CharPointer_UTF8 (mj7::factoryPresets()[(size_t) i].name)).paddedRight (' ', 28)
                      << juce::String (lu, 1) << " LUFS, " << juce::String (tp, 2) << " dBTP" << std::endl;
            CHECK (allFinite (out), "valeur non finie");
            CHECK (tp <= -0.85f, "plafond true peak depasse");
        }
        // latence
        {
            p.loadFactoryPreset ((int) mj7::factoryPresets().size() - 1);
            for (const char* id : { "eq_on", "mb_on", "glue_on", "sat_on", "st_on", "lim_on" }) p.apvts.getParameter (id)->setValueNotifyingHost (0.0f);
            p.prepareToPlay (sr, 512);
            juce::AudioBuffer<float> imp (2, (int) sr); imp.clear(); imp.setSample (0, 20000, 0.5f); imp.setSample (1, 20000, 0.5f);
            const auto out = run (p, imp, rnd);
            int best = 0; for (int i = 0; i < out.getNumSamples(); ++i) if (std::abs (out.getSample (0, i)) > std::abs (out.getSample (0, best))) best = i;
            std::cout << "  latence declaree " << p.getLatencySamples() << ", mesuree " << best - 20000 << " (" << juce::String (1000.0 * p.getLatencySamples() / sr, 1) << " ms)" << std::endl;
            CHECK (best - 20000 == p.getLatencySamples(), "latence declaree differente de la latence mesuree");
            p.loadFactoryPreset (0);
        }
        // etat
        {
            p.apvts.getParameter ("st_width")->setValueNotifyingHost (0.77f);
            juce::MemoryBlock st; p.getStateInformation (st);
            MJ7MasterProcessor q; q.setStateInformation (st.getData(), (int) st.getSize());
            bool same = true;
            for (int pid = 0; pid < mj7::kNumParams; ++pid)
                if (std::abs (p.apvts.getParameter (mj7::paramDef (pid).id)->getValue() - q.apvts.getParameter (mj7::paramDef (pid).id)->getValue()) > 1.0e-5f) same = false;
            CHECK (same, "l'etat recharge differe");
        }
    }

    // --- ANALYSER de bout en bout, puis verification sur le son reel ---
    {
        const double sr = 48000.0;
        MJ7MasterProcessor p; p.setRateAndBufferSizeDetails (sr, 512); p.prepareToPlay (sr, 512);
        p.loadFactoryPreset (0);
        const auto mix = makeMix (sr, 24.0, 0.2f);
        for (int t : { 0, 5, 4 })                                                // streaming -14, TV -23, club -8
        {
            p.apvts.getParameter ("target")->setValueNotifyingHost (p.apvts.getParameter ("target")->convertTo0to1 ((float) t));
            pump (200);
            if (t == 0)
            {
                p.startAnalysis();
                run (p, mix, rnd);
            }
            for (int i = 0; i < 300 && p.analysisState() != MJ7MasterProcessor::done && p.analysisState() != MJ7MasterProcessor::failed; ++i) pump (100);
            pump (300);
            CHECK (p.analysisState() == MJ7MasterProcessor::done, "l'analyse n'a pas abouti");
            const float target = mj7::targetLufs (t);
            std::cout << "== cible " << target << " LUFS ==" << std::endl;
            for (const auto& line : p.summary) std::cout << "  " << line << std::endl;
            p.prepareToPlay (sr, 512);
            const auto out = run (p, mix, rnd);
            float tp = 0; const float lu = measure (out, sr, &tp);
            std::cout << "  mesure sur le son traite : " << juce::String (lu, 2) << " LUFS, " << juce::String (tp, 2) << " dBTP" << std::endl;
            CHECK (std::abs (lu - target) < 0.6f, "la sortie n'atteint pas la cible");
            CHECK (tp <= -0.85f, "crete vraie au-dessus du plafond");
            if (t == 0)
            {
                juce::MemoryBlock st; p.getStateInformation (st);
                MJ7MasterProcessor q; q.setStateInformation (st.getData(), (int) st.getSize());
                CHECK (q.hasAnalysis() && q.summary.size() == p.summary.size(), "analyse non restauree avec le projet");
            }
        }
        // bypass a niveau egal : l'original doit sortir a peu pres au niveau du master
        {
            p.apvts.getParameter ("target")->setValueNotifyingHost (0.0f); pump (200);
            for (int i = 0; i < 300 && p.analysisState() != MJ7MasterProcessor::done; ++i) pump (100);
            pump (300);
            run (p, makeMix (sr, 6.0, 0.2f), rnd);
            p.apvts.getParameter ("bypass")->setValueNotifyingHost (1.0f);
            const auto byp = run (p, makeMix (sr, 6.0, 0.2f), rnd);
            float tp = 0; const float lb = measure (byp, sr, &tp);
            std::cout << "  bypass a niveau egal : " << juce::String (lb, 2) << " LUFS (master -14)" << std::endl;
            CHECK (std::abs (lb + 14.0f) < 1.5f, "le bypass a niveau egal ne compense pas le volume");
            p.apvts.getParameter ("bypass")->setValueNotifyingHost (0.0f);
        }
        // interface
        std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
        auto snap = [&] (const juce::String& name)
        {
            run (p, makeMix (sr, 1.0, 0.2f), rnd); pump (400);
            const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
            juce::File f = outDir.getChildFile (name); f.deleteFile();
            juce::FileOutputStream os (f); juce::PNGImageFormat png; png.writeImageToStream (img, os);
        };
        auto* view = ed->getChildComponent (0);
        auto select = [view] (int index)
        {
            mj7ui::ChainTile* found = nullptr; int tile = 0;
            for (auto* c : view->getChildren()) if (auto* t = dynamic_cast<mj7ui::ChainTile*> (c)) if (tile++ == index) found = t;
            if (found != nullptr && found->onSelect) found->onSelect();
        };
        snap ("master_traite.png");
        select (3); snap ("master_multibande.png");
        select (2); snap ("master_eq.png");
        select (0);
    }
    std::cout << (failures == 0 ? "\nTOUS LES TESTS PASSENT" : "\nECHECS : " + std::to_string (failures)) << std::endl;
    return failures == 0 ? 0 : 1;
}
