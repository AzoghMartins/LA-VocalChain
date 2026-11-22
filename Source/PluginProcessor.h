#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Parameters.h"
#include "dsp/VocalEngine.h"

namespace lavc
{
/** Lock-free single-producer / single-consumer queue (audio thread -> UI). */
template <typename T, int Capacity>
class SpscQueue
{
public:
    bool push (const T& item) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (1, s1, n1, s2, n2);
        if (n1 + n2 == 0)
            return false;
        data[(size_t) (n1 > 0 ? s1 : s2)] = item;
        fifo.finishedWrite (1);
        return true;
    }

    bool pop (T& item) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (1, s1, n1, s2, n2);
        if (n1 + n2 == 0)
            return false;
        item = data[(size_t) (n1 > 0 ? s1 : s2)];
        fifo.finishedRead (1);
        return true;
    }

    void clear() noexcept { fifo.reset(); }

private:
    juce::AbstractFifo fifo { Capacity };
    std::array<T, (size_t) Capacity> data {};
};

/** Block-based float FIFO for the spectrum analyser. */
class SampleFifo
{
public:
    void push (const float* data, int n) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToWrite (n, s1, n1, s2, n2);
        std::copy (data, data + n1, buffer.begin() + s1);
        std::copy (data + n1, data + n1 + n2, buffer.begin() + s2);
        fifo.finishedWrite (n1 + n2);
    }
    int pop (float* dest, int maxN) noexcept
    {
        int s1, n1, s2, n2;
        fifo.prepareToRead (maxN, s1, n1, s2, n2);
        std::copy (buffer.begin() + s1, buffer.begin() + s1 + n1, dest);
        std::copy (buffer.begin() + s2, buffer.begin() + s2 + n2, dest + n1);
        fifo.finishedRead (n1 + n2);
        return n1 + n2;
    }

private:
    static constexpr int size = 1 << 15;
    juce::AbstractFifo fifo { size };
    std::array<float, size> buffer {};
};

/** Meter values published by the audio thread. */
struct AtomicMeters
{
    std::atomic<float> inPeak { 0.0f }, outPeak { 0.0f }, gate { 0.0f }, level { 0.0f }, deess { 0.0f }, comp { 0.0f }, limit { 0.0f };
    std::atomic<bool> breath { false };
    std::array<std::atomic<float>, kSculptBands> sculpt {};
};

class VocalChainProcessor : public juce::AudioProcessor, private juce::AudioProcessorValueTreeState::Listener,
                            private juce::AsyncUpdater
{
public:
    VocalChainProcessor();
    ~VocalChainProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return (int) getFactoryPresets().size(); }
    int getCurrentProgram() override { return currentProgram; }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- UI interface (message thread) ------------------------------------------------------
    juce::AudioProcessorValueTreeState& getState() noexcept { return apvts; }

    ModuleOrder getModuleOrder() const noexcept;
    void setModuleOrder (const ModuleOrder& order);

    /** Assistant: listens to the vocal, then sets gain staging, gate, de-esser, low cut, compressor and key. */
    void startAssist();
    /** Stops listening and applies the result. Returns a short summary (empty if nothing was learned). */
    juce::String finishAssist();
    bool isAssistRunning() const noexcept { return assistState.load() == 1; }
    double getAssistSeconds() const noexcept { return assistSeconds.load(); }

    void startKeyLearn();
    juce::String finishKeyLearn();
    bool isKeyLearnRunning() const noexcept { return keyLearn.load(); }
    KeyEstimate getLastKeyEstimate() const noexcept;

    // User presets
    static juce::File getUserPresetFolder();
    bool saveUserPreset (const juce::String& name);
    bool loadUserPreset (const juce::File& file);
    juce::String getCurrentPresetName() const { return currentPresetName; }

    double getEngineSampleRate() const noexcept { return engineSampleRate.load(); }
    bool isTrackingMode() const noexcept { return engineTracking.load(); }

    SpscQueue<PitchFrame, 4096> pitchQueue;
    SampleFifo spectrumFifo;
    AtomicMeters meters;
    std::atomic<float> currentInputMidi { 0.0f }, currentOutputMidi { 0.0f };
    std::atomic<bool> currentVoiced { false };

private:
    struct Listener : PitchListener
    {
        VocalChainProcessor* owner = nullptr;
        void pitchFrame (const PitchFrame& f) override
        {
            owner->pitchQueue.push (f);
            owner->currentVoiced.store (f.voiced, std::memory_order_relaxed);
            if (f.voiced)
            {
                owner->currentInputMidi.store (f.inputMidi, std::memory_order_relaxed);
                owner->currentOutputMidi.store (f.outputMidi, std::memory_order_relaxed);
            }
        }
    };

    void parameterChanged (const juce::String& id, float value) override;
    void handleAsyncUpdate() override;
    void rebuildEngine();
    void setParam (const char* id, float value);
    void resetToDefaults();
    void handleMidi (const juce::MidiBuffer& midi) noexcept;

    juce::AudioProcessorValueTreeState apvts;
    ParameterCache params;
    VocalEngine engine;
    EngineParams engineParams;
    Listener listener;

    std::atomic<uint32_t> packedOrder { 0 };
    std::atomic<int> assistState { 0 }; // 0 idle, 1 running, 2 stop requested, 3 stopped
    std::atomic<double> assistSeconds { 0.0 };
    std::atomic<bool> keyLearn { false }, keyLearnActive { false };
    KeyEstimate lastKey;

    std::atomic<double> engineSampleRate { 48000.0 };
    std::atomic<bool> engineTracking { false };
    int preparedBlockSize = 512;
    double preparedRate = 0.0;
    int currentProgram = 0;
    juce::String currentPresetName { "Default" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VocalChainProcessor)
};
} // namespace lavc
