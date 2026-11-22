#include "Modules.h"

namespace lavc
{
//==============================================================================
// Gate + breath control
void GateModule::prepare (double sampleRate)
{
    sr = sampleRate;
    keyHp.setHighPass (sr, 80.0);
    hfHp.setHighPass (sr, 5000.0);
    peakEnv.setTimes (sr, 0.1f, 25.0f);
    rmsEnv.setTimes (sr, 8.0f, 8.0f);
    hfEnv.setTimes (sr, 8.0f, 8.0f);
    reset();
}

void GateModule::reset()
{
    keyHp.reset();
    hfHp.reset();
    peakEnv.reset();
    rmsEnv.reset();
    hfEnv.reset();
    gain = breathGain = 1.0f;
    gainDb = 0.0f;
    phraseDb = -60.0f;
    holdCounter = breathCounter = 0;
    open = false;
    breathActive = false;
    grDb = 0.0f;
}

void GateModule::process (float* l, float* r, const uint8_t* voiced, int n) noexcept
{
    const float rangeAbs = std::max (1.0f, -params.rangeDb);
    const float upStep = rangeAbs / (std::max (0.05f, params.attackMs) * 0.001f * (float) sr);
    const float downStep = rangeAbs / (std::max (1.0f, params.releaseMs) * 0.001f * (float) sr);
    const int holdSamples = (int) (params.holdMs * 0.001 * sr);
    const bool breathOn = params.breathDb < -0.1f && voiced != nullptr;
    const float breathTarget = dbToGain (params.breathDb);
    const float gapDb = 30.0f - 0.24f * clampf (params.breathSens, 0.0f, 100.0f);
    const float breathDown = msCoeff (sr, 15.0f), breathUp = msCoeff (sr, 25.0f);
    const float phraseDecay = 3.0f / (float) sr;
    const int breathMin = (int) (0.04 * sr);

    float minGain = 1.0f;
    for (int i = 0; i < n; ++i)
    {
        const float x = 0.5f * (l[i] + r[i]);
        const float k = keyHp.process (x);
        const float levelDb = gainToDb (peakEnv.process (std::abs (k)));

        if (params.enabled)
        {
            if (levelDb > params.thresholdDb)
            {
                open = true;
                holdCounter = holdSamples;
            }
            else if (levelDb < params.thresholdDb - 4.0f)
            {
                if (holdCounter > 0) --holdCounter;
                else open = false;
            }
        }
        // Linear ramps in dB: attack / release are the times to travel the full range
        const float targetDb = (! params.enabled || open) ? 0.0f : params.rangeDb;
        if (gainDb < targetDb) gainDb = std::min (targetDb, gainDb + upStep);
        else if (gainDb > targetDb) gainDb = std::max (targetDb, gainDb - downStep);
        gain = params.rangeDb <= -79.9f && gainDb <= -79.9f ? 0.0f : dbToGain (gainDb);

        float bt = 1.0f;
        if (breathOn)
        {
            const float ms = rmsEnv.process (k * k);
            const float h = hfHp.process (x);
            const float hf = hfEnv.process (h * h);
            const float ldb = 10.0f * std::log10 (ms + 1.0e-12f);
            const bool v = voiced[i] != 0;
            if (v)
                phraseDb = std::max (ldb, phraseDb - phraseDecay);
            else
                phraseDb -= phraseDecay;
            phraseDb = std::max (phraseDb, -70.0f);

            const bool candidate = ! v && ldb > -80.0f && ldb < phraseDb - gapDb && hf < 0.4f * ms;
            breathCounter = candidate ? breathCounter + 1 : 0;
            breathActive = breathCounter > breathMin;
            bt = breathActive ? breathTarget : 1.0f;
        }
        breathGain = bt + (bt < breathGain ? breathDown : breathUp) * (breathGain - bt);

        const float g = gain * breathGain;
        minGain = std::min (minGain, g);
        l[i] *= g;
        r[i] *= g;
    }
    grDb = -gainToDb (minGain);
}

//==============================================================================
// Leveler
void LevelerModule::prepare (double sampleRate)
{
    sr = sampleRate;
    weightHp.setHighPass (sr, 150.0);
    weightShelf.setHighShelf (sr, 2000.0, 4.0);
    msCoeffV = msCoeff (sr, 250.0f);
    reset();
}

void LevelerModule::reset()
{
    weightHp.reset();
    weightShelf.reset();
    meanSquare = 0.0f;
    gainDb = 0.0f;
}

void LevelerModule::process (float* l, float* r, int n) noexcept
{
    const float speed = 1.0f - std::exp (-1.0f / (std::max (10.0f, params.speedMs) * 0.001f * (float) sr));
    for (int i = 0; i < n; ++i)
    {
        const float w = weightShelf.process (weightHp.process (0.5f * (l[i] + r[i])));
        meanSquare = w * w + msCoeffV * (meanSquare - w * w);
        const float levelDb = 10.0f * std::log10 (meanSquare + 1.0e-12f);
        if (levelDb > params.targetDb - 30.0f)
        {
            const float desired = clampf (params.targetDb - levelDb, -params.rangeDb, params.rangeDb);
            gainDb += (desired - gainDb) * speed;
        }
        const float g = dbToGain (gainDb);
        l[i] *= g;
        r[i] *= g;
    }
}

//==============================================================================
// De-esser
void DeEsserModule::prepare (double sampleRate)
{
    sr = sampleRate;
    env.setTimes (sr, 0.3f, 60.0f);
    lastFreq = -1.0f;
    setParams (params);
    reset();
}

void DeEsserModule::reset()
{
    scHp1.reset(); scHp2.reset(); splitL.reset(); splitR.reset(); env.reset();
    gainSm = 1.0f;
    grDb = 0.0f;
}

void DeEsserModule::setParams (const DeEsserParams& p) noexcept
{
    params = p;
    if (std::abs (p.freqHz - lastFreq) > 0.5f)
    {
        lastFreq = p.freqHz;
        scHp1.setHighPass (sr, p.freqHz, 0.54119610);
        scHp2.setHighPass (sr, p.freqHz, 1.30656296);
        splitL.setFrequency (sr, p.freqHz * 0.8);
        splitR.setFrequency (sr, p.freqHz * 0.8);
    }
}

void DeEsserModule::process (float* l, float* r, int n) noexcept
{
    const float att = msCoeff (sr, 0.5f), rel = msCoeff (sr, 45.0f);
    float maxRed = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        const float s = scHp2.process (scHp1.process (0.5f * (l[i] + r[i])));
        const float levelDb = gainToDb (env.process (std::abs (s)));
        const float over = levelDb - params.thresholdDb;
        float red = 0.0f;
        if (over > 0.0f)
            red = over < 4.0f ? over * over / 8.0f : over - 2.0f; // soft knee
        red = std::min (red * 0.8f, params.rangeDb);
        const float target = dbToGain (-red);
        gainSm = target + (target < gainSm ? att : rel) * (gainSm - target);
        maxRed = std::max (maxRed, -gainToDb (gainSm));

        float ll, hl, lr, hr;
        splitL.process (l[i], ll, hl);
        splitR.process (r[i], lr, hr);
        if (params.listen)
        {
            l[i] = hl;
            r[i] = hr;
        }
        else if (params.mode == 0)
        {
            // Linkwitz-Riley split: low + high is flat, so only the sibilance band is turned down
            l[i] = ll + hl * gainSm;
            r[i] = lr + hr * gainSm;
        }
        else
        {
            l[i] *= gainSm;
            r[i] *= gainSm;
        }
    }
    grDb = maxRed;
}

