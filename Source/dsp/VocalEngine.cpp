#include "VocalEngine.h"

#include <cstring>

namespace lavc
{
const char* moduleShortName (ModuleId m) noexcept
{
    switch (m)
    {
        case ModuleId::gate:   return "GATE";
        case ModuleId::level:  return "LEVEL";
        case ModuleId::deess:  return "DE-ESS";
        case ModuleId::sculpt: return "SCULPT";
        case ModuleId::eq:     return "EQ";
        case ModuleId::comp:   return "COMP";
        case ModuleId::sat:    return "SAT";
        case ModuleId::numModules: break;
    }
    return "";
}

ModuleOrder defaultModuleOrder() noexcept
{
    return { (int) ModuleId::gate, (int) ModuleId::level, (int) ModuleId::sculpt, (int) ModuleId::eq,
             (int) ModuleId::comp, (int) ModuleId::deess, (int) ModuleId::sat };
}

ModuleOrder orderFromString (const char* text) noexcept
{
    ModuleOrder order {};
    if (text == nullptr || std::strlen (text) != (size_t) kNumOrderable)
        return defaultModuleOrder();
    bool seen[kNumOrderable] {};
    for (int i = 0; i < kNumOrderable; ++i)
    {
        const int v = text[i] - '0';
        if (v < 0 || v >= kNumOrderable || seen[v])
            return defaultModuleOrder();
        seen[v] = true;
        order[(size_t) i] = v;
    }
    return order;
}

void orderToString (const ModuleOrder& order, char* out) noexcept
{
    for (int i = 0; i < kNumOrderable; ++i)
        out[i] = (char) ('0' + order[(size_t) i]);
    out[kNumOrderable] = 0;
}

float AssistStats::percentile (const std::array<int, kBins>& h, float pct, float fallback)
{
    int64_t total = 0;
    for (int v : h) total += v;
    if (total == 0)
        return fallback;
    const double want = total * pct / 100.0;
    int64_t acc = 0;
    for (int i = 0; i < kBins; ++i)
    {
        acc += h[(size_t) i];
        if ((double) acc >= want)
            return (float) (i - kBins) + 0.5f;
    }
    return 0.0f;
}

//==============================================================================
void VocalEngine::prepare (double sampleRate, int maxBlockSize, bool trackingMode)
{
    sr = sampleRate;
    maxBlock = std::max (16, maxBlockSize);
    tracking = trackingMode;

    pitch.prepare (sr, maxBlock, tracking);
    gate.prepare (sr);
    level.prepare (sr);
    deess.prepare (sr);
    sculpt.prepare (sr);
    eq.prepare (sr);
    comp.prepare (sr);
    sat.prepare (sr);
    doubler.prepare (sr);
    modulation.prepare (sr);
    delay.prepare (sr);
    reverb.prepare (sr);
    limiter.prepare (sr, tracking ? 0 : (int) std::lround (sr * 0.0015));

    for (auto* v : { &monoIn, &leadBuf, &harmL, &harmR, &workL, &workR, &dryL, &dryR, &analysisBuf })
        v->assign ((size_t) maxBlock, 0.0f);
    voiced.assign ((size_t) maxBlock, 0);

    const int preLimiter = pitch.getLatency() + SaturationModule::getLatency();
    for (auto& d : dryDelay)
    {
        d.prepare (preLimiter + 8);
        d.setDelay (preLimiter);
    }
    totalLatency = preLimiter + limiter.getLatency();
    sideDelay.prepare (pitch.getLatency() + 8);
    sideDelay.setDelay (pitch.getLatency());

    inGain.setTime (sr, 20.0f);
    outGain.setTime (sr, 20.0f);
    widthSm.setTime (sr, 30.0f);
    mixSm.setTime (sr, 30.0f);
    assistHf.setHighPass (sr, 5000.0);
    lastLowCut = -1.0f;
    setParams (params);
    reset();
}

void VocalEngine::reset()
{
    for (auto* b : { &lowCut1L, &lowCut1R, &lowCut2L, &lowCut2R })
        b->reset();
    pitch.reset();
    gate.reset();
    level.reset();
    deess.reset();
    sculpt.reset();
    eq.reset();
    comp.reset();
    sat.reset();
    doubler.reset();
    modulation.reset();
    delay.reset();
    reverb.reset();
    limiter.reset();
    for (auto& d : dryDelay) d.reset();
    sideDelay.reset();
    inGain.snap (dbToGain (params.inputGainDb));
    outGain.snap (dbToGain (params.outputGainDb));
    widthSm.snap (params.width);
    mixSm.snap (params.mix);
    meters = {};
}

void VocalEngine::setParams (const EngineParams& p) noexcept
{
    params = p;
    if (p.lowCutHz != lastLowCut || p.lowCut24 != lastLowCut24)
    {
        lastLowCut = p.lowCutHz;
        lastLowCut24 = p.lowCut24;
        const double f = std::max (20.0f, p.lowCutHz);
        if (p.lowCut24)
        {
            lowCut1L.setHighPass (sr, f, 0.54119610);
            lowCut1R.setHighPass (sr, f, 0.54119610);
            lowCut2L.setHighPass (sr, f, 1.30656296);
            lowCut2R.setHighPass (sr, f, 1.30656296);
        }
        else
        {
            lowCut1L.setHighPass (sr, f, 0.70710678);
            lowCut1R.setHighPass (sr, f, 0.70710678);
            lowCut2L.setBypass();
            lowCut2R.setBypass();
        }
    }
    pitch.setParams (p.pitch);
    gate.setParams (p.gate);
    level.setParams (p.level);
    deess.setParams (p.deess);
    sculpt.setParams (p.sculpt);
    eq.setParams (p.eq);
    comp.setParams (p.comp);
    sat.setParams (p.sat);
    doubler.setParams (p.doubler);
    modulation.setParams (p.mod);
    delay.setParams (p.delay, p.bpm);
    reverb.setParams (p.reverb);
}

void VocalEngine::setAssistActive (bool on) noexcept
{
    if (on && ! assistOn)
    {
        assist.clear();
        assistAcc = assistHfAcc = 0.0;
        assistCount = assistVoicedCount = 0;
        pitch.resetKeyHistogram();
    }
    assistOn = on;
}

void VocalEngine::runModule (ModuleId id, float* l, float* r, int n) noexcept
{
    switch (id)
    {
        case ModuleId::gate:
            if (params.gate.enabled || params.gate.breathDb < -0.1f)
                gate.process (l, r, voiced.data(), n);
            break;
        case ModuleId::level:  if (params.level.enabled) level.process (l, r, n); break;
        case ModuleId::deess:  if (params.deess.enabled) deess.process (l, r, n); break;
        case ModuleId::sculpt: if (params.sculpt.enabled) sculpt.process (l, r, n); break;
        case ModuleId::eq:     if (params.eq.enabled) eq.process (l, r, n); break;
        case ModuleId::comp:   if (params.comp.enabled) comp.process (l, r, n); break;
        case ModuleId::sat:
        {
            // Always runs so the latency stays constant; mix = 0 when disabled is not needed because
            // the dry path inside is already delayed.
            auto sp = params.sat;
            if (! sp.enabled) { sp.mix = 0.0f; sat.setParams (sp); }
            sat.process (l, r, n);
            if (! sp.enabled) sat.setParams (params.sat);
            break;
        }
        case ModuleId::numModules: break;
    }
}

void VocalEngine::gatherAssist (const float* l, const float* r, int n) noexcept
{
    const int frame = std::max (64, (int) (sr * 0.02));
    const float gainComp = -params.inputGainDb;
    for (int i = 0; i < n; ++i)
    {
        const float x = 0.5f * (l[i] + r[i]);
        const float h = assistHf.process (x);
        assistAcc += (double) x * x;
        assistHfAcc += (double) h * h;
        assistVoicedCount += voiced[(size_t) i];
        if (++assistCount >= frame)
        {
            const float db = 10.0f * (float) std::log10 (assistAcc / frame + 1.0e-12) + gainComp;
            const float hfDb = 10.0f * (float) std::log10 (assistHfAcc / frame + 1.0e-12) + gainComp;
            auto bin = [] (float d) { return std::max (0, std::min (AssistStats::kBins - 1, (int) std::floor (d) + AssistStats::kBins)); };
            ++assist.allFrames[(size_t) bin (db)];
            const bool v = assistVoicedCount > frame / 2;
            if (v)
            {
                ++assist.voicedFrames[(size_t) bin (db)];
                const int note = (int) std::lround (pitch.getLastInputMidi());
                if (note > 0 && note < 128)
                    ++assist.pitchHist[(size_t) note];
            }
            else if (db > -60.0f)
                ++assist.sibilance[(size_t) bin (hfDb)];
            assistAcc = assistHfAcc = 0.0;
            assistCount = assistVoicedCount = 0;
        }
    }
    assist.samples += n;
}

void VocalEngine::process (float* const* io, int channels, int n, PitchListener* listener) noexcept
{
    n = std::min (n, maxBlock);
    float* inL = io[0];
    float* inR = channels > 1 ? io[1] : io[0];
    float* l = workL.data();
    float* r = workR.data();

    // ---- Input stage -----------------------------------------------------------------------------
    const float ig = dbToGain (params.inputGainDb) * (params.phaseInvert ? -1.0f : 1.0f);
    const bool lowCutOn = params.lowCutHz > 20.5f;
    float inPeak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        inPeak = std::max (inPeak, std::max (std::abs (inL[i]), std::abs (inR[i])));
        dryL[(size_t) i] = dryDelay[0].process (inL[i]);
        dryR[(size_t) i] = dryDelay[1].process (inR[i]);
        const float g = inGain.next (ig);
        float a = inL[i] * g, b = inR[i] * g;
        if (lowCutOn)
        {
            a = lowCut2L.process (lowCut1L.process (a));
            b = lowCut2R.process (lowCut1R.process (b));
        }
        l[i] = a;
        r[i] = b;
        monoIn[(size_t) i] = 0.5f * (a + b);
        harmL[(size_t) i] = harmR[(size_t) i] = 0.0f;
    }

