#include "Parameters.h"

namespace lavc
{
namespace
{
using APF = juce::AudioParameterFloat;
using APFA = juce::AudioParameterFloatAttributes;

juce::String formatDb (float v, int) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; }
juce::String formatMs (float v, int) { return (v < 10.0f ? juce::String (v, 1) : juce::String (juce::roundToInt (v))) + " ms"; }
juce::String formatPct (float v, int) { return juce::String (juce::roundToInt (v)) + " %"; }
juce::String formatHz (float v, int = 0)
{
    return v >= 1000.0f ? juce::String (v / 1000.0f, v >= 10000.0f ? 1 : 2) + " kHz" : juce::String (juce::roundToInt (v)) + " Hz";
}
juce::String formatSemis (float v, int)
{
    return std::abs (v) < 0.05f ? juce::String ("0 st") : (v > 0 ? "+" : "") + juce::String (v, std::abs (v - std::round (v)) < 0.05f ? 0 : 1) + " st";
}

juce::NormalisableRange<float> skewed (float lo, float hi, float centre, float step = 0.0f)
{
    juce::NormalisableRange<float> r (lo, hi, step);
    r.setSkewForCentre (centre);
    return r;
}

std::unique_ptr<APF> floatParam (const char* id, const juce::String& name, juce::NormalisableRange<float> range, float def,
                                 std::function<juce::String (float, int)> fmt, const juce::String& label = {})
{
    return std::make_unique<APF> (juce::ParameterID { id, 1 }, name, range, def,
                                  APFA().withLabel (label).withStringFromValueFunction (std::move (fmt)));
}

std::unique_ptr<juce::AudioParameterBool> boolParam (const char* id, const juce::String& name, bool def)
{
    return std::make_unique<juce::AudioParameterBool> (juce::ParameterID { id, 1 }, name, def);
}

std::unique_ptr<juce::AudioParameterChoice> choiceParam (const char* id, const juce::String& name, const juce::StringArray& items,
                                                         int def, bool automatable = true)
{
    return std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { id, 1 }, name, items, def,
                                                         juce::AudioParameterChoiceAttributes().withAutomatable (automatable));
}

juce::StringArray keyNames()
{
    juce::StringArray s;
    for (int i = 0; i < 12; ++i) s.add (keyName (i));
    return s;
}

juce::StringArray scaleNames()
{
    juce::StringArray s;
    for (int i = 0; i < (int) Scale::numScales; ++i) s.add (scaleName ((Scale) i));
    return s;
}

juce::String ordinal (int degree)
{
    static const char* names[] = { "Unison", "2nd", "3rd", "4th", "5th", "6th", "7th", "Oct" };
    if (degree <= 7)
        return names[degree];
    return "Oct+" + juce::String (names[degree - 7]);
}
} // namespace

juce::String harmonyIntervalText (int interval, int harmMode)
{
    if (harmMode == 2)
        return "MIDI";
    if (interval == 0)
        return harmMode == 0 ? "Unison" : "0 st";
    const juce::String sign = interval > 0 ? "+" : "-";
    if (harmMode == 1)
        return sign + juce::String (std::abs (interval)) + " st";
    return sign + ordinal (std::abs (interval));
}

