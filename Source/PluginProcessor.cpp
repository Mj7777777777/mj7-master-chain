#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace mj7;

static juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int pid = 0; pid < kNumParams; ++pid)
    {
        const auto& d = paramDef (pid);
        const juce::ParameterID id { d.id, 1 };
        const auto name = U8 (d.name), unit = U8 (d.unit);
        switch (d.type)
        {
            case PType::tF:
            {
                juce::NormalisableRange<float> range (d.min, d.max);
                if (d.centre > 0.0f) range.setSkewForCentre (d.centre);
                const bool db = juce::String (d.unit).startsWith ("dB");
                const int decimals = (d.max - d.min) >= 100.0f ? 0 : ((db || (d.max - d.min) >= 20.0f) ? 1 : 2);
                layout.add (std::make_unique<juce::AudioParameterFloat> (id, name, range, d.def,
                    juce::AudioParameterFloatAttributes()
                        .withStringFromValueFunction ([decimals, unit] (float value, int)
                        {
                            if (std::abs (value) < 0.005f) value = 0.0f;           // pas de "-0.0"
                            const auto num = decimals == 0 ? juce::String (juce::roundToInt (value)) : juce::String (value, decimals);
                            return unit.isEmpty() ? num : num + " " + unit;
                        })
                        .withValueFromStringFunction ([] (const juce::String& s) { return s.replaceCharacter (',', '.').getFloatValue(); })));
                break;
            }
            case PType::tI:
                layout.add (std::make_unique<juce::AudioParameterInt> (id, name, (int) d.min, (int) d.max, (int) d.def));
                break;
            case PType::tB:
                layout.add (std::make_unique<juce::AudioParameterBool> (id, name, d.def > 0.5f));
                break;
            case PType::tC:
                layout.add (std::make_unique<juce::AudioParameterChoice> (id, name, juce::StringArray::fromTokens (U8 (d.choices), "|", ""), (int) d.def));
                break;
        }
    }
    return layout;
}

MJ7MasterProcessor::MJ7MasterProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread ("MJ7 analyse"),
      apvts (*this, nullptr, "MJ7MasterChain", createLayout())
{
    for (int pid = 0; pid < kNumParams; ++pid) raw[pid] = apvts.getRawParameterValue (paramDef (pid).id);
    for (auto& m : meter) m.store (0.0f);
    engine = std::make_unique<MasterEngine>();
    outLoud = std::make_unique<LoudnessMeter>(); inLoud = std::make_unique<LoudnessMeter>(); tpMeter = std::make_unique<TruePeakMeter>();
    safety = std::make_unique<TruePeakLimiter>();
    apvts.addParameterListener ("intensity", this);
    apvts.addParameterListener ("target", this);
    startTimerHz (15);
}

MJ7MasterProcessor::~MJ7MasterProcessor()
{
    stopTimer(); stopThread (10000);
    apvts.removeParameterListener ("intensity", this);
    apvts.removeParameterListener ("target", this);
}

bool MJ7MasterProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet(), out = layouts.getMainOutputChannelSet();
    const bool inOk = in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
    const bool outOk = out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
    return inOk && outOk && in.size() <= out.size();
}

juce::AudioProcessorEditor* MJ7MasterProcessor::createEditor() { return new MJ7MasterEditor (*this); }

MasterParams MJ7MasterProcessor::currentParams() const noexcept
{
    MasterParams p;
    p.inGain = v (in_gain);
    p.eqOn = on (eq_on); p.lowCut = v (eq_lowcut); p.lowF = v (eq_low_f); p.lowG = v (eq_low_g);
    p.mudF = v (eq_mud_f); p.mudG = v (eq_mud_g); p.mudQ = v (eq_mud_q); p.presF = v (eq_pres_f); p.presG = v (eq_pres_g);
    p.airF = v (eq_air_f); p.airG = v (eq_air_g);
    p.mbOn = on (mb_on); p.xLow = v (mb_xlow); p.xHigh = v (mb_xhigh);
    p.thr[0] = v (mb_low_thr); p.ratio[0] = v (mb_low_ratio); p.bandGain[0] = v (mb_low_gain);
    p.thr[1] = v (mb_mid_thr); p.ratio[1] = v (mb_mid_ratio); p.bandGain[1] = v (mb_mid_gain);
    p.thr[2] = v (mb_high_thr); p.ratio[2] = v (mb_high_ratio); p.bandGain[2] = v (mb_high_gain);
    p.glueOn = on (glue_on); p.glueThr = v (glue_thr); p.glueRatio = v (glue_ratio); p.glueAttack = v (glue_attack);
    p.glueRelease = v (glue_release); p.glueMakeup = v (glue_makeup); p.glueMix = v (glue_mix);
    p.satOn = on (sat_on); p.satMode = (int) v (sat_mode); p.satDrive = v (sat_drive); p.satMix = v (sat_mix);
    p.stOn = on (st_on); p.width = v (st_width); p.monoBelow = v (st_mono);
    p.limOn = on (lim_on); p.limGain = v (lim_gain); p.ceiling = v (lim_ceiling); p.limRelease = v (lim_release);
    return p;
}

