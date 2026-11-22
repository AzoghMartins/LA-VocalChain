#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "Effects.h"
#include "Modules.h"
#include "PitchEngine.h"

namespace lavc
{
/** Correction modules whose order can be changed by the user. */
enum class ModuleId { gate, level, deess, sculpt, eq, comp, sat, numModules };
constexpr int kNumOrderable = (int) ModuleId::numModules;
using ModuleOrder = std::array<int, kNumOrderable>;

const char* moduleShortName (ModuleId m) noexcept;
ModuleOrder defaultModuleOrder() noexcept;
/** Packs / unpacks an order into a string such as "0123456" (invalid strings give the default). */
ModuleOrder orderFromString (const char* text) noexcept;
void orderToString (const ModuleOrder& order, char* out7plusNull) noexcept;

struct EngineParams
{
    bool tracking = false;

    // Input
    float inputGainDb = 0.0f;
    bool phaseInvert = false;
    float lowCutHz = 80.0f; // <= 20 = off
    bool lowCut24 = false;

    PitchParams pitch;
    GateParams gate;
    LevelerParams level;
    DeEsserParams deess;
    SculptParams sculpt;
    EqParams eq;
    CompParams comp;
    SatParams sat;
    ModuleOrder order = defaultModuleOrder();

    DoublerParams doubler;
    ModParams mod;
    DelayParams delay;
    ReverbParams reverb;
    double bpm = 120.0;

    // Output
    float outputGainDb = 0.0f;
    float width = 1.0f;      // 0..2
    float mix = 1.0f;        // global dry / wet
    bool limiterEnabled = true;
    float ceilingDb = -1.0f;
};

/** Per-block meter values for the UI. */
struct MeterData
{
    float inPeak = 0.0f, outPeak = 0.0f;
    float gateDb = 0.0f, levelDb = 0.0f, deessDb = 0.0f, compDb = 0.0f, limitDb = 0.0f;
    bool breath = false;
    std::array<float, kSculptBands> sculpt {};
};

/** Statistics gathered while the Assistant listens. */
struct AssistStats
{
    static constexpr int kBins = 120; // 1 dB bins from -120 to 0 dBFS
    std::array<int, kBins> allFrames {}, voicedFrames {}, sibilance {};
    std::array<int, 128> pitchHist {};
    int64_t samples = 0;
    void clear() { *this = AssistStats(); }

    /** Value at the given percentile of a histogram (dBFS), or `fallback` if empty. */
    static float percentile (const std::array<int, kBins>& h, float pct, float fallback);
};

/**
    Complete vocal chain, independent of JUCE:
    Input -> Pitch (tune + harmonies) -> correction modules (re-orderable) -> Doubler -> Modulation
    -> Delay -> Reverb -> Output (width, gain, limiter).
*/
class VocalEngine
{
public:
    void prepare (double sampleRate, int maxBlockSize, bool trackingMode);
    void reset();
    void setParams (const EngineParams& p) noexcept;

    /** Processes up to maxBlockSize samples in place. `channels` is 1 or 2. */
    void process (float* const* io, int channels, int numSamples, PitchListener* listener) noexcept;

    int getLatency() const noexcept { return totalLatency; }
    bool isTracking() const noexcept { return tracking; }
    const MeterData& getMeters() const noexcept { return meters; }
    PitchEngine& getPitchEngine() noexcept { return pitch; }

    void setAssistActive (bool on) noexcept;
    const AssistStats& getAssistStats() const noexcept { return assist; }

    /** Mono copy of the output for the spectrum analyser (valid after process()). */
    const float* getAnalysisBuffer() const noexcept { return analysisBuf.data(); }

private:
    void runModule (ModuleId id, float* l, float* r, int n) noexcept;
    void gatherAssist (const float* l, const float* r, int n) noexcept;

    double sr = 48000.0;
    int maxBlock = 512;
    bool tracking = false;
    EngineParams params;

    Biquad lowCut1L, lowCut1R, lowCut2L, lowCut2R;
    float lastLowCut = -1.0f;
    bool lastLowCut24 = false;
    Smoothed inGain, outGain, widthSm, mixSm;

    PitchEngine pitch;
    GateModule gate;
    LevelerModule level;
    DeEsserModule deess;
    SculptModule sculpt;
    EqModule eq;
    CompressorModule comp;
    SaturationModule sat;
    DoublerModule doubler;
    ModulationModule modulation;
    DelayModule delay;
    ReverbModule reverb;
    LimiterModule limiter;

    std::vector<float> monoIn, leadBuf, harmL, harmR, workL, workR, dryL, dryR, analysisBuf;
    std::vector<uint8_t> voiced;
    std::array<LatencyDelay, 2> dryDelay;
    LatencyDelay sideDelay;
    int totalLatency = 0;

    MeterData meters;

    bool assistOn = false;
    AssistStats assist;
    Biquad assistHf;
    double assistAcc = 0.0, assistHfAcc = 0.0;
    int assistCount = 0, assistVoicedCount = 0;
};
} // namespace lavc
