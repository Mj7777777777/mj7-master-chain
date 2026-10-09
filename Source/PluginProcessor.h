// MJ7 Master Chain - coeur du plugin : parametres, chaine de mastering, analyse, styles.
#pragma once
#include "Params.h"

class MJ7MasterProcessor final : public juce::AudioProcessor,
                                 private juce::Timer,
                                 private juce::Thread,
                                 private juce::AudioProcessorValueTreeState::Listener
{
public:
    enum AnalysisState { idle = 0, waiting, listening, computing, done, failed };

    MJ7MasterProcessor();
    ~MJ7MasterProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override { return apvts.getParameter ("bypass"); }
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "MJ7 Master Chain"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    void startAnalysis();
    void undoAnalysis();
    AnalysisState analysisState() const noexcept { return (AnalysisState) state.load(); }
    float analysisProgress() const noexcept { return captureLen > 0 ? (float) capturePos.load() / (float) captureLen : 0.0f; }
    float analysisSeconds() const noexcept { return 20.0f; }
    bool hasAnalysis() const noexcept { return ! adjustments.empty(); }
    juce::StringArray summary;

    int currentPreset() const noexcept { return presetIndex; }
    void loadFactoryPreset (int index);
    bool savePresetFile (const juce::File&);
    bool loadPresetFile (const juce::File&);
    static juce::File presetFolder();
    void toggleAB();
    int abSlot() const noexcept { return abIndex; }
    void resetLoudness() noexcept { resetRequest.store (true); }

    // --- mesures lues par l'interface ---
    std::atomic<float> meter[mj7::kNumMeters];
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f };
    std::atomic<float> lufsI { -120.0f }, lufsS { -120.0f }, lufsM { -120.0f }, truePeak { -120.0f }, correlation { 1.0f };
    void readScope (float* dest, int n, bool input = false) const noexcept;

private:
    void timerCallback() override;
    void run() override;
    void parameterChanged (const juce::String&, float) override;
    void processChunk (float* L, float* R, int n);
    void applyAnalysis();
    void applyIntensity();
    void setParam (int pid, float realValue);
    void writeExtras (juce::ValueTree&) const;
    void readExtras (const juce::ValueTree&);
    mj7::MasterParams currentParams() const noexcept;
    float v (int pid) const noexcept { return raw[pid]->load (std::memory_order_relaxed); }
    bool on (int pid) const noexcept { return v (pid) > 0.5f; }

    std::atomic<float>* raw[mj7::kNumParams] = {};
    float sr = 48000.0f; int latency = 0;

    std::unique_ptr<mj7::MasterEngine> engine;
    std::unique_ptr<mj7::LoudnessMeter> outLoud, inLoud;
    std::unique_ptr<mj7::TruePeakMeter> tpMeter;
    mj7::FixedDelay bypassL, bypassR; mj7::Smooth dryGainSm;
    std::unique_ptr<mj7::TruePeakLimiter> safety;          // protege l'ecoute de l'original remonte a niveau egal
    bool wasBypassed = false; float matchDb = 0.0f; double corrAcc[3] = {};
    std::atomic<bool> resetRequest { false };
    static constexpr int chunk = mj7::MasterEngine::kMaxBlock;
    float tmpR[chunk] = {};

    static constexpr int scopeSize = 4096;
    float scope[scopeSize] = {}, scopeIn[scopeSize] = {}; std::atomic<int> scopePos { 0 }, scopeInPos { 0 };

    // --- analyse ---
    std::atomic<int> state { idle }, capturePos { 0 };
    std::atomic<bool> resultReady { false }, intensityDirty { false }, targetDirty { false };
    std::vector<float> capL, capR; int captureLen = 0, waited = 0; bool captureValid = false;
    mj7::MasterAnalysis result; mj7::GenreTarget analysisTarget; float analysisLufs = -14.0f; mj7::MasterParams analysisBase;
    struct Adjustment { int pid; float base, target; };
    std::vector<Adjustment> adjustments; float baseLimGain = 0.0f; bool hasBaseLim = false;

    int presetIndex = 0, abIndex = 0; juce::ValueTree abState[2];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MJ7MasterProcessor)
};