// ================================================================================================
void MJ7MasterProcessor::prepareToPlay (double sampleRate, int)
{
    stopThread (10000);
    if (state.load() != done) state.store (idle);
    sr = (float) sampleRate;
    engine->prepare (sr); engine->setParams (currentParams()); engine->reset();
    outLoud->prepare (sr); inLoud->prepare (sr); tpMeter->prepare();
    latency = engine->latency();
    const int dryDelay = latency - engine->limiterLatency();          // + la latence du limiteur de securite = latence totale
    bypassL.prepare (dryDelay + 1); bypassR.prepare (dryDelay + 1); bypassL.setDelay (dryDelay); bypassR.setDelay (dryDelay);
    safety->prepare (sr);
    dryGainSm.setTime (50.0f, sr); dryGainSm.reset (1.0f);
    setLatencySamples (latency);
    const int needed = (int) (analysisSeconds() * sr);
    if (needed != captureLen) { capL.assign ((size_t) needed, 0.0f); capR.assign ((size_t) needed, 0.0f); captureLen = needed; captureValid = false; capturePos.store (0); }
}

void MJ7MasterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    if (n == 0 || buffer.getNumChannels() == 0) return;
    const bool stereo = buffer.getNumChannels() > 1;
    float* L = buffer.getWritePointer (0);
    float* R = stereo ? buffer.getWritePointer (1) : nullptr;
    if (stereo && getTotalNumInputChannels() < 2) std::memcpy (R, L, sizeof (float) * (size_t) n);
    for (int pos = 0; pos < n; pos += chunk)
    {
        const int len = std::min (chunk, n - pos);
        if (stereo) processChunk (L + pos, R + pos, len);
        else
        {
            std::memcpy (tmpR, L + pos, sizeof (float) * (size_t) len); processChunk (L + pos, tmpR, len);
            for (int i = 0; i < len; ++i) L[pos + i] = 0.5f * (L[pos + i] + tmpR[i]);
        }
    }
    for (int ch = 2; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);
}

