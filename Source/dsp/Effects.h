#pragma once

#include <array>
#include <vector>

#include "DspUtils.h"

namespace lavc
{
/** Two-tap crossfading delay-line pitch shifter, used for small detunes. */
class MicroShifter
{
public:
    void prepare (double sampleRate, float windowMs, float maxExtraDelayMs);
    void reset();
    /** cents: detune, extraDelay: base delay in samples added to the read taps. */
    float process (float x, float cents, float extraDelay) noexcept;

private:
    double sr = 48000.0;
    DelayLine line;
    float window = 1000.0f;
    double phase = 0.0;
};

//==============================================================================
struct DoublerParams
{
    bool enabled = false;
    float amount = 0.6f;    // level of the doubles (0..1)
    float detuneCents = 10.0f;
    float delayMs = 18.0f;
    float width = 0.8f;     // 0..1
    float drift = 0.5f;     // humanise 0..1
};

/** ADT-style doubler: two detuned, delayed and drifting copies panned left and right. */
class DoublerModule
{
public:
    void prepare (double sampleRate);
    void reset();
    void setParams (const DoublerParams& p) noexcept { params = p; }
    void process (float* l, float* r, int n) noexcept;

private:
    double sr = 48000.0;
    DoublerParams params;
    std::array<MicroShifter, 2> shifters;
    std::array<RandomLfo, 2> timeLfo { RandomLfo (11u), RandomLfo (23u) };
    std::array<RandomLfo, 2> pitchLfo { RandomLfo (37u), RandomLfo (51u) };
    std::array<Biquad, 2> tone;
    Smoothed amountSm;
};

//==============================================================================
enum class ModMode { chorus, flanger, phaser, numModes };

struct ModParams
{
    bool enabled = false;
    ModMode mode = ModMode::chorus;
    float rateHz = 0.6f;
    float depth = 0.5f;     // 0..1
    float feedback = 0.2f;  // 0..0.95
    float mix = 0.35f;      // 0..1
};

class ModulationModule
{
public:
    void prepare (double sampleRate);
    void reset();
    void setParams (const ModParams& p) noexcept { params = p; }
    void process (float* l, float* r, int n) noexcept;

private:
    double sr = 48000.0;
    ModParams params;
    std::array<DelayLine, 2> lines;
    SineLfo lfo;
    std::array<float, 2> fb {};
    std::array<std::array<float, 6>, 2> apState {};
};

//==============================================================================
enum class DelayMode { digital, tape, pingPong, slapback, numModes };

struct DelayParams
{
    bool enabled = false;
    DelayMode mode = DelayMode::digital;
    float timeMs = 375.0f;
    int sync = 0;          // 0 = free (ms), otherwise index into note values
    float feedback = 0.3f; // 0..0.95
    float hpHz = 200.0f;
    float lpHz = 6000.0f;
    float duck = 0.5f;     // 0..1
    float width = 1.0f;    // 0..1
    float mix = 0.2f;      // wet level 0..1
};

/** Note values for tempo sync (in quarter notes); index 0 = free. */
int numDelaySyncOptions() noexcept;
const char* delaySyncName (int index) noexcept;
double delaySyncBeats (int index) noexcept;

class DelayModule
{
public:
    void prepare (double sampleRate);
    void reset();
    void setParams (const DelayParams& p, double bpm) noexcept;
    /** Send style: wet is added to l / r. */
    void process (float* l, float* r, int n) noexcept;

private:
    double sr = 48000.0;
    DelayParams params;
    double bpm = 120.0;
    std::array<DelayLine, 2> lines;
    std::array<Biquad, 2> hp, lp;
    Smoothed timeSm;
    EnvelopeFollower duckEnv;
    SineLfo wow;
    RandomLfo flutter { 77u };
    float lastHp = -1.0f, lastLp = -1.0f;
};

//==============================================================================
enum class ReverbType { plate, room, hall, chamber, numTypes };

struct ReverbParams
{
    bool enabled = false;
    ReverbType type = ReverbType::plate;
    float decayS = 1.8f;
    float predelayMs = 20.0f;
    float size = 0.5f;     // 0..1
    float damping = 0.5f;  // 0..1 (high frequency damping)
    float lowCutHz = 250.0f;
    float duck = 0.3f;
    float width = 1.0f;
    float mix = 0.18f;     // wet level 0..1
};

/** 8-line feedback delay network reverb with input diffusion and early reflections. */
class ReverbModule
{
public:
    static constexpr int kLines = 8;

    void prepare (double sampleRate);
    void reset();
    void setParams (const ReverbParams& p) noexcept;
    /** Send style: wet is added to l / r. */
    void process (float* l, float* r, int n) noexcept;

private:
    void configure() noexcept;

    double sr = 48000.0;
    ReverbParams params, configured;
    bool needsConfigure = true;
    DelayLine predelay;
    std::array<DelayLine, 4> diffusers;
    std::array<int, 4> diffLen {};
    float diffGain = 0.7f;
    std::array<DelayLine, kLines> lines;
    std::array<float, kLines> lineLen {}, lineGain {}, dampState {};
    float dampCoeff = 0.3f, erGain = 0.0f;
    std::array<int, 6> erTaps {};
    std::array<Biquad, 2> lowCut;
    std::array<SineLfo, 2> mod;
    EnvelopeFollower duckEnv;
    float modDepth = 6.0f;
};
} // namespace lavc