//==============================================================================
// Sculpt
namespace
{
// Target long-term spectral balance per style (dB per band, relative)
const float sculptTargets[kSculptStyles][kSculptBands] = {
    { -3.0f, 0.0f, 0.0f, -4.0f, -8.0f, -11.0f, -15.0f, -20.0f },   // Natural
    { -6.0f, -2.0f, -1.0f, -4.0f, -6.0f, -7.0f, -10.0f, -13.0f },  // Modern Pop
    { -1.0f, 1.0f, 1.0f, -4.0f, -9.0f, -13.0f, -18.0f, -24.0f },   // Warm
    { -4.0f, -1.0f, -2.0f, -4.0f, -5.0f, -8.0f, -12.0f, -16.0f },  // Upfront (rap)
    { -5.0f, -1.0f, 0.0f, -2.0f, -5.0f, -9.0f, -13.0f, -18.0f },   // Rock
    { -5.0f, -2.0f, -1.0f, -5.0f, -8.0f, -9.0f, -10.0f, -11.0f },  // Airy
    { -2.0f, 1.0f, 0.0f, -5.0f, -9.0f, -12.0f, -14.0f, -17.0f },   // Intimate
    { -12.0f, -4.0f, 0.0f, 0.0f, -3.0f, -8.0f, -16.0f, -28.0f },   // Radio
    { -3.0f, -1.0f, -1.0f, -4.0f, -7.0f, -10.0f, -13.0f, -17.0f }, // Smooth
};
} // namespace