const char* moduleEnableID (ModuleId m) noexcept
{
    switch (m)
    {
        case ModuleId::gate:   return ParamID::gateOn;
        case ModuleId::level:  return ParamID::levelOn;
        case ModuleId::deess:  return ParamID::deessOn;
        case ModuleId::sculpt: return ParamID::sculptOn;
        case ModuleId::eq:     return ParamID::eqOn;
        case ModuleId::comp:   return ParamID::compOn;
        case ModuleId::sat:    return ParamID::satOn;
        case ModuleId::numModules: break;
    }
    return ParamID::eqOn;
}

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    auto group = [] (const char* id, const char* name) { return std::make_unique<juce::AudioProcessorParameterGroup> (id, name, "|"); };

    // ---- Global / input --------------------------------------------------------------------------
    auto input = group ("input", "Input");
    input->addChild (choiceParam (ParamID::mode, "Processing Mode", { "Mixing", "Tracking" }, 0, false));
    input->addChild (floatParam (ParamID::inGain, "Input Gain", { -24.0f, 24.0f, 0.1f }, 0.0f, formatDb, "dB"));
    input->addChild (boolParam (ParamID::phase, "Phase Invert", false));
    input->addChild (floatParam (ParamID::lowCut, "Low Cut", skewed (20.0f, 400.0f, 90.0f, 1.0f), 80.0f,
                                 [] (float v, int) { return v <= 20.5f ? juce::String ("Off") : formatHz (v); }, "Hz"));
    input->addChild (choiceParam (ParamID::lowCutSlope, "Low Cut Slope", { "12 dB/oct", "24 dB/oct" }, 0));
    layout.add (std::move (input));

    // ---- Tune --------------------------------------------------------------------------------------
    auto tune = group ("tune", "Tune");
    tune->addChild (boolParam (ParamID::tuneOn, "Tune", false));
    tune->addChild (choiceParam (ParamID::tuneKey, "Key", keyNames(), 0));
    tune->addChild (choiceParam (ParamID::tuneScale, "Scale", scaleNames(), 0));
    tune->addChild (choiceParam (ParamID::tuneTarget, "Tune Target", { "Scale", "MIDI Notes" }, 0));
    tune->addChild (floatParam (ParamID::tuneSpeed, "Retune Speed", skewed (0.0f, 400.0f, 40.0f, 0.1f), 25.0f,
                                [] (float v, int) { return v < 0.5f ? juce::String ("Hard") : formatMs (v, 0); }, "ms"));
    tune->addChild (floatParam (ParamID::tuneTolerance, "Tolerance", { 0.0f, 100.0f, 1.0f }, 10.0f,
                                [] (float v, int) { return juce::String (juce::roundToInt (v)) + " ct"; }, "ct"));
    tune->addChild (floatParam (ParamID::tuneAmount, "Tune Amount", { 0.0f, 100.0f, 1.0f }, 100.0f, formatPct, "%"));
    tune->addChild (floatParam (ParamID::transpose, "Transpose", { -12.0f, 12.0f, 1.0f }, 0.0f, formatSemis, "st"));
    tune->addChild (floatParam (ParamID::formant, "Formant", { -12.0f, 12.0f, 0.1f }, 0.0f, formatSemis, "st"));
    layout.add (std::move (tune));

    // ---- Harmonies ---------------------------------------------------------------------------------
    auto harm = group ("harm", "Harmonies");
    harm->addChild (boolParam (ParamID::harmOn, "Harmonies", false));
    harm->addChild (choiceParam (ParamID::harmMode, "Harmony Mode", { "Scale", "Semitones", "MIDI Chord" }, 0));
    harm->addChild (floatParam (ParamID::harmHumanize, "Harmony Humanize", { 0.0f, 100.0f, 1.0f }, 35.0f, formatPct, "%"));
    harm->addChild (floatParam (ParamID::harmLevel, "Harmony Level", { -30.0f, 6.0f, 0.1f }, -3.0f, formatDb, "dB"));
    const char* hvIds[4][5] = { { ParamID::hv1On, ParamID::hv1Interval, ParamID::hv1Level, ParamID::hv1Pan, ParamID::hv1Formant },
                                { ParamID::hv2On, ParamID::hv2Interval, ParamID::hv2Level, ParamID::hv2Pan, ParamID::hv2Formant },
                                { ParamID::hv3On, ParamID::hv3Interval, ParamID::hv3Level, ParamID::hv3Pan, ParamID::hv3Formant },
                                { ParamID::hv4On, ParamID::hv4Interval, ParamID::hv4Level, ParamID::hv4Pan, ParamID::hv4Formant } };
    const int defInterval[4] = { 2, 4, -3, 7 };
    const float defPan[4] = { -60.0f, 60.0f, -30.0f, 30.0f };
    for (int v = 0; v < 4; ++v)
    {
        const juce::String n = "Voice " + juce::String (v + 1);
        harm->addChild (boolParam (hvIds[v][0], n, v < 2));
        harm->addChild (std::make_unique<juce::AudioParameterInt> (juce::ParameterID { hvIds[v][1], 1 }, n + " Interval", -14, 14, defInterval[v],
            juce::AudioParameterIntAttributes().withStringFromValueFunction ([] (int i, int) { return (i > 0 ? "+" : "") + juce::String (i); })));
        harm->addChild (floatParam (hvIds[v][2], n + " Level", { -30.0f, 6.0f, 0.1f }, -4.0f, formatDb, "dB"));
        harm->addChild (floatParam (hvIds[v][3], n + " Pan", { -100.0f, 100.0f, 1.0f }, defPan[v],
                                    [] (float p, int) { return std::abs (p) < 0.5f ? juce::String ("C") : juce::String (juce::roundToInt (std::abs (p))) + (p < 0 ? " L" : " R"); }));
        harm->addChild (floatParam (hvIds[v][4], n + " Formant", { -12.0f, 12.0f, 0.1f }, 0.0f, formatSemis, "st"));
    }
    layout.add (std::move (harm));

    // ---- Gate / breath -------------------------------------------------------------------------------
    auto gate = group ("gate", "Gate");
    gate->addChild (boolParam (ParamID::gateOn, "Gate", false));
    gate->addChild (floatParam (ParamID::gateThreshold, "Gate Threshold", { -90.0f, 0.0f, 0.1f }, -55.0f, formatDb, "dB"));
    gate->addChild (floatParam (ParamID::gateRange, "Gate Range", { -80.0f, 0.0f, 0.1f }, -30.0f,
                                [] (float v, int) { return v <= -79.9f ? juce::String ("-inf dB") : formatDb (v, 0); }, "dB"));
    gate->addChild (floatParam (ParamID::gateAttack, "Gate Attack", skewed (0.05f, 50.0f, 2.0f, 0.01f), 1.0f, formatMs, "ms"));
    gate->addChild (floatParam (ParamID::gateHold, "Gate Hold", skewed (0.0f, 500.0f, 60.0f, 1.0f), 60.0f, formatMs, "ms"));
    gate->addChild (floatParam (ParamID::gateRelease, "Gate Release", skewed (5.0f, 2000.0f, 150.0f, 1.0f), 180.0f, formatMs, "ms"));
    gate->addChild (floatParam (ParamID::breath, "Breath Reduction", { -30.0f, 0.0f, 0.1f }, 0.0f,
                                [] (float v, int) { return v > -0.1f ? juce::String ("Off") : formatDb (v, 0); }, "dB"));
    gate->addChild (floatParam (ParamID::breathSens, "Breath Sensitivity", { 0.0f, 100.0f, 1.0f }, 50.0f, formatPct, "%"));
    layout.add (std::move (gate));

    // ---- Leveler -----------------------------------------------------------------------------------------
    auto level = group ("level", "Auto Level");
    level->addChild (boolParam (ParamID::levelOn, "Auto Level", false));
    level->addChild (floatParam (ParamID::levelTarget, "Level Target", { -40.0f, -6.0f, 0.1f }, -18.0f, formatDb, "dB"));
    level->addChild (floatParam (ParamID::levelRange, "Level Range", { 0.0f, 18.0f, 0.1f }, 8.0f,
                                 [] (float v, int) { return juce::String::charToString (0xb1) + juce::String (v, 1) + " dB"; }, "dB"));
    level->addChild (floatParam (ParamID::levelSpeed, "Level Speed", skewed (20.0f, 2000.0f, 250.0f, 1.0f), 300.0f, formatMs, "ms"));
    layout.add (std::move (level));

    // ---- De-esser -------------------------------------------------------------------------------------------
    auto deess = group ("deess", "De-Esser");
    deess->addChild (boolParam (ParamID::deessOn, "De-Esser", true));
    deess->addChild (floatParam (ParamID::deessFreq, "De-Ess Frequency", skewed (2000.0f, 12000.0f, 6000.0f, 10.0f), 6500.0f, formatHz, "Hz"));
    deess->addChild (floatParam (ParamID::deessThreshold, "De-Ess Threshold", { -60.0f, 0.0f, 0.1f }, -30.0f, formatDb, "dB"));
    deess->addChild (floatParam (ParamID::deessRange, "De-Ess Range", { 0.0f, 24.0f, 0.1f }, 8.0f, formatDb, "dB"));
    deess->addChild (choiceParam (ParamID::deessMode, "De-Ess Mode", { "Split Band", "Wide Band" }, 0));
    deess->addChild (boolParam (ParamID::deessListen, "De-Ess Listen", false));
    layout.add (std::move (deess));

    // ---- Sculpt -------------------------------------------------------------------------------------------------
    auto sculpt = group ("sculpt", "Sculpt");
    sculpt->addChild (boolParam (ParamID::sculptOn, "Sculpt", false));
    juce::StringArray styles;
    for (int i = 0; i < kSculptStyles; ++i) styles.add (sculptStyleName (i));
    sculpt->addChild (choiceParam (ParamID::sculptStyle, "Sculpt Style", styles, 0));
    sculpt->addChild (floatParam (ParamID::sculptAmount, "Sculpt Amount", { 0.0f, 100.0f, 1.0f }, 50.0f, formatPct, "%"));
    sculpt->addChild (floatParam (ParamID::sculptSpeed, "Sculpt Speed", skewed (50.0f, 3000.0f, 400.0f, 1.0f), 500.0f, formatMs, "ms"));
    sculpt->addChild (floatParam (ParamID::sculptTilt, "Sculpt Tilt", { -6.0f, 6.0f, 0.1f }, 0.0f, formatDb, "dB"));
    layout.add (std::move (sculpt));

    // ---- EQ ---------------------------------------------------------------------------------------------------------
    auto eq = group ("eq", "EQ");
    eq->addChild (boolParam (ParamID::eqOn, "EQ", true));
    const char* eqIds[5][3] = { { ParamID::eq1Freq, ParamID::eq1Gain, ParamID::eq1Q }, { ParamID::eq2Freq, ParamID::eq2Gain, ParamID::eq2Q },
                                { ParamID::eq3Freq, ParamID::eq3Gain, ParamID::eq3Q }, { ParamID::eq4Freq, ParamID::eq4Gain, ParamID::eq4Q },
                                { ParamID::eq5Freq, ParamID::eq5Gain, ParamID::eq5Q } };
    const char* eqNames[5] = { "Low", "Low Mid", "Mid", "Presence", "Air" };
    const float eqLo[5] = { 30.0f, 80.0f, 300.0f, 1000.0f, 2000.0f };
    const float eqHi[5] = { 600.0f, 1500.0f, 6000.0f, 12000.0f, 20000.0f };
    const float eqDef[5] = { 120.0f, 300.0f, 1500.0f, 4000.0f, 10000.0f };
    const float eqCentre[5] = { 150.0f, 350.0f, 1500.0f, 4000.0f, 8000.0f };
    for (int b = 0; b < 5; ++b)
    {
        const juce::String n = juce::String ("EQ ") + eqNames[b];
        eq->addChild (floatParam (eqIds[b][0], n + " Freq", skewed (eqLo[b], eqHi[b], eqCentre[b], 1.0f), eqDef[b], formatHz, "Hz"));
        eq->addChild (floatParam (eqIds[b][1], n + " Gain", { -18.0f, 18.0f, 0.1f }, 0.0f, formatDb, "dB"));
        const bool shelf = b == 0 || b == 4;
        eq->addChild (floatParam (eqIds[b][2], n + (shelf ? " Slope" : " Q"), shelf ? juce::NormalisableRange<float> (0.3f, 1.0f, 0.01f) : skewed (0.2f, 10.0f, 1.0f, 0.01f),
                                  shelf ? 0.7f : 1.0f, [] (float v, int) { return juce::String (v, 2); }));
    }
    layout.add (std::move (eq));

    // ---- Compressor -----------------------------------------------------------------------------------------------------
    auto comp = group ("comp", "Compressor");
    comp->addChild (boolParam (ParamID::compOn, "Compressor", true));
    comp->addChild (choiceParam (ParamID::compMode, "Comp Character", { "Clean", "Opto", "FET", "Tube", "Multiband" }, 1));
    comp->addChild (floatParam (ParamID::compThreshold, "Comp Threshold", { -60.0f, 0.0f, 0.1f }, -20.0f, formatDb, "dB"));
    comp->addChild (floatParam (ParamID::compRatio, "Comp Ratio", skewed (1.0f, 20.0f, 4.0f, 0.1f), 3.0f,
                                [] (float v, int) { return juce::String (v, 1) + ":1"; }));
    comp->addChild (floatParam (ParamID::compAttack, "Comp Attack", skewed (0.1f, 100.0f, 10.0f, 0.01f), 8.0f, formatMs, "ms"));
    comp->addChild (floatParam (ParamID::compRelease, "Comp Release", skewed (10.0f, 1500.0f, 150.0f, 1.0f), 140.0f, formatMs, "ms"));
    comp->addChild (floatParam (ParamID::compMakeup, "Comp Makeup", { 0.0f, 24.0f, 0.1f }, 3.0f, formatDb, "dB"));
    comp->addChild (floatParam (ParamID::compMix, "Comp Mix", { 0.0f, 100.0f, 1.0f }, 100.0f, formatPct, "%"));
    layout.add (std::move (comp));

    // ---- Saturation -----------------------------------------------------------------------------------------------------
    auto sat = group ("sat", "Saturation");
    sat->addChild (boolParam (ParamID::satOn, "Saturation", false));
    sat->addChild (choiceParam (ParamID::satMode, "Saturation Type", { "Tape", "Tube", "Console", "Exciter", "Radio" }, 0));
    sat->addChild (floatParam (ParamID::satDrive, "Saturation Drive", { 0.0f, 24.0f, 0.1f }, 6.0f, formatDb, "dB"));
    sat->addChild (floatParam (ParamID::satTone, "Saturation Tone", { -100.0f, 100.0f, 1.0f }, 0.0f,
                               [] (float v, int) { return std::abs (v) < 0.5f ? juce::String ("Neutral") : (v > 0 ? "Bright " : "Dark ") + juce::String (juce::roundToInt (std::abs (v))); }));
    sat->addChild (floatParam (ParamID::satMix, "Saturation Mix", { 0.0f, 100.0f, 1.0f }, 100.0f, formatPct, "%"));
    layout.add (std::move (sat));

    // ---- Doubler ---------------------------------------------------------------------------------------------------------------
    auto dbl = group ("doubler", "Doubler");
    dbl->addChild (boolParam (ParamID::dblOn, "Doubler", false));
    dbl->addChild (floatParam (ParamID::dblAmount, "Doubler Amount", { 0.0f, 100.0f, 1.0f }, 50.0f, formatPct, "%"));
    dbl->addChild (floatParam (ParamID::dblDetune, "Doubler Detune", { 0.0f, 40.0f, 0.1f }, 9.0f,
                               [] (float v, int) { return juce::String (v, 1) + " ct"; }, "ct"));
    dbl->addChild (floatParam (ParamID::dblDelay, "Doubler Delay", { 0.0f, 60.0f, 0.1f }, 14.0f, formatMs, "ms"));
    dbl->addChild (floatParam (ParamID::dblWidth, "Doubler Width", { 0.0f, 100.0f, 1.0f }, 85.0f, formatPct, "%"));
    dbl->addChild (floatParam (ParamID::dblDrift, "Doubler Humanize", { 0.0f, 100.0f, 1.0f }, 50.0f, formatPct, "%"));
    layout.add (std::move (dbl));

    // ---- Modulation ------------------------------------------------------------------------------------------------------------
    auto mod = group ("mod", "Modulation");
    mod->addChild (boolParam (ParamID::modOn, "Modulation", false));
    mod->addChild (choiceParam (ParamID::modMode, "Modulation Type", { "Chorus", "Flanger", "Phaser" }, 0));
    mod->addChild (floatParam (ParamID::modRate, "Mod Rate", skewed (0.02f, 8.0f, 0.8f, 0.01f), 0.6f,
                               [] (float v, int) { return juce::String (v, 2) + " Hz"; }, "Hz"));
    mod->addChild (floatParam (ParamID::modDepth, "Mod Depth", { 0.0f, 100.0f, 1.0f }, 50.0f, formatPct, "%"));
    mod->addChild (floatParam (ParamID::modFeedback, "Mod Feedback", { 0.0f, 90.0f, 1.0f }, 20.0f, formatPct, "%"));
    mod->addChild (floatParam (ParamID::modMix, "Mod Mix", { 0.0f, 100.0f, 1.0f }, 35.0f, formatPct, "%"));
    layout.add (std::move (mod));

    // ---- Delay -------------------------------------------------------------------------------------------------------------------
    auto dly = group ("delay", "Delay");
    dly->addChild (boolParam (ParamID::dlyOn, "Delay", false));
    dly->addChild (choiceParam (ParamID::dlyMode, "Delay Type", { "Digital", "Tape", "Ping-Pong", "Slapback" }, 0));
    dly->addChild (floatParam (ParamID::dlyTime, "Delay Time", skewed (10.0f, 2000.0f, 300.0f, 1.0f), 320.0f, formatMs, "ms"));
    juce::StringArray syncs;
    for (int i = 0; i < numDelaySyncOptions(); ++i) syncs.add (delaySyncName (i));
    dly->addChild (choiceParam (ParamID::dlySync, "Delay Sync", syncs, 7));
    dly->addChild (floatParam (ParamID::dlyFeedback, "Delay Feedback", { 0.0f, 95.0f, 1.0f }, 30.0f, formatPct, "%"));
    dly->addChild (floatParam (ParamID::dlyHp, "Delay Low Cut", skewed (20.0f, 2000.0f, 250.0f, 1.0f), 250.0f, formatHz, "Hz"));
    dly->addChild (floatParam (ParamID::dlyLp, "Delay High Cut", skewed (1000.0f, 20000.0f, 6000.0f, 1.0f), 6500.0f, formatHz, "Hz"));
    dly->addChild (floatParam (ParamID::dlyDuck, "Delay Ducking", { 0.0f, 100.0f, 1.0f }, 50.0f, formatPct, "%"));
    dly->addChild (floatParam (ParamID::dlyWidth, "Delay Width", { 0.0f, 100.0f, 1.0f }, 100.0f, formatPct, "%"));
    dly->addChild (floatParam (ParamID::dlyMix, "Delay Level", { 0.0f, 100.0f, 1.0f }, 22.0f, formatPct, "%"));
    layout.add (std::move (dly));

    // ---- Reverb ------------------------------------------------------------------------------------------------------------------
    auto rev = group ("reverb", "Reverb");
    rev->addChild (boolParam (ParamID::revOn, "Reverb", false));
    rev->addChild (choiceParam (ParamID::revType, "Reverb Type", { "Plate", "Room", "Hall", "Chamber" }, 0));
    rev->addChild (floatParam (ParamID::revDecay, "Reverb Decay", skewed (0.2f, 10.0f, 1.8f, 0.01f), 1.8f,
                               [] (float v, int) { return juce::String (v, 2) + " s"; }, "s"));
    rev->addChild (floatParam (ParamID::revPredelay, "Reverb Pre-Delay", { 0.0f, 200.0f, 0.1f }, 25.0f, formatMs, "ms"));
    rev->addChild (floatParam (ParamID::revSize, "Reverb Size", { 0.0f, 100.0f, 1.0f }, 50.0f, formatPct, "%"));
    rev->addChild (floatParam (ParamID::revDamp, "Reverb Damping", { 0.0f, 100.0f, 1.0f }, 45.0f, formatPct, "%"));
    rev->addChild (floatParam (ParamID::revLowCut, "Reverb Low Cut", skewed (20.0f, 1000.0f, 250.0f, 1.0f), 300.0f, formatHz, "Hz"));
    rev->addChild (floatParam (ParamID::revDuck, "Reverb Ducking", { 0.0f, 100.0f, 1.0f }, 25.0f, formatPct, "%"));
    rev->addChild (floatParam (ParamID::revWidth, "Reverb Width", { 0.0f, 100.0f, 1.0f }, 100.0f, formatPct, "%"));
    rev->addChild (floatParam (ParamID::revMix, "Reverb Level", { 0.0f, 100.0f, 1.0f }, 18.0f, formatPct, "%"));
    layout.add (std::move (rev));

    // ---- Output ---------------------------------------------------------------------------------------------------------------------
    auto out = group ("output", "Output");
    out->addChild (floatParam (ParamID::outGain, "Output Gain", { -24.0f, 12.0f, 0.1f }, 0.0f, formatDb, "dB"));
    out->addChild (floatParam (ParamID::outWidth, "Stereo Width", { 0.0f, 200.0f, 1.0f }, 100.0f, formatPct, "%"));
    out->addChild (floatParam (ParamID::outMix, "Dry/Wet", { 0.0f, 100.0f, 1.0f }, 100.0f, formatPct, "%"));
    out->addChild (boolParam (ParamID::limOn, "Limiter", true));
    out->addChild (floatParam (ParamID::limCeiling, "Limiter Ceiling", { -12.0f, 0.0f, 0.1f }, -1.0f, formatDb, "dB"));
    layout.add (std::move (out));

    return layout;
}