void MJ7MasterProcessor::processChunk (float* L, float* R, int n)
{
    if (resetRequest.exchange (false)) { outLoud->resetIntegrated(); tpMeter->reset(); }

    // --- capture pour ANALYSER (mix d'origine, stereo) ---
    const int st = state.load (std::memory_order_relaxed);
    if (st == waiting)
    {
        float pk = 0.0f; for (int i = 0; i < n; ++i) pk = std::max (pk, std::max (std::abs (L[i]), std::abs (R[i])));
        if (pk > 0.003f) state.store (listening);
        else if ((waited += n) > (int) (30.0f * sr)) state.store (failed);
    }
    if (state.load (std::memory_order_relaxed) == listening)
    {
        const int p = capturePos.load (std::memory_order_relaxed), take = std::min (n, captureLen - p);
        std::memcpy (capL.data() + p, L, sizeof (float) * (size_t) take); std::memcpy (capR.data() + p, R, sizeof (float) * (size_t) take);
        capturePos.store (p + take);
        if (p + take >= captureLen) state.store (computing);
    }

    float pk = 0.0f;
    for (int i = 0; i < n; ++i) pk = std::max (pk, std::max (std::abs (L[i]), std::abs (R[i])));
    inPeak.store (std::max (inPeak.load (std::memory_order_relaxed), pk));
    inLoud->process (L, R, n);
    { int sp = scopeInPos.load (std::memory_order_relaxed);
      for (int i = 0; i < n; ++i) { scopeIn[sp] = 0.5f * (L[i] + R[i]); sp = (sp + 1) & (scopeSize - 1); }
      scopeInPos.store (sp, std::memory_order_relaxed); }

    const bool bypassed = on (bypass);
    if (bypassed != wasBypassed) { bypassL.reset(); bypassR.reset(); safety->reset(); wasBypassed = bypassed; }
    if (bypassed)
    {
        // ecoute du mix d'origine, au meme niveau que le master si "niveau egal" est actif
        const float g = on (match) ? dbToGain (clampf (-matchDb, -30.0f, 18.0f)) : 1.0f;
        for (int i = 0; i < n; ++i) { const float s = dryGainSm.next (g); L[i] = bypassL.process (L[i]) * s; R[i] = bypassR.process (R[i]) * s; }
        safety->setParams (v (lim_ceiling), 100.0f, true); safety->process (L, R, n);
        for (auto& m : meter) m.store (0.0f);
    }
    else
    {
        engine->setParams (currentParams());
        engine->process (L, R, n);
        meter[mLow].store (engine->bandGr[0]); meter[mMid].store (engine->bandGr[1]); meter[mHigh].store (engine->bandGr[2]);
        meter[mGlue].store (engine->glueGr); meter[mLimit].store (engine->limiterGr());
    }

    if (! bypassed) { outLoud->process (L, R, n); tpMeter->process (L, R, n); }      // les mesures suivent le master, pas l'original
    if (! bypassed && inLoud->shortTerm > -50.0f && outLoud->shortTerm > -50.0f)
        matchDb += 0.02f * ((inLoud->shortTerm - outLoud->shortTerm) - matchDb);          // ecart de niveau entree / sortie, lisse
    for (int i = 0; i < n; ++i) { corrAcc[0] = 0.9995 * corrAcc[0] + (double) L[i] * R[i]; corrAcc[1] = 0.9995 * corrAcc[1] + (double) L[i] * L[i]; corrAcc[2] = 0.9995 * corrAcc[2] + (double) R[i] * R[i]; }
    correlation.store ((float) (corrAcc[0] / std::sqrt (std::max (1.0e-12, corrAcc[1] * corrAcc[2]))));

    pk = 0.0f; for (int i = 0; i < n; ++i) pk = std::max (pk, std::max (std::abs (L[i]), std::abs (R[i])));
    outPeak.store (std::max (outPeak.load (std::memory_order_relaxed), pk));
    lufsI.store (outLoud->integrated()); lufsS.store (outLoud->shortTerm); lufsM.store (outLoud->momentary);
    truePeak.store (gainToDb (tpMeter->maxPeak));
    int sp = scopePos.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i) { scope[sp] = 0.5f * (L[i] + R[i]); sp = (sp + 1) & (scopeSize - 1); }
    scopePos.store (sp, std::memory_order_relaxed);
}

void MJ7MasterProcessor::readScope (float* dest, int n, bool input) const noexcept
{
    const int sp = (input ? scopeInPos : scopePos).load (std::memory_order_relaxed);
    const float* src = input ? scopeIn : scope;
    for (int i = 0; i < n; ++i) dest[i] = src[(sp - n + i) & (scopeSize - 1)];
}

// ================================================================================================
//  Analyse
// ================================================================================================
void MJ7MasterProcessor::startAnalysis()
{
    if (state.load() == computing) return;
    stopThread (10000);
    if (hasAnalysis()) undoAnalysis();
    captureValid = false; waited = 0; capturePos.store (0); summary.clear();
    state.store (waiting);
}

void MJ7MasterProcessor::run()
{
    result = analyseMaster (capL.data(), capR.data(), captureLen, sr, analysisTarget, analysisLufs, analysisBase);
    resultReady.store (true);
}

void MJ7MasterProcessor::timerCallback()
{
    if (targetDirty.exchange (false) && captureValid && state.load() == done)
    {
        // nouvelle cible : on repart du style puis on refait le calcul sur l'extrait deja ecoute
        for (const auto& a : adjustments) setParam (a.pid, a.base);
        if (hasBaseLim) setParam (lim_gain, baseLimGain);
        adjustments.clear(); state.store (computing);
    }
    if (state.load() == computing && ! isThreadRunning() && ! resultReady.load())
    {
        captureValid = true;
        analysisTarget = factoryPresets()[(size_t) std::clamp (presetIndex, 0, (int) factoryPresets().size() - 1)].target;
        analysisLufs = targetLufs ((int) v (target));
        analysisBase = currentParams();
        startThread();
    }
    if (resultReady.exchange (false)) applyAnalysis();
    if (intensityDirty.exchange (false) && hasAnalysis()) applyIntensity();
    if (state.load() == failed && summary.isEmpty())
        summary.add (U8 ("Aucun son détecté. Lancez la lecture du morceau (de préférence le passage le plus fort), puis appuyez de nouveau."));
}

void MJ7MasterProcessor::parameterChanged (const juce::String& id, float)
{
    if (id == "target") targetDirty.store (true); else intensityDirty.store (true);
}