    // ---- Pitch: tune + harmonies --------------------------------------------------------------
    pitch.process (monoIn.data(), leadBuf.data(), harmL.data(), harmR.data(), voiced.data(), n, listener);
    {
        // Stereo sources keep their image when tuning is off: the lead is then the delayed mid signal,
        // so add back the side signal delayed by the same amount.
        const bool useLead = params.pitch.tuneEnabled || channels < 2;
        for (int i = 0; i < n; ++i)
        {
            const float side = sideDelay.process (0.5f * (l[i] - r[i]));
            const float s = useLead ? 0.0f : side;
            l[i] = leadBuf[(size_t) i] + s + harmL[(size_t) i];
            r[i] = leadBuf[(size_t) i] - s + harmR[(size_t) i];
        }
    }

    if (assistOn)
        gatherAssist (leadBuf.data(), leadBuf.data(), n);

    // ---- Correction chain (user order) --------------------------------------------------------
    for (int idx : params.order)
        runModule ((ModuleId) idx, l, r, n);

    // ---- Effects ---------------------------------------------------------------------------------
    if (params.doubler.enabled) doubler.process (l, r, n);
    if (params.mod.enabled) modulation.process (l, r, n);
    if (params.delay.enabled) delay.process (l, r, n);
    if (params.reverb.enabled) reverb.process (l, r, n);