//==============================================================================
ParameterCache::ParameterCache (juce::AudioProcessorValueTreeState& s)
{
    const char* ids[] = {
#define LAVC_ID_STRING(name, id) id,
        LAVC_PARAMETERS (LAVC_ID_STRING)
#undef LAVC_ID_STRING
    };
    for (size_t i = 0; i < values.size(); ++i)
    {
        values[i] = s.getRawParameterValue (ids[i]);
        jassert (values[i] != nullptr);
    }
}

void ParameterCache::read (EngineParams& p) const noexcept
{
    auto f = [this] (P id) { return get (id); };
    auto b = [this] (P id) { return get (id) > 0.5f; };
    auto i = [this] (P id) { return (int) std::lround (get (id)); };

    p.tracking = i (P::mode) == 1;
    p.inputGainDb = f (P::inGain);
    p.phaseInvert = b (P::phase);
    p.lowCutHz = f (P::lowCut);
    p.lowCut24 = i (P::lowCutSlope) == 1;

    auto& t = p.pitch;
    t.tuneEnabled = b (P::tuneOn);
    t.key = i (P::tuneKey);
    t.scale = (Scale) i (P::tuneScale);
    t.targetMode = i (P::tuneTarget);
    t.speedMs = f (P::tuneSpeed);
    t.toleranceCents = f (P::tuneTolerance);
    t.amount = f (P::tuneAmount) * 0.01f;
    t.transpose = f (P::transpose);
    t.formant = f (P::formant);
    t.harmEnabled = b (P::harmOn);
    t.harmMode = i (P::harmMode);
    t.harmHumanize = f (P::harmHumanize) * 0.01f;
    t.harmLevelDb = f (P::harmLevel);
    const P hv[4][5] = { { P::hv1On, P::hv1Interval, P::hv1Level, P::hv1Pan, P::hv1Formant },
                         { P::hv2On, P::hv2Interval, P::hv2Level, P::hv2Pan, P::hv2Formant },
                         { P::hv3On, P::hv3Interval, P::hv3Level, P::hv3Pan, P::hv3Formant },
                         { P::hv4On, P::hv4Interval, P::hv4Level, P::hv4Pan, P::hv4Formant } };
    for (size_t v = 0; v < 4; ++v)
    {
        auto& vp = t.voices[v];
        vp.enabled = b (hv[v][0]);
        vp.interval = i (hv[v][1]);
        vp.levelDb = f (hv[v][2]);
        vp.pan = f (hv[v][3]) * 0.01f;
        vp.formant = f (hv[v][4]);
    }

    p.gate = { b (P::gateOn), f (P::gateThreshold), f (P::gateRange), f (P::gateAttack), f (P::gateHold), f (P::gateRelease),
               f (P::breath), f (P::breathSens) };
    p.level = { b (P::levelOn), f (P::levelTarget), f (P::levelRange), f (P::levelSpeed) };
    p.deess = { b (P::deessOn), f (P::deessFreq), f (P::deessThreshold), f (P::deessRange), i (P::deessMode), b (P::deessListen) };
    p.sculpt = { b (P::sculptOn), i (P::sculptStyle), f (P::sculptAmount) * 0.01f, f (P::sculptSpeed), f (P::sculptTilt) };

    p.eq.enabled = b (P::eqOn);
    const P eqp[5][3] = { { P::eq1Freq, P::eq1Gain, P::eq1Q }, { P::eq2Freq, P::eq2Gain, P::eq2Q }, { P::eq3Freq, P::eq3Gain, P::eq3Q },
                          { P::eq4Freq, P::eq4Gain, P::eq4Q }, { P::eq5Freq, P::eq5Gain, P::eq5Q } };
    for (size_t k = 0; k < 5; ++k)
    {
        p.eq.freq[k] = f (eqp[k][0]);
        p.eq.gain[k] = f (eqp[k][1]);
        p.eq.q[k] = f (eqp[k][2]);
    }

    p.comp = { b (P::compOn), (CompMode) i (P::compMode), f (P::compThreshold), f (P::compRatio), f (P::compAttack),
               f (P::compRelease), f (P::compMakeup), f (P::compMix) * 0.01f };
    p.sat = { b (P::satOn), (SatMode) i (P::satMode), f (P::satDrive), f (P::satTone) * 0.01f, f (P::satMix) * 0.01f };

    p.doubler = { b (P::dblOn), f (P::dblAmount) * 0.01f, f (P::dblDetune), f (P::dblDelay), f (P::dblWidth) * 0.01f, f (P::dblDrift) * 0.01f };
    p.mod = { b (P::modOn), (ModMode) i (P::modMode), f (P::modRate), f (P::modDepth) * 0.01f, f (P::modFeedback) * 0.01f, f (P::modMix) * 0.01f };
    p.delay = { b (P::dlyOn), (DelayMode) i (P::dlyMode), f (P::dlyTime), i (P::dlySync), f (P::dlyFeedback) * 0.01f, f (P::dlyHp),
                f (P::dlyLp), f (P::dlyDuck) * 0.01f, f (P::dlyWidth) * 0.01f, f (P::dlyMix) * 0.01f };
    p.reverb = { b (P::revOn), (ReverbType) i (P::revType), f (P::revDecay), f (P::revPredelay), f (P::revSize) * 0.01f,
                 f (P::revDamp) * 0.01f, f (P::revLowCut), f (P::revDuck) * 0.01f, f (P::revWidth) * 0.01f, f (P::revMix) * 0.01f };

    p.outputGainDb = f (P::outGain);
    p.width = f (P::outWidth) * 0.01f;
    p.mix = f (P::outMix) * 0.01f;
    p.limiterEnabled = b (P::limOn);
    p.ceilingDb = f (P::limCeiling);
}