void MJ7MasterProcessor::setParam (int pid, float realValue)
{
    if (auto* p = apvts.getParameter (paramDef (pid).id))
    {
        const float norm = p->convertTo0to1 (realValue);
        if (std::abs (norm - p->getValue()) < 1.0e-6f) return;
        p->beginChangeGesture(); p->setValueNotifyingHost (norm); p->endChangeGesture();
    }
}

void MJ7MasterProcessor::applyAnalysis()
{
    const auto& r = result;
    adjustments.clear(); summary.clear();
    if (! r.valid)
    {
        captureValid = false; state.store (failed);
        summary.add (U8 ("Niveau trop faible ou extrait trop court pour être analysé. Vérifiez que la piste joue bien dans le plugin."));
        return;
    }
    auto add = [this] (int pid, float target) { adjustments.push_back ({ pid, v (pid), target }); };
    add (eq_low_g, clampf (v (eq_low_g) + r.lowG, -6.0f, 6.0f));
    add (eq_mud_g, clampf (v (eq_mud_g) + r.mudG, -6.0f, 3.0f));
    add (eq_pres_g, clampf (v (eq_pres_g) + r.presG, -6.0f, 6.0f));
    add (eq_air_g, clampf (v (eq_air_g) + r.airG, -6.0f, 6.0f));
    add (st_mono, r.monoBelow); add (st_width, r.width);
    add (mb_low_thr, r.thr[0]); add (mb_mid_thr, r.thr[1]); add (mb_high_thr, r.thr[2]); add (glue_thr, r.glueThr);
    baseLimGain = v (lim_gain); hasBaseLim = true;
    setParam (lim_gain, r.limGain);                       // le gain du limiteur vise toujours la cible exacte
    resetLoudness();                                      // la mesure integree repart de zero avec les nouveaux reglages
    applyIntensity();

    auto db = [] (float x) { return (x > 0.0f ? "+" : "") + juce::String (x, 1) + " dB"; };
    const auto& preset = factoryPresets()[(size_t) std::clamp (presetIndex, 0, (int) factoryPresets().size() - 1)];
    summary.add (U8 ("Mix d'origine : ") + juce::String (r.inLufs, 1) + U8 (" LUFS, crête vraie ") + juce::String (r.inTp, 1) + U8 (" dBTP, dynamique ") + juce::String (r.inPlr, 1) + " dB");
    summary.add (U8 ("Équilibre : basses ") + db (r.lowG) + U8 (", bas-médium ") + db (r.mudG) + U8 (", présence ") + db (r.presG) + ", air " + db (r.airG)
                 + U8 (" (profil ") + U8 (preset.name) + ")");
    summary.add (U8 ("Image : graves corrélés à ") + juce::String (r.corrLow, 2) + U8 (" → mono sous ") + juce::String (juce::roundToInt (r.monoBelow)) + " Hz, largeur "
                 + juce::String (juce::roundToInt (r.width)) + " %");
    summary.add (U8 ("Compression : seuils ") + juce::String (r.thr[0], 1) + " / " + juce::String (r.thr[1], 1) + " / " + juce::String (r.thr[2], 1) + " dB, glue " + juce::String (r.glueThr, 1) + " dB");
    summary.add (U8 ("Limiteur : ") + db (r.limGain) + U8 (" → ") + juce::String (r.outLufs, 1) + U8 (" LUFS prévus, crête ") + juce::String (r.outTp, 1) + " dBTP");
    summary.add (U8 ("Travail du limiteur : ") + juce::String (std::abs (r.limAvgGr) < 0.05f ? 0.0f : -r.limAvgGr, 1) + U8 (" dB en moyenne, ") + juce::String (std::abs (r.limMaxGr) < 0.05f ? 0.0f : -r.limMaxGr, 1) + U8 (" dB au maximum"));
    if (! r.reached)  summary.add (U8 ("Attention : cible non atteinte. Montez le niveau du mix ou choisissez une cible moins forte."));
    else if (r.heavy) summary.add (U8 ("Attention : cette cible demande beaucoup de limitation, le mix va perdre du punch. Une cible moins forte sonnera plus ouvert."));
    else if (r.alreadyLoud) summary.add (U8 ("Le mix est déjà plus fort que la cible : le plugin baisse le niveau."));
    else summary.add (U8 ("Vérifiez à l'oreille avec ÉCOUTER L'ORIGINAL : la comparaison se fait à niveau égal."));
    state.store (done);
}

void MJ7MasterProcessor::applyIntensity()
{
    const float t = v (intensity) * 0.01f;
    for (const auto& a : adjustments) setParam (a.pid, a.base + t * (a.target - a.base));
}