const char* sculptStyleName (int style) noexcept
{
    static const char* names[kSculptStyles] = { "Natural", "Modern Pop", "Warm", "Upfront", "Rock", "Airy", "Intimate", "Radio", "Smooth" };
    return names[std::max (0, std::min (kSculptStyles - 1, style))];
}

void SculptModule::prepare (double sampleRate)
{
    sr = sampleRate;
    for (int b = 0; b < kSculptBands; ++b)
        analysis[(size_t) b].setBandPass (sr, bandFreqs[(size_t) b], 1.4);
    powerCoeff = msCoeff (sr, 200.0f);
    reset();
}

void SculptModule::reset()
{
    for (auto& a : analysis) a.reset();
    for (auto& f : eqL) f.reset();
    for (auto& f : eqR) f.reset();
    power.fill (0.0f);
    applied.fill (0.0f);
    levelMs = 0.0f;
    counter = 0;
    updateFilters();
}

void SculptModule::updateFilters() noexcept
{
    for (int b = 0; b < kSculptBands; ++b)
    {
        const double f = bandFreqs[(size_t) b];
        eqL[(size_t) b].setPeak (sr, f, 1.1, applied[(size_t) b]);
        eqR[(size_t) b].setPeak (sr, f, 1.1, applied[(size_t) b]);
    }
}

void SculptModule::process (float* l, float* r, int n) noexcept
{
    constexpr int interval = 64;
    const auto& target = sculptTargets[std::max (0, std::min (kSculptStyles - 1, params.style))];
    float targetMean = 0.0f;
    for (float t : target) targetMean += t;
    targetMean /= (float) kSculptBands;

    for (int i = 0; i < n; ++i)
    {
        const float m = 0.5f * (l[i] + r[i]);
        for (int b = 0; b < kSculptBands; ++b)
        {
            const float y = analysis[(size_t) b].process (m);
            power[(size_t) b] = y * y + powerCoeff * (power[(size_t) b] - y * y);
        }
        levelMs = m * m + powerCoeff * (levelMs - m * m);

        if (++counter >= interval)
        {
            counter = 0;
            const float alpha = 1.0f - std::exp (-(float) interval / (std::max (20.0f, params.speedMs) * 0.001f * (float) sr));
            const bool active = 10.0f * std::log10 (levelMs + 1.0e-12f) > -50.0f;
            std::array<float, kSculptBands> db {};
            float mean = 0.0f;
            for (int b = 0; b < kSculptBands; ++b)
            {
                db[(size_t) b] = 10.0f * std::log10 (power[(size_t) b] + 1.0e-12f);
                mean += db[(size_t) b];
            }
            mean /= (float) kSculptBands;
            for (int b = 0; b < kSculptBands; ++b)
            {
                const float tilt = params.tiltDb * ((float) b - 3.5f) / 3.5f;
                float desired = applied[(size_t) b];
                if (active)
                {
                    const float diff = (target[b] - targetMean) - (db[(size_t) b] - mean);
                    desired = clampf (diff * params.amount, -9.0f, 9.0f) + tilt;
                }
                applied[(size_t) b] += (desired - applied[(size_t) b]) * alpha;
            }
            updateFilters();
        }

        float yl = l[i], yr = r[i];
        for (int b = 0; b < kSculptBands; ++b)
        {
            yl = eqL[(size_t) b].process (yl);
            yr = eqR[(size_t) b].process (yr);
        }
        l[i] = yl;
        r[i] = yr;
    }
}

//==============================================================================
// EQ
void EqModule::design (Biquad& b, double sr, int band, float freq, float gainDb, float q) noexcept
{
    if (band == 0)
        b.setLowShelf (sr, freq, gainDb, std::max (0.3f, std::min (1.0f, q)));
    else if (band == kEqBands - 1)
        b.setHighShelf (sr, freq, gainDb, std::max (0.3f, std::min (1.0f, q)));
    else
        b.setPeak (sr, freq, std::max (0.1f, q), gainDb);
}