//==============================================================================
const std::vector<FactoryPreset>& getFactoryPresets()
{
    using namespace ParamID;
    static const std::vector<FactoryPreset> presets {
        { "Default", {} },
        { "Modern Pop Lead",
          { { tuneOn, 1 }, { tuneSpeed, 18 }, { sculptOn, 1 }, { sculptStyle, 1 }, { sculptAmount, 55 }, { levelOn, 1 },
            { compMode, 0 }, { compThreshold, -22 }, { compRatio, 4 }, { compAttack, 4 }, { compRelease, 90 }, { compMakeup, 5 },
            { eq1Gain, -1.5f }, { eq3Freq, 2800 }, { eq3Gain, 1.5f }, { eq5Gain, 3.0f }, { deessThreshold, -32 },
            { satOn, 1 }, { satMode, 3 }, { satDrive, 4 }, { satMix, 60 }, { dlyOn, 1 }, { dlySync, 7 }, { dlyMix, 14 },
            { revOn, 1 }, { revType, 0 }, { revDecay, 1.6f }, { revMix, 14 } } },
        { "Hip-Hop Upfront",
          { { tuneOn, 1 }, { tuneSpeed, 30 }, { sculptOn, 1 }, { sculptStyle, 3 }, { gateOn, 1 }, { gateThreshold, -50 },
            { compMode, 2 }, { compThreshold, -24 }, { compRatio, 6 }, { compAttack, 1 }, { compRelease, 70 }, { compMakeup, 6 },
            { eq2Freq, 350 }, { eq2Gain, -2.5f }, { eq4Freq, 4500 }, { eq4Gain, 2.5f }, { satOn, 1 }, { satMode, 2 }, { satDrive, 8 },
            { dblOn, 1 }, { dblAmount, 25 }, { dlyOn, 1 }, { dlySync, 6 }, { dlyMix, 10 }, { dlyDuck, 80 } } },
        { "Hard Tune (Trap)",
          { { tuneOn, 1 }, { tuneSpeed, 0 }, { tuneTolerance, 0 }, { tuneScale, 2 }, { compMode, 2 }, { compThreshold, -22 },
            { compRatio, 5 }, { compMakeup, 5 }, { eq5Gain, 4 }, { satOn, 1 }, { satMode, 3 }, { satDrive, 6 },
            { dlyOn, 1 }, { dlySync, 7 }, { dlyMix, 18 }, { revOn, 1 }, { revType, 2 }, { revDecay, 2.6f }, { revMix, 16 } } },
        { "R&B Silk",
          { { tuneOn, 1 }, { tuneSpeed, 45 }, { tuneTolerance, 20 }, { sculptOn, 1 }, { sculptStyle, 8 }, { compMode, 1 },
            { compThreshold, -24 }, { compRatio, 3 }, { compMakeup, 4 }, { eq5Gain, 3.5f }, { deessThreshold, -34 },
            { modOn, 1 }, { modMode, 0 }, { modMix, 18 }, { dlyOn, 1 }, { dlyMode, 2 }, { dlySync, 9 }, { dlyMix, 16 },
            { revOn, 1 }, { revType, 2 }, { revDecay, 2.4f }, { revMix, 20 } } },
        { "Rock Grit",
          { { sculptOn, 1 }, { sculptStyle, 4 }, { compMode, 2 }, { compThreshold, -26 }, { compRatio, 8 }, { compAttack, 2 },
            { compMakeup, 7 }, { satOn, 1 }, { satMode, 1 }, { satDrive, 12 }, { eq3Freq, 1200 }, { eq3Gain, 2 },
            { dlyOn, 1 }, { dlyMode, 3 }, { dlySync, 0 }, { dlyTime, 110 }, { dlyMix, 18 }, { revOn, 1 }, { revType, 1 }, { revMix, 12 } } },
        { "Indie Intimate",
          { { sculptOn, 1 }, { sculptStyle, 6 }, { compMode, 1 }, { compThreshold, -20 }, { compRatio, 2.5f }, { compMakeup, 3 },
            { breath, -8 }, { satOn, 1 }, { satMode, 0 }, { satDrive, 5 }, { revOn, 1 }, { revType, 3 }, { revDecay, 1.2f }, { revMix, 12 } } },
        { "Podcast / Voice-Over",
          { { lowCut, 90 }, { lowCutSlope, 1 }, { gateOn, 1 }, { gateThreshold, -52 }, { gateRange, -18 }, { breath, -12 },
            { levelOn, 1 }, { levelTarget, -20 }, { sculptOn, 1 }, { sculptStyle, 0 }, { compMode, 1 }, { compThreshold, -24 },
            { compRatio, 3 }, { compMakeup, 6 }, { deessThreshold, -34 }, { limCeiling, -1.5f } } },
        { "Stacked Harmonies",
          { { tuneOn, 1 }, { tuneSpeed, 20 }, { tuneScale, 1 }, { harmOn, 1 }, { harmMode, 0 }, { hv1On, 1 }, { hv2On, 1 },
            { hv3On, 1 }, { hv3Interval, -3 }, { hv4On, 0 }, { compThreshold, -22 }, { compMakeup, 4 }, { dblOn, 1 },
            { dblAmount, 35 }, { revOn, 1 }, { revType, 2 }, { revMix, 20 } } },
        { "Wide Double",
          { { dblOn, 1 }, { dblAmount, 65 }, { dblDetune, 12 }, { dblDelay, 16 }, { dblWidth, 100 }, { compThreshold, -20 },
            { compMakeup, 4 }, { outWidth, 130 }, { revOn, 1 }, { revType, 0 }, { revMix, 10 } } },
        { "Lo-Fi Radio",
          { { sculptOn, 1 }, { sculptStyle, 7 }, { satOn, 1 }, { satMode, 4 }, { satDrive, 10 }, { compMode, 2 }, { compThreshold, -26 },
            { compRatio, 8 }, { compMakeup, 8 }, { dlyOn, 1 }, { dlyMode, 1 }, { dlySync, 0 }, { dlyTime, 140 }, { dlyMix, 18 } } },
        { "Ballad Hall",
          { { tuneOn, 1 }, { tuneSpeed, 60 }, { tuneTolerance, 25 }, { compMode, 3 }, { compThreshold, -22 }, { compRatio, 3 },
            { compMakeup, 4 }, { eq5Gain, 3 }, { dlyOn, 1 }, { dlySync, 9 }, { dlyMix, 15 }, { dlyDuck, 70 },
            { revOn, 1 }, { revType, 2 }, { revDecay, 3.5f }, { revPredelay, 45 }, { revMix, 26 } } },
        { "Robot Formant",
          { { tuneOn, 1 }, { tuneSpeed, 0 }, { formant, -4 }, { harmOn, 1 }, { harmMode, 1 }, { hv1On, 1 }, { hv1Interval, -12 },
            { hv1Formant, -5 }, { hv1Pan, 0 }, { hv2On, 0 }, { modOn, 1 }, { modMode, 1 }, { modMix, 40 } } },
        { "Live Tracking",
          { { mode, 1 }, { tuneOn, 1 }, { tuneSpeed, 30 }, { compMode, 0 }, { compThreshold, -20 }, { compMakeup, 3 },
            { revOn, 1 }, { revType, 0 }, { revMix, 12 } } },
    };
    return presets;
}
} // namespace lavc
