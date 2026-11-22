#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "DspUtils.h"

namespace lavc
{
//==============================================================================
struct GateParams
{
    bool enabled = false;
    float thresholdDb = -50.0f;
    float rangeDb = -40.0f;    // attenuation when closed
    float attackMs = 1.0f;
    float holdMs = 50.0f;
    float releaseMs = 150.0f;
    float breathDb = 0.0f;     // breath reduction (0 = off, negative dB)
    float breathSens = 50.0f;  // 0..100
};

/** Downward expander/gate with breath detection and reduction. */
class GateModule
{
public:
    void prepare (double sampleRate);
    void reset();
    void setParams (const GateParams& p) noexcept { params = p; }
    void process (float* l, float* r, const uint8_t* voiced, int n) noexcept;
    float getReductionDb() const noexcept { return grDb; }
    bool isBreathActive() const noexcept { return breathActive; }

private:
    double sr = 48000.0;
    GateParams params;
    Biquad keyHp, hfHp;
    EnvelopeFollower peakEnv, rmsEnv, hfEnv;
    float gain = 1.0f, gainDb = 0.0f, breathGain = 1.0f, phraseDb = -60.0f, grDb = 0.0f;
    int holdCounter = 0, breathCounter = 0;
    bool open = false, breathActive = false;
};

//==============================================================================
struct LevelerParams
{
    bool enabled = false;
    float targetDb = -18.0f;
    float rangeDb = 9.0f;
    float speedMs = 300.0f;
};

/** Automatic level rider: slowly rides the vocal towards a target loudness. */
class LevelerModule
{
public:
    void prepare (double sampleRate);
    void reset();
    void setParams (const LevelerParams& p) noexcept { params = p; }
    void process (float* l, float* r, int n) noexcept;
    float getGainDb() const noexcept { return gainDb; }

private:
    double sr = 48000.0;
    LevelerParams params;
    Biquad weightHp, weightShelf;
    float meanSquare = 0.0f, msCoeffV = 0.0f, gainDb = 0.0f;
};

//==============================================================================
struct DeEsserParams
{
    bool enabled = false;
    float freqHz = 6500.0f;
    float thresholdDb = -30.0f;
    float rangeDb = 10.0f;
    int mode = 0;      // 0 = split band, 1 = wide band
    bool listen = false;
};

class DeEsserModule
{
public:
    void prepare (double sampleRate);
    void reset();
    void setParams (const DeEsserParams& p) noexcept;
    void process (float* l, float* r, int n) noexcept;
    float getReductionDb() const noexcept { return grDb; }

private:
    double sr = 48000.0;
    DeEsserParams params;
    float lastFreq = -1.0f;
    Biquad scHp1, scHp2;
    LR4Crossover splitL, splitR;
    EnvelopeFollower env;
    float gainSm = 1.0f, grDb = 0.0f;
};

//==============================================================================
constexpr int kSculptBands = 8;
constexpr int kSculptStyles = 9;
const char* sculptStyleName (int style) noexcept;

struct SculptParams
{
    bool enabled = false;
    int style = 0;
    float amount = 0.5f;   // 0..1
    float speedMs = 400.0f;
    float tiltDb = 0.0f;
};

/** Adaptive resonance / tonal balance matcher: steers the vocal's spectrum towards a target style. */
class SculptModule
{
public:
    static constexpr std::array<float, kSculptBands> bandFreqs { 120.0f, 250.0f, 500.0f, 1000.0f, 2000.0f, 3500.0f, 6000.0f, 10000.0f };

    void prepare (double sampleRate);
    void reset();
    void setParams (const SculptParams& p) noexcept { params = p; }
    void process (float* l, float* r, int n) noexcept;
    const std::array<float, kSculptBands>& getBandGains() const noexcept { return applied; }

private:
    void updateFilters() noexcept;