void EqModule::setParams (const EqParams& p) noexcept
{
    params = p;
    for (int b = 0; b < kEqBands; ++b)
    {
        const size_t i = (size_t) b;
        if (p.freq[i] != last.freq[i] || p.gain[i] != last.gain[i] || p.q[i] != last.q[i])
        {
            design (filtL[i], sr, b, p.freq[i], p.gain[i], p.q[i]);
            design (filtR[i], sr, b, p.freq[i], p.gain[i], p.q[i]);
        }
    }
    last = p;
}

void EqModule::process (float* l, float* r, int n) noexcept
{
    for (int b = 0; b < kEqBands; ++b)
    {
        if (std::abs (params.gain[(size_t) b]) < 0.01f)
            continue;
        auto& fl = filtL[(size_t) b];
        auto& fr = filtR[(size_t) b];
        for (int i = 0; i < n; ++i)
        {
            l[i] = fl.process (l[i]);
            r[i] = fr.process (r[i]);
        }
    }
}

//==============================================================================
// Compressor
void CompressorModule::prepare (double sampleRate)
{
    sr = sampleRate;
    rmsCoeff = msCoeff (sr, 10.0f);
    chargeCoeff = msCoeff (sr, 1500.0f);
    for (int ch = 0; ch < 2; ++ch)
    {
        xLow[(size_t) ch].setFrequency (sr, 250.0);
        xHigh[(size_t) ch].setFrequency (sr, 3000.0);
        xComp[(size_t) ch].setFrequency (sr, 3000.0);
    }
    reset();
}

void CompressorModule::reset()
{
    for (auto& d : det) d = {};
    for (auto* x : { &xLow, &xHigh, &xComp })
        for (auto& c : *x) c.reset();
    grDb = 0.0f;
}

void CompressorModule::setParams (const CompParams& p) noexcept
{
    params = p;
}

float CompressorModule::computeGr (Detector& d, float levelDb, float kneeDb, float att, float rel) noexcept
{
    const float over = levelDb - params.thresholdDb;
    float ratio = std::max (1.0f, params.ratio);
    if (params.mode == CompMode::tube) // vari-mu: ratio grows with level
        ratio = 1.0f + (ratio - 1.0f) * clampf ((over + kneeDb * 0.5f) / 12.0f, 0.2f, 1.0f);
    const float slope = 1.0f - 1.0f / ratio;
    float gr = 0.0f;
    if (2.0f * over <= -kneeDb)
        gr = 0.0f;
    else if (2.0f * std::abs (over) < kneeDb)
        gr = slope * (over + kneeDb * 0.5f) * (over + kneeDb * 0.5f) / (2.0f * kneeDb);
    else
        gr = slope * over;

    d.envDb = gr + (gr > d.envDb ? att : rel) * (d.envDb - gr);
    return d.envDb;
}