void MJ7MasterProcessor::undoAnalysis()
{
    for (const auto& a : adjustments) setParam (a.pid, a.base);
    if (hasBaseLim) setParam (lim_gain, baseLimGain);
    hasBaseLim = false; adjustments.clear(); summary.clear();
    if (state.load() == done || state.load() == failed) state.store (idle);
}

// ================================================================================================
//  Styles et etat
// ================================================================================================
void MJ7MasterProcessor::loadFactoryPreset (int index)
{
    const auto& presets = factoryPresets();
    index = std::clamp (index, 0, (int) presets.size() - 1);
    const bool reanalyse = captureValid && state.load() == done;
    adjustments.clear(); summary.clear(); hasBaseLim = false;
    std::vector<float> values ((size_t) kNumParams);
    for (int pid = 0; pid < kNumParams; ++pid) values[(size_t) pid] = paramDef (pid).def;
    for (const auto& [pid, value] : presets[(size_t) index].values) values[(size_t) pid] = value;
    for (int pid = 0; pid < kNumParams; ++pid)
        if (pid != bypass && pid != intensity && pid != target && pid != match && pid != in_gain) setParam (pid, values[(size_t) pid]);
    presetIndex = index;
    if (reanalyse) state.store (computing);
    else if (state.load() == done || state.load() == failed) state.store (idle);
}

void MJ7MasterProcessor::writeExtras (juce::ValueTree& tree) const
{
    tree.setProperty ("presetIndex", presetIndex, nullptr);
    juce::String adj;
    for (const auto& a : adjustments) adj << paramDef (a.pid).id << "=" << a.base << "," << a.target << ";";
    tree.setProperty ("analysis", adj, nullptr);
    tree.setProperty ("analysisSummary", summary.joinIntoString ("\n"), nullptr);
    tree.setProperty ("baseLimGain", hasBaseLim ? juce::var (baseLimGain) : juce::var(), nullptr);
}

void MJ7MasterProcessor::readExtras (const juce::ValueTree& tree)
{
    presetIndex = std::clamp ((int) tree.getProperty ("presetIndex", 0), 0, (int) factoryPresets().size() - 1);
    adjustments.clear(); summary.clear(); hasBaseLim = false;
    for (const auto& item : juce::StringArray::fromTokens (tree.getProperty ("analysis").toString(), ";", ""))
    {
        const auto id = item.upToFirstOccurrenceOf ("=", false, false);
        const auto vals = juce::StringArray::fromTokens (item.fromFirstOccurrenceOf ("=", false, false), ",", "");
        for (int pid = 0; pid < kNumParams && vals.size() == 2; ++pid)
            if (id == paramDef (pid).id) { adjustments.push_back ({ pid, vals[0].getFloatValue(), vals[1].getFloatValue() }); break; }
    }
    if (tree.hasProperty ("baseLimGain") && ! tree.getProperty ("baseLimGain").isVoid()) { baseLimGain = (float) tree.getProperty ("baseLimGain"); hasBaseLim = true; }
    if (! adjustments.empty())
    {
        summary = juce::StringArray::fromLines (tree.getProperty ("analysisSummary").toString()); summary.removeEmptyStrings();
        state.store (done);
    }
    else if (state.load() == done || state.load() == failed) state.store (idle);
}

void MJ7MasterProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto tree = apvts.copyState(); writeExtras (tree);
    if (auto xml = tree.createXml()) copyXmlToBinary (*xml, dest);
}

void MJ7MasterProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            apvts.replaceState (tree);
            readExtras (tree);
            targetDirty.store (false); intensityDirty.store (false);
        }
}

juce::File MJ7MasterProcessor::presetFolder()
{
    auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("MJ7 Master Chain").getChildFile ("Presets");
    dir.createDirectory();
    return dir;
}

bool MJ7MasterProcessor::savePresetFile (const juce::File& file)
{
    auto tree = apvts.copyState(); writeExtras (tree);
    if (auto xml = tree.createXml()) return xml->writeTo (file);
    return false;
}

bool MJ7MasterProcessor::loadPresetFile (const juce::File& file)
{
    if (auto xml = juce::XmlDocument::parse (file))
        if (xml->hasTagName (apvts.state.getType()))
        {
            juce::MemoryBlock block; copyXmlToBinary (*xml, block);
            setStateInformation (block.getData(), (int) block.getSize());
            return true;
        }
    return false;
}

void MJ7MasterProcessor::toggleAB()
{
    abState[abIndex] = apvts.copyState();
    abIndex ^= 1;
    if (abState[abIndex].isValid()) { adjustments.clear(); hasBaseLim = false; apvts.replaceState (abState[abIndex].createCopy()); }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MJ7MasterProcessor(); }
