#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "dsp/VocalEngine.h"

namespace lavc
{
// Every parameter: X (enum name, id string)
#define LAVC_PARAMETERS(X)                                                                                         \
    X (mode, "mode")                                                                                                \
    X (inGain, "inGain") X (phase, "phase") X (lowCut, "lowCut") X (lowCutSlope, "lowCutSlope")                     \
    X (tuneOn, "tuneOn") X (tuneKey, "tuneKey") X (tuneScale, "tuneScale") X (tuneTarget, "tuneTarget")             \
    X (tuneSpeed, "tuneSpeed") X (tuneTolerance, "tuneTolerance") X (tuneAmount, "tuneAmount")                      \
    X (transpose, "transpose") X (formant, "formant")                                                               \
    X (harmOn, "harmOn") X (harmMode, "harmMode") X (harmHumanize, "harmHumanize") X (harmLevel, "harmLevel")       \
    X (hv1On, "hv1On") X (hv1Interval, "hv1Interval") X (hv1Level, "hv1Level") X (hv1Pan, "hv1Pan") X (hv1Formant, "hv1Formant") \
    X (hv2On, "hv2On") X (hv2Interval, "hv2Interval") X (hv2Level, "hv2Level") X (hv2Pan, "hv2Pan") X (hv2Formant, "hv2Formant") \
    X (hv3On, "hv3On") X (hv3Interval, "hv3Interval") X (hv3Level, "hv3Level") X (hv3Pan, "hv3Pan") X (hv3Formant, "hv3Formant") \
    X (hv4On, "hv4On") X (hv4Interval, "hv4Interval") X (hv4Level, "hv4Level") X (hv4Pan, "hv4Pan") X (hv4Formant, "hv4Formant") \
    X (gateOn, "gateOn") X (gateThreshold, "gateThreshold") X (gateRange, "gateRange") X (gateAttack, "gateAttack")  \
    X (gateHold, "gateHold") X (gateRelease, "gateRelease") X (breath, "breath") X (breathSens, "breathSens")       \
    X (levelOn, "levelOn") X (levelTarget, "levelTarget") X (levelRange, "levelRange") X (levelSpeed, "levelSpeed") \
    X (deessOn, "deessOn") X (deessFreq, "deessFreq") X (deessThreshold, "deessThreshold") X (deessRange, "deessRange") \
    X (deessMode, "deessMode") X (deessListen, "deessListen")                                                       \
    X (sculptOn, "sculptOn") X (sculptStyle, "sculptStyle") X (sculptAmount, "sculptAmount")                        \
    X (sculptSpeed, "sculptSpeed") X (sculptTilt, "sculptTilt")                                                     \
    X (eqOn, "eqOn")                                                                                                \
    X (eq1Freq, "eq1Freq") X (eq1Gain, "eq1Gain") X (eq1Q, "eq1Q")                                                  \
    X (eq2Freq, "eq2Freq") X (eq2Gain, "eq2Gain") X (eq2Q, "eq2Q")                                                  \
    X (eq3Freq, "eq3Freq") X (eq3Gain, "eq3Gain") X (eq3Q, "eq3Q")                                                  \
    X (eq4Freq, "eq4Freq") X (eq4Gain, "eq4Gain") X (eq4Q, "eq4Q")                                                  \
    X (eq5Freq, "eq5Freq") X (eq5Gain, "eq5Gain") X (eq5Q, "eq5Q")                                                  \
    X (compOn, "compOn") X (compMode, "compMode") X (compThreshold, "compThreshold") X (compRatio, "compRatio")      \
    X (compAttack, "compAttack") X (compRelease, "compRelease") X (compMakeup, "compMakeup") X (compMix, "compMix")  \
    X (satOn, "satOn") X (satMode, "satMode") X (satDrive, "satDrive") X (satTone, "satTone") X (satMix, "satMix")   \
    X (dblOn, "dblOn") X (dblAmount, "dblAmount") X (dblDetune, "dblDetune") X (dblDelay, "dblDelay")               \
    X (dblWidth, "dblWidth") X (dblDrift, "dblDrift")                                                               \
    X (modOn, "modOn") X (modMode, "modMode") X (modRate, "modRate") X (modDepth, "modDepth")                       \
    X (modFeedback, "modFeedback") X (modMix, "modMix")                                                             \
    X (dlyOn, "dlyOn") X (dlyMode, "dlyMode") X (dlyTime, "dlyTime") X (dlySync, "dlySync")                         \
    X (dlyFeedback, "dlyFeedback") X (dlyHp, "dlyHp") X (dlyLp, "dlyLp") X (dlyDuck, "dlyDuck")                     \
    X (dlyWidth, "dlyWidth") X (dlyMix, "dlyMix")                                                                   \
    X (revOn, "revOn") X (revType, "revType") X (revDecay, "revDecay") X (revPredelay, "revPredelay")               \
    X (revSize, "revSize") X (revDamp, "revDamp") X (revLowCut, "revLowCut") X (revDuck, "revDuck")                 \
    X (revWidth, "revWidth") X (revMix, "revMix")                                                                   \
    X (outGain, "outGain") X (outWidth, "outWidth") X (outMix, "outMix") X (limOn, "limOn") X (limCeiling, "limCeiling")

namespace ParamID
{
#define LAVC_DECLARE_ID(name, id) inline constexpr auto name = id;
LAVC_PARAMETERS (LAVC_DECLARE_ID)
#undef LAVC_DECLARE_ID
} // namespace ParamID

enum class P : int
{
#define LAVC_DECLARE_ENUM(name, id) name,
    LAVC_PARAMETERS (LAVC_DECLARE_ENUM)
#undef LAVC_DECLARE_ENUM
    count
};

/** IDs of the enable switch of each orderable module (indexed by ModuleId). */
const char* moduleEnableID (ModuleId m) noexcept;

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** Cached raw parameter pointers so the audio thread can build EngineParams cheaply. */
class ParameterCache
{
public:
    explicit ParameterCache (juce::AudioProcessorValueTreeState& state);
    /** Fills everything except the module order and tempo. */
    void read (EngineParams& p) const noexcept;
    float get (P p) const noexcept { return values[(size_t) p]->load (std::memory_order_relaxed); }

private:
    std::array<std::atomic<float>*, (size_t) P::count> values {};
};

struct FactoryPreset
{
    FactoryPreset (juce::String n, std::vector<std::pair<const char*, float>> v, juce::String o = {})
        : name (std::move (n)), values (std::move (v)), order (std::move (o)) {}

    juce::String name;
    std::vector<std::pair<const char*, float>> values;
    juce::String order; // empty = default order
};

const std::vector<FactoryPreset>& getFactoryPresets();

/** Text for a harmony interval (scale degrees, semitones or MIDI voice index). */
juce::String harmonyIntervalText (int interval, int harmMode);
} // namespace lavc