void CompressorModule::process (float* l, float* r, int n) noexcept
{
    const auto mode = params.mode;
    float attackMs = std::max (0.05f, params.attackMs), releaseMs = std::max (5.0f, params.releaseMs), knee = 6.0f;
    switch (mode)
    {
        case CompMode::opto:  knee = 10.0f; attackMs = std::max (attackMs, 3.0f); break;
        case CompMode::fet:   knee = 2.0f; attackMs *= 0.25f; break;
        case CompMode::tube:  knee = 12.0f; attackMs *= 1.5f; break;
        default: break;
    }
    const float att = msCoeff (sr, attackMs);
    const float makeup = dbToGain (params.makeupDb);
    const float mix = clampf (params.mix, 0.0f, 1.0f);
    const bool useRms = mode == CompMode::opto || mode == CompMode::tube;
    float maxGr = 0.0f;

    for (int i = 0; i < n; ++i)
    {
        const float dl = l[i], dr = r[i];
        float yl, yr;

        if (mode == CompMode::multi)
        {
            float bl[3], br[3], lowA, lowB, rest;
            xLow[0].process (dl, lowA, rest);
            xHigh[0].process (rest, bl[1], bl[2]);
            xComp[0].process (lowA, lowA, lowB);
            bl[0] = lowA + lowB;
            xLow[1].process (dr, lowA, rest);
            xHigh[1].process (rest, br[1], br[2]);
            xComp[1].process (lowA, lowA, lowB);
            br[0] = lowA + lowB;

            const float rel = msCoeff (sr, releaseMs);
            yl = yr = 0.0f;
            float bandMax = 0.0f;
            for (int b = 0; b < 3; ++b)
            {
                const float pk = std::max (std::abs (bl[b]), std::abs (br[b]));
                // Bands sit lower than the full signal, offset the threshold so the bands react similarly
                const float gr = computeGr (det[(size_t) b], gainToDb (pk) + 4.0f, knee, att, rel);
                bandMax = std::max (bandMax, gr);
                const float g = dbToGain (-gr);
                yl += bl[b] * g;
                yr += br[b] * g;
            }
            maxGr = std::max (maxGr, bandMax);
        }
        else
        {
            const float pk = std::max (std::abs (dl), std::abs (dr));
            float levelDb;
            auto& d = det[0];
            if (useRms)
            {
                d.rms = pk * pk + rmsCoeff * (d.rms - pk * pk);
                levelDb = 10.0f * std::log10 (d.rms * 2.0f + 1.0e-12f);
            }
            else
                levelDb = gainToDb (pk);

            float relMs = releaseMs;
            if (mode == CompMode::opto)
            {
                const float target = d.envDb > 3.0f ? 1.0f : 0.0f;
                d.charge = target + chargeCoeff * (d.charge - target);
                relMs *= 0.6f + 2.5f * d.charge; // longer release after sustained compression
            }
            const float gr = computeGr (d, levelDb, knee, att, msCoeff (sr, relMs));
            maxGr = std::max (maxGr, gr);
            const float g = dbToGain (-gr);
            yl = dl * g;
            yr = dr * g;

            if (mode == CompMode::fet)
            {
                const float a = clampf (gr / 24.0f, 0.0f, 0.4f);
                yl = (1.0f - a) * yl + a * std::tanh (1.5f * yl) / 1.5f;
                yr = (1.0f - a) * yr + a * std::tanh (1.5f * yr) / 1.5f;
            }
        }

        yl *= makeup;
        yr *= makeup;
        l[i] = dl + mix * (yl - dl);
        r[i] = dr + mix * (yr - dr);
    }
    grDb = maxGr;
}

//==============================================================================
// Saturation
void SaturationModule::prepare (double sampleRate)
{
    sr = sampleRate;
    for (int ch = 0; ch < 2; ++ch)
    {
        dry[(size_t) ch].prepare (getLatency() + 4);
        dry[(size_t) ch].setDelay (getLatency());
        exciterHp[(size_t) ch].setHighPass (sr, 3000.0);
        radioHp[(size_t) ch].setHighPass (sr, 450.0, 0.9);
        radioLp[(size_t) ch].setLowPass (sr, 3400.0, 0.9);
        dcBlock[(size_t) ch].setHighPass (sr, 12.0);
        tapeLp[(size_t) ch].setLowPass (sr, 15000.0, 0.6);
    }
    driveSm.setTime (sr, 20.0f);
    lastTone = 1000.0f;
    setParams (params);
    reset();
}

void SaturationModule::reset()
{
    for (auto& o : os) o.reset();
    for (auto& d : dry) d.reset();
    for (auto* arr : { &exciterHp, &toneLow, &toneHigh, &radioHp, &radioLp, &dcBlock, &tapeLp })
        for (auto& b : *arr) b.reset();
    driveSm.snap (params.driveDb);
}

void SaturationModule::setParams (const SatParams& p) noexcept
{
    params = p;
    if (std::abs (p.tone - lastTone) > 1.0e-3f)
    {
        lastTone = p.tone;
        for (int ch = 0; ch < 2; ++ch)
        {
            toneLow[(size_t) ch].setLowShelf (sr, 300.0, -p.tone * 5.0);
            toneHigh[(size_t) ch].setHighShelf (sr, 3000.0, p.tone * 5.0);
        }
    }
}

float SaturationModule::shape (float x, int) noexcept
{
    switch (params.mode)
    {
        case SatMode::tape:    return std::tanh (x + 0.12f) - 0.119427f;
        case SatMode::tube:    return x >= 0.0f ? std::tanh (x) : std::tanh (0.6f * x) / 0.6f;
        case SatMode::console:
        {
            const float c = clampf (x, -1.5f, 1.5f);
            return c - c * c * c / 6.75f;
        }
        case SatMode::radio:   return clampf (x, -0.5f, 0.5f) * 2.0f;
        case SatMode::exciter: return std::tanh (x);
        case SatMode::numModes: break;
    }
    return x;
}