    // ---- Output -----------------------------------------------------------------------------------
    const float og = dbToGain (params.outputGainDb);
    for (int i = 0; i < n; ++i)
    {
        const float w = widthSm.next (clampf (params.width, 0.0f, 2.0f));
        const float mid = 0.5f * (l[i] + r[i]), side = 0.5f * (l[i] - r[i]) * w;
        const float g = outGain.next (og);
        const float mix = mixSm.next (clampf (params.mix, 0.0f, 1.0f));
        const float wl = (mid + side) * g, wr = (mid - side) * g;
        l[i] = dryL[(size_t) i] + mix * (wl - dryL[(size_t) i]);
        r[i] = dryR[(size_t) i] + mix * (wr - dryR[(size_t) i]);
    }
    limiter.process (l, r, n, params.ceilingDb, params.limiterEnabled);

    float outPeak = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float a = std::isfinite (l[i]) ? l[i] : 0.0f;
        const float b = std::isfinite (r[i]) ? r[i] : 0.0f;
        outPeak = std::max (outPeak, std::max (std::abs (a), std::abs (b)));
        analysisBuf[(size_t) i] = 0.5f * (a + b);
        if (channels > 1)
        {
            inL[i] = a;
            inR[i] = b;
        }
        else
            inL[i] = 0.5f * (a + b);
    }

    meters.inPeak = inPeak;
    meters.outPeak = outPeak;
    meters.gateDb = (params.gate.enabled || params.gate.breathDb < -0.1f) ? gate.getReductionDb() : 0.0f;
    meters.breath = gate.isBreathActive() && params.gate.breathDb < -0.1f;
    meters.levelDb = params.level.enabled ? level.getGainDb() : 0.0f;
    meters.deessDb = params.deess.enabled ? deess.getReductionDb() : 0.0f;
    meters.compDb = params.comp.enabled ? comp.getReductionDb() : 0.0f;
    meters.limitDb = limiter.getReductionDb();
    meters.sculpt = params.sculpt.enabled ? sculpt.getBandGains() : std::array<float, kSculptBands> {};
}
} // namespace lavc