    double sr = 48000.0;
    SculptParams params;
    std::array<Biquad, kSculptBands> analysis;
    std::array<float, kSculptBands> power {}, applied {};
    std::array<Biquad, kSculptBands> eqL, eqR;
    float powerCoeff = 0.0f, levelMs = 0.0f;
    int counter = 0;
};

//==============================================================================
constexpr int kEqBands = 5;

struct EqParams
{
    bool enabled = true;
    std::array<float, kEqBands> freq { 120.0f, 300.0f, 1500.0f, 4000.0f, 10000.0f };
    std::array<float, kEqBands> gain {};
    std::array<float, kEqBands> q { 0.7f, 1.0f, 1.0f, 1.0f, 0.7f };
};

/** 5-band vocal EQ: low shelf, three bells, air shelf. */
class EqModule
{
public:
    void prepare (double sampleRate) { sr = sampleRate; last = {}; last.freq[0] = -1.0f; }
    void reset() { for (auto& f : filtL) f.reset(); for (auto& f : filtR) f.reset(); }
    void setParams (const EqParams& p) noexcept;
    void process (float* l, float* r, int n) noexcept;

    /** Designs the filter for one band (shared with the UI to draw the curve). */
    static void design (Biquad& b, double sr, int band, float freq, float gainDb, float q) noexcept;

private:
    double sr = 48000.0;
    EqParams params, last;
    std::array<Biquad, kEqBands> filtL, filtR;
};

//==============================================================================
enum class CompMode { clean, opto, fet, tube, multi, numModes };

struct CompParams
{
    bool enabled = false;
    CompMode mode = CompMode::clean;
    float thresholdDb = -20.0f;
    float ratio = 3.0f;
    float attackMs = 10.0f;
    float releaseMs = 120.0f;
    float makeupDb = 0.0f;
    float mix = 1.0f;
};

class CompressorModule
{
public:
    void prepare (double sampleRate);
    void reset();
    void setParams (const CompParams& p) noexcept;
    void process (float* l, float* r, int n) noexcept;
    float getReductionDb() const noexcept { return grDb; }

private:
    struct Detector
    {
        float envDb = 0.0f, rms = 0.0f, charge = 0.0f;
    };
    float computeGr (Detector& d, float levelDb, float kneeDb, float attackCoeff, float releaseCoeff) noexcept;

    double sr = 48000.0;
    CompParams params;
    std::array<Detector, 3> det;
    std::array<LR4Crossover, 2> xLow, xHigh, xComp; // per channel
    float rmsCoeff = 0.0f, chargeCoeff = 0.0f, grDb = 0.0f;
};

//==============================================================================
enum class SatMode { tape, tube, console, exciter, radio, numModes };

struct SatParams
{
    bool enabled = false;
    SatMode mode = SatMode::tape;
    float driveDb = 6.0f;
    float tone = 0.0f;   // -1..1 (dark..bright)
    float mix = 1.0f;
};

/** Saturation with 2x oversampling. Always adds Oversampler2x::latency() samples of delay. */
class SaturationModule
{
public:
    void prepare (double sampleRate);
    void reset();
    void setParams (const SatParams& p) noexcept;
    void process (float* l, float* r, int n) noexcept;
    static int getLatency() noexcept { return Oversampler2x::latency(); }

private:
    float shape (float x, int ch) noexcept;

    double sr = 48000.0;
    SatParams params;
    std::array<Oversampler2x, 2> os;
    std::array<LatencyDelay, 2> dry;
    std::array<Biquad, 2> exciterHp, toneLow, toneHigh, radioHp, radioLp, dcBlock, tapeLp;
    float lastTone = 1000.0f;
    SatMode lastMode = SatMode::numModes;
    Smoothed driveSm;
};

//==============================================================================
/** Look-ahead peak limiter (look-ahead only in mixing mode). */
class LimiterModule
{
public:
    void prepare (double sampleRate, int lookaheadSamples);
    void reset();
    void process (float* l, float* r, int n, float ceilingDb, bool enabled) noexcept;
    int getLatency() const noexcept { return lookahead; }
    float getReductionDb() const noexcept { return grDb; }

private:
    double sr = 48000.0;
    int lookahead = 0;
    std::array<DelayLine, 2> delay;
    std::vector<float> gainRing;
    int ringPos = 0;
    float env = 1.0f, relCoeff = 0.0f, grDb = 0.0f;
    std::vector<float> smoothRing;
    int smoothPos = 0;
    double smoothSum = 0.0;
};
} // namespace lavc