void SaturationModule::process (float* l, float* r, int n) noexcept
{
    const float mix = clampf (params.mix, 0.0f, 1.0f);
    const auto mode = params.mode;
    float* chans[2] = { l, r };

    for (int i = 0; i < n; ++i)
    {
        const float drive = dbToGain (driveSm.next (params.driveDb));
        for (int ch = 0; ch < 2; ++ch)
        {
            const size_t c = (size_t) ch;
            const float x = chans[ch][i];
            const float d = dry[c].process (x);
            float wet;
            if (mode == SatMode::exciter)
            {
                const float h = exciterHp[c].process (x);
                float u0, u1;
                os[c].upsample (h * drive, u0, u1);
                const float s = os[c].downsample (shape (u0, ch), shape (u1, ch));
                wet = d + 0.8f * s / std::sqrt (drive);
            }
            else
            {
                float u0, u1;
                os[c].upsample (x * drive, u0, u1);
                float s = os[c].downsample (shape (u0, ch), shape (u1, ch));
                if (mode == SatMode::radio)
                    s = radioLp[c].process (radioHp[c].process (s)) * 0.6f / std::sqrt (drive);
                else
                    s /= drive;
                if (mode == SatMode::tube || mode == SatMode::tape)
                    s = dcBlock[c].process (s);
                if (mode == SatMode::tape)
                    s = tapeLp[c].process (s);
                wet = s;
            }
            wet = toneHigh[c].process (toneLow[c].process (wet));
            chans[ch][i] = d + mix * (wet - d);
        }
    }
}

//==============================================================================
// Limiter
void LimiterModule::prepare (double sampleRate, int lookaheadSamples)
{
    sr = sampleRate;
    lookahead = std::max (0, lookaheadSamples);
    for (auto& d : delay) d.prepare (lookahead + 4);
    gainRing.assign ((size_t) lookahead + 1, 1.0f);
    smoothRing.assign ((size_t) std::max (1, lookahead), 1.0f);
    relCoeff = msCoeff (sr, 60.0f);
    reset();
}

void LimiterModule::reset()
{
    for (auto& d : delay) d.reset();
    std::fill (gainRing.begin(), gainRing.end(), 1.0f);
    std::fill (smoothRing.begin(), smoothRing.end(), 1.0f);
    smoothSum = (double) smoothRing.size();
    ringPos = smoothPos = 0;
    env = 1.0f;
    grDb = 0.0f;
}

void LimiterModule::process (float* l, float* r, int n, float ceilingDb, bool enabled) noexcept
{
    const float ceiling = dbToGain (ceilingDb);
    float minG = 1.0f;
    for (int i = 0; i < n; ++i)
    {
        const float pk = std::max (std::abs (l[i]), std::abs (r[i]));
        const float req = enabled && pk > ceiling ? ceiling / pk : 1.0f;
        float g, ol, orr;
        if (lookahead == 0)
        {
            env = req < env ? req : req + relCoeff * (env - req);
            g = env;
            ol = l[i];
            orr = r[i];
        }
        else
        {
            gainRing[(size_t) ringPos] = req;
            ringPos = (ringPos + 1) % (int) gainRing.size();
            float mn = 1.0f;
            for (float v : gainRing) mn = std::min (mn, v);
            env = mn < env ? mn : mn + relCoeff * (env - mn);

            smoothSum -= smoothRing[(size_t) smoothPos];
            smoothRing[(size_t) smoothPos] = env;
            smoothSum += env;
            smoothPos = (smoothPos + 1) % (int) smoothRing.size();
            g = std::min (1.0f, (float) (smoothSum / (double) smoothRing.size()));

            delay[0].push (l[i]);
            delay[1].push (r[i]);
            ol = delay[0].readInt (lookahead + 1);
            orr = delay[1].readInt (lookahead + 1);
        }
        ol *= g;
        orr *= g;
        if (enabled)
        {
            ol = clampf (ol, -ceiling, ceiling);
            orr = clampf (orr, -ceiling, ceiling);
        }
        l[i] = ol;
        r[i] = orr;
        minG = std::min (minG, g);
    }
    grDb = -gainToDb (minG);
}
} // namespace lavc
