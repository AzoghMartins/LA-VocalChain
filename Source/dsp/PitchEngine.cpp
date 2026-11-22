#include "PitchEngine.h"

#include <cstring>

namespace lavc
{
//==============================================================================
uint16_t scaleMask (Scale s) noexcept
{
    auto bits = [] (std::initializer_list<int> degrees)
    {
        uint16_t m = 0;
        for (int d : degrees)
            m = (uint16_t) (m | (1u << d));
        return m;
    };

    switch (s)
    {
        case Scale::chromatic:       return 0x0fff;
        case Scale::major:           return bits ({ 0, 2, 4, 5, 7, 9, 11 });
        case Scale::minor:           return bits ({ 0, 2, 3, 5, 7, 8, 10 });
        case Scale::harmonicMinor:   return bits ({ 0, 2, 3, 5, 7, 8, 11 });
        case Scale::melodicMinor:    return bits ({ 0, 2, 3, 5, 7, 9, 11 });
        case Scale::majorPentatonic: return bits ({ 0, 2, 4, 7, 9 });
        case Scale::minorPentatonic: return bits ({ 0, 3, 5, 7, 10 });
        case Scale::blues:           return bits ({ 0, 3, 5, 6, 7, 10 });
        case Scale::dorian:          return bits ({ 0, 2, 3, 5, 7, 9, 10 });
        case Scale::phrygian:        return bits ({ 0, 1, 3, 5, 7, 8, 10 });
        case Scale::lydian:          return bits ({ 0, 2, 4, 6, 7, 9, 11 });
        case Scale::mixolydian:      return bits ({ 0, 2, 4, 5, 7, 9, 10 });
        case Scale::numScales:       break;
    }
    return 0x0fff;
}

const char* scaleName (Scale s) noexcept
{
    static const char* names[] = { "Chromatic", "Major", "Minor", "Harmonic Minor", "Melodic Minor", "Major Pentatonic",
                                   "Minor Pentatonic", "Blues", "Dorian", "Phrygian", "Lydian", "Mixolydian" };
    const int i = (int) s;
    return i >= 0 && i < (int) Scale::numScales ? names[i] : "Chromatic";
}

const char* keyName (int pc) noexcept
{
    static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    return names[((pc % 12) + 12) % 12];
}

static bool inScale (int note, int key, uint16_t mask) noexcept
{
    const int degree = (((note - key) % 12) + 12) % 12;
    return (mask >> degree) & 1u;
}

int quantizeToScale (float midi, int key, Scale scale) noexcept
{
    const uint16_t mask = scaleMask (scale);
    const int centre = (int) std::lround (midi);
    int best = centre;
    float bestDist = 1.0e9f;
    for (int n = centre - 6; n <= centre + 6; ++n)
    {
        if (! inScale (n, key, mask))
            continue;
        const float d = std::abs ((float) n - midi);
        if (d < bestDist)
        {
            bestDist = d;
            best = n;
        }
    }
    return best;
}

int scaleStep (int note, int steps, int key, Scale scale) noexcept
{
    const uint16_t mask = scaleMask (scale);
    int n = note;
    const int dir = steps > 0 ? 1 : -1;
    for (int s = 0; s < std::abs (steps); ++s)
    {
        n += dir;
        int guard = 0;
        while (! inScale (n, key, mask) && guard++ < 12)
            n += dir;
    }
    return n;
}

//==============================================================================
void PitchEngine::prepare (double sampleRate, int, bool trackingMode)
{
    sr = sampleRate;
    tracking = trackingMode;

    decim = std::max (1, (int) (sr / 11000.0));
    decSr = sr / decim;
    decimLp.setLowPass (sr, std::min (1800.0, decSr * 0.4));

    tauMin = std::max (2, (int) std::floor (decSr / 1100.0));
    tauMax = (int) std::ceil (decSr / 70.0);
    yinW = tauMax + 16;
    hop = std::max (1, (int) std::lround (decSr * 0.0025));
    int decSize = 1;
    while (decSize < (yinW + tauMax) * 2) decSize <<= 1;
    decRing.assign ((size_t) decSize, 0.0f);
    decMask = decSize - 1;
    yinBuf.assign ((size_t) (yinW + tauMax + 2), 0.0f);
    cmBuf.assign ((size_t) (tauMax + 2), 1.0f);

    maxPeriod = (int) std::ceil (sr / 70.0);
    minPeriod = (int) std::floor (sr / 1100.0);
    unvoicedSpacing = (int) std::lround (sr * 0.004);

    int n = 1;
    while (n < (int) (sr * 0.35)) n <<= 1;
    inRing.assign ((size_t) n, 0.0f);
    inMask = n - 1;
    outMask = inMask;
    for (auto* v : { &lead, &harmonies[0], &harmonies[1], &harmonies[2], &harmonies[3] })
    {
        v->acc.assign ((size_t) n, 0.0f);
        v->wsum.assign ((size_t) n, 0.0f);
    }

    marks.assign ((size_t) kMarkCap, Mark {});

    if (tracking)
    {
        markLag = 0;
        outLatency = std::max (8, (int) std::lround (sr * 0.004));
        analysisLag = 0;
    }
    else
    {
        // Align marks with the centre of the detection window, and leave room for a full grain.
        const int centreLag = (int) std::lround ((yinW + tauMax) * 0.5 * decim + hop * decim * 0.5);
        markLag = std::max (centreLag, maxPeriod);
        outLatency = maxPeriod + 2;
        analysisLag = markLag;
    }
    contentLatency = outLatency + analysisLag;
    dryDelay.prepare (contentLatency + 4);
    dryDelay.setDelay (contentLatency);

    tuneMix.setTime (sr, 15.0f);
    for (auto& p : panL) p.setTime (sr, 30.0f);
    for (auto& p : panR) p.setTime (sr, 30.0f);

    uint32_t seed = 7u;
    for (auto& h : harmonies)
    {
        h.drift = RandomLfo (seed++ * 2654435761u);
        h.timing = RandomLfo (seed++ * 2246822519u);
        h.drift.setRate (sr, 0.35f);
        h.timing.setRate (sr, 0.25f);
    }
    reset();
}

void PitchEngine::reset()
{
    std::fill (inRing.begin(), inRing.end(), 0.0f);
    std::fill (decRing.begin(), decRing.end(), 0.0f);
    decimLp.reset();
    decimCount = 0;
    decCount = 0;
    hopCount = 0;
    tNow = 0;
    curPeriod = 0.0f;
    curLevelDb = -120.0f;
    recentPeriods.fill (0.0f);
    recentCount = 0;
    numMarks = 0;
    nextMarkPos = 0;
    corr = 0.0f;
    lastTarget = -1;
    dryDelay.reset();
    tuneMix.snap (params.tuneEnabled ? 1.0f : 0.0f);

    for (auto* v : { &lead, &harmonies[0], &harmonies[1], &harmonies[2], &harmonies[3] })
    {
        std::fill (v->acc.begin(), v->acc.end(), 0.0f);
        std::fill (v->wsum.begin(), v->wsum.end(), 0.0f);
        v->nextSynth = -outLatency + unvoicedSpacing;
        v->markIndex = 0;
        v->gain = 0.0f;
    }
    for (size_t i = 0; i < panL.size(); ++i)
    {
        panL[i].snap (0.0f);
        panR[i].snap (0.0f);
    }
}

//==============================================================================
void PitchEngine::analyse (float x) noexcept
{
    const float lp = decimLp.process (x);
    if (++decimCount < decim)
        return;
    decimCount = 0;

    decRing[(size_t) (decCount & decMask)] = lp;
    ++decCount;
    levelAcc += lp * lp;
    ++levelCount;

    if (++hopCount >= hop)
    {
        hopCount = 0;
        runDetection();
    }
}

void PitchEngine::runDetection() noexcept
{
    const int len = yinW + tauMax;
    if (decCount < len)
        return;

    const int64_t start = decCount - len;
    for (int j = 0; j < len; ++j)
        yinBuf[(size_t) j] = decRing[(size_t) ((start + j) & decMask)];

    double energy = 0.0;
    for (int j = len - yinW; j < len; ++j)
        energy += (double) yinBuf[(size_t) j] * yinBuf[(size_t) j];
    curLevelDb = 10.0f * std::log10 ((float) (energy / yinW) + 1.0e-12f);

    float detectedPeriod = 0.0f;

    if (curLevelDb > -55.0f)
    {
        // Difference function + cumulative mean normalisation
        float bestTau = -1.0f, bestVal = 1.0e9f;
        double running = 0.0;
        int firstDip = -1;
        auto& cm = cmBuf;
        cm[0] = 1.0f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double d = 0.0;
            const float* a = yinBuf.data();
            const float* b = yinBuf.data() + tau;
            for (int j = 0; j < yinW; ++j)
            {
                const float diff = a[j] - b[j];
                d += (double) diff * diff;
            }
            running += d;
            const float c = running > 0.0 ? (float) (d * tau / running) : 1.0f;
            cm[(size_t) tau] = c;
            if (tau >= tauMin && c < bestVal)
            {
                bestVal = c;
                bestTau = (float) tau;
            }
        }

        // First dip below the absolute threshold (avoids octave-down errors)
        constexpr float threshold = 0.15f;
        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (cm[(size_t) tau] < threshold)
            {
                while (tau + 1 <= tauMax && cm[(size_t) tau + 1] < cm[(size_t) tau])
                    ++tau;
                firstDip = tau;
                break;
            }
        }

        int tauI = firstDip > 0 ? firstDip : (int) bestTau;
        const float val = tauI > 0 ? cm[(size_t) tauI] : 1.0f;

        if (tauI > 0 && val < 0.25f)
        {
            float tauF = (float) tauI;
            if (tauI > 1 && tauI < tauMax)
            {
                const float s0 = cm[(size_t) tauI - 1], s1 = cm[(size_t) tauI], s2 = cm[(size_t) tauI + 1];
                const float den = s0 - 2.0f * s1 + s2;
                if (std::abs (den) > 1.0e-9f)
                    tauF += clampf (0.5f * (s0 - s2) / den, -1.0f, 1.0f);
            }

            // Refine at full rate around the decimated estimate
            const int approx = (int) std::lround (tauF * decim);
            if (decim > 1 && approx > minPeriod / 2)
            {
                const int W = std::min (yinW * decim, (int) inRing.size() / 4);
                const int lo = std::max (2, approx - decim - 1), hi = approx + decim + 1;
                if (tNow > W + hi + 2)
                {
                    const int64_t base = tNow - W - hi - 1;
                    double vals[64] {};
                    int count = std::min (hi - lo + 1, 64);
                    for (int k = 0; k < count; ++k)
                    {
                        const int p = lo + k;
                        double d = 0.0;
                        for (int j = 0; j < W; ++j)
                        {
                            const float diff = inRing[(size_t) ((base + j) & inMask)] - inRing[(size_t) ((base + j + p) & inMask)];
                            d += (double) diff * diff;
                        }
                        vals[k] = d;
                    }
                    int bk = 0;
                    for (int k = 1; k < count; ++k)
                        if (vals[k] < vals[bk]) bk = k;
                    float refined = (float) (lo + bk);
                    if (bk > 0 && bk < count - 1)
                    {
                        const double den = vals[bk - 1] - 2.0 * vals[bk] + vals[bk + 1];
                        if (std::abs (den) > 1.0e-12)
                            refined += (float) clampf ((float) (0.5 * (vals[bk - 1] - vals[bk + 1]) / den), -1.0f, 1.0f);
                    }
                    tauF = refined / (float) decim;
                }
            }
            detectedPeriod = tauF * decim;
        }
    }

    // Median of the last three estimates removes isolated octave jumps
    recentPeriods[(size_t) (recentCount % 3)] = detectedPeriod;
    ++recentCount;
    if (detectedPeriod <= 0.0f)
    {
        curPeriod = 0.0f;
        return;
    }
    std::array<float, 3> sorted = recentPeriods;
    std::sort (sorted.begin(), sorted.end());
    const float med = sorted[1] > 0.0f ? sorted[1] : detectedPeriod;
    // Accept the new value unless it is an outlier against the median
    curPeriod = std::abs (detectedPeriod - med) / med < 0.2f ? detectedPeriod : med;
}

//==============================================================================
void PitchEngine::computeTargets (Mark& m) noexcept
{
    m.harmOn.fill (false);
    m.harm.fill (0.0f);

    if (! m.voiced)
    {
        corr *= 0.7f;
        lastTarget = -1;
        m.leadShift = 0.0f;
        return;
    }

    const float src = m.srcMidi;
    lastInputMidi = src;
    const auto scale = params.scale;
    const int key = params.key;

    int target;
    if (params.targetMode == 1 && heldNotes.any())
    {
        uint16_t pcs = 0;
        for (int i = 0; i < 128; ++i)
            if (heldNotes.test ((size_t) i))
                pcs = (uint16_t) (pcs | (1u << (i % 12)));
        target = (int) std::lround (src);
        float best = 1.0e9f;
        for (int nn = (int) src - 7; nn <= (int) src + 7; ++nn)
        {
            if (((pcs >> (((nn % 12) + 12) % 12)) & 1u) == 0)
                continue;
            const float d = std::abs ((float) nn - src);
            if (d < best) { best = d; target = nn; }
        }
    }
    else
    {
        const uint16_t mask = scaleMask (scale);
        if (lastTarget >= 0 && std::abs (src - (float) lastTarget) < 0.62f && inScale (lastTarget, key, mask))
            target = lastTarget; // hysteresis: hold the note while the singer hovers between two notes
        else
            target = quantizeToScale (src, key, scale);
    }
    lastTarget = target;

    if (keyLearning)
    {
        const float dev = std::abs (src - std::round (src));
        const double w = (double) m.period / sr * (dev < 0.3f ? 1.0 : 0.3);
        pcHistogram[(size_t) ((((int) std::lround (src)) % 12 + 12) % 12)] += w;
    }

    float dev = (float) target - src;
    const float tol = params.toleranceCents * 0.01f;
    if (std::abs (dev) <= tol)
        dev = 0.0f;
    else
        dev -= std::copysign (tol, dev);
    const float corrTarget = dev * params.amount;
    const float alpha = params.speedMs <= 0.05f ? 1.0f
                                                : 1.0f - std::exp (-(float) m.period / (params.speedMs * 0.001f * (float) sr));
    corr += (corrTarget - corr) * alpha;

    m.leadShift = params.tuneEnabled ? corr + params.transpose : 0.0f;

    if (! params.harmEnabled)
        return;

    const float leadOffset = m.leadShift; // harmonies follow the lead as it is heard
    if (params.harmMode == 2)
    {
        int notes[kNumHarmonyVoices] {};
        int count = 0;
        for (int i = 0; i < 128 && count < kNumHarmonyVoices; ++i)
            if (heldNotes.test ((size_t) i))
                notes[count++] = i;
        for (int v = 0; v < kNumHarmonyVoices; ++v)
        {
            if (! params.voices[(size_t) v].enabled || v >= count)
                continue;
            m.harmOn[(size_t) v] = true;
            m.harm[(size_t) v] = (float) notes[v] - src;
        }
        return;
    }

    for (int v = 0; v < kNumHarmonyVoices; ++v)
    {
        const auto& vp = params.voices[(size_t) v];
        if (! vp.enabled)
            continue;
        float interval = (float) vp.interval;
        if (params.harmMode == 0)
            interval = (float) (scaleStep (target, vp.interval, key, scale) - target);
        m.harmOn[(size_t) v] = true;
        m.harm[(size_t) v] = leadOffset + interval;
    }
}

void PitchEngine::placeMarks() noexcept
{
    const int64_t limit = tNow - 1 - markLag;
    while (nextMarkPos <= limit)
    {
        Mark m;
        m.pos = nextMarkPos;
        if (curPeriod > 0.0f)
        {
            m.voiced = true;
            m.period = std::max (minPeriod, std::min (maxPeriod, (int) std::lround (curPeriod)));
            m.srcMidi = 69.0f + 12.0f * std::log2 ((float) (sr / curPeriod) / 440.0f);
        }
        else
        {
            m.voiced = false;
            m.period = unvoicedSpacing;
        }
        computeTargets (m);
        marks[(size_t) (numMarks & (kMarkCap - 1))] = m;
        ++numMarks;
        nextMarkPos += m.period;
    }
}

float PitchEngine::readInput (double pos) const noexcept
{
    if (pos < (double) (tNow - (int64_t) inRing.size() + 2) || pos > (double) (tNow - 1) || pos < 0.0)
        return 0.0f;
    const int64_t i = (int64_t) std::floor (pos);
    const float f = (float) (pos - (double) i);
    const float a = inRing[(size_t) (i & inMask)];
    const float b = i + 1 <= tNow - 1 ? inRing[(size_t) ((i + 1) & inMask)] : a;
    return a + (b - a) * f;
}

void PitchEngine::synthesise (SynthVoice& v, int voiceIndex, int64_t emit) noexcept
{
    if (numMarks == 0)
        return;

    for (int guard = 0; guard < 64; ++guard)
    {
        const int lastT = v.markIndex < numMarks && v.markIndex > numMarks - kMarkCap ? markAt (v.markIndex).period : unvoicedSpacing;
        if (v.nextSynth > emit + lastT)
            break;
        if (v.nextSynth + lastT < emit) // fell behind (e.g. after a reset): resynchronise
        {
            v.nextSynth = emit + lastT;
            continue;
        }

        const int64_t s = v.nextSynth;
        const bool isLead = voiceIndex < 0;
        const auto* vp = isLead ? nullptr : &params.voices[(size_t) voiceIndex];
        const float formantSemis = isLead ? (params.tuneEnabled ? params.formant : 0.0f) : vp->formant;
        const float fr = std::pow (2.0f, clampf (formantSemis, -12.0f, 12.0f) / 12.0f);

        // Analysis time we would like to use
        double extraLag = 0.0;
        if (! isLead)
            extraLag = sr * 0.001 * (4.0 + 6.0 * voiceIndex + 18.0 * (0.5 + 0.5 * v.timing.next())) * params.harmHumanize;
        const double wanted = (double) (s - analysisLag) - extraLag;

        int64_t oldest = std::max<int64_t> (0, numMarks - kMarkCap + 1);
        int64_t idx = std::max (oldest, std::min (v.markIndex, numMarks - 1));
        auto available = [&] (int64_t i)
        {
            const auto& mk = markAt (i);
            return (double) mk.pos + mk.period * std::max (1.0f, fr) <= (double) (tNow - 1);
        };
        while (idx + 1 < numMarks && available (idx + 1)
               && std::abs ((double) markAt (idx + 1).pos - wanted) <= std::abs ((double) markAt (idx).pos - wanted))
            ++idx;
        while (idx > oldest && ! available (idx))
            --idx;
        while (idx > oldest && (double) markAt (idx).pos - wanted > markAt (idx).period * 0.5
               && std::abs ((double) markAt (idx - 1).pos - wanted) < std::abs ((double) markAt (idx).pos - wanted))
            --idx;
        v.markIndex = idx;
        const Mark& m = markAt (idx);
        const int T = std::max (2, m.period);

        float shift = 0.0f, gainTarget = 1.0f;
        if (isLead)
        {
            shift = m.voiced ? m.leadShift : 0.0f;
        }
        else
        {
            v.driftCents = v.drift.next() * 14.0f * params.harmHumanize;
            const bool on = m.voiced && m.harmOn[(size_t) voiceIndex];
            shift = m.harm[(size_t) voiceIndex] + v.driftCents * 0.01f;
            gainTarget = on ? 1.0f : 0.0f;
        }

        const float ratio = std::pow (2.0f, clampf (shift, -24.0f, 24.0f) / 12.0f);
        v.gain += (gainTarget - v.gain) * 0.3f;
        // Shifting down leaves gaps between grains: compensate the lost power.
        const float g = v.gain * (ratio < 1.0f ? 1.0f / std::sqrt (std::max (0.5f, ratio)) : 1.0f);

        const double inv = kPi / T;
        for (int k = -T; k <= T; ++k)
        {
            const int64_t o = s + k;
            if (o < emit)
                continue;
            const float w = 0.5f + 0.5f * (float) std::cos (inv * k);
            const size_t oi = (size_t) (o & outMask);
            v.wsum[oi] += w;
            if (g > 1.0e-4f)
                v.acc[oi] += g * w * readInput ((double) m.pos + (double) k * fr);
        }

        const double spacing = std::max (1.0, (double) T / ratio);
        v.nextSynth = s + std::max<int64_t> (1, (int64_t) std::lround (spacing));
    }
}

//==============================================================================
void PitchEngine::process (const float* in, float* leadOut, float* harmL, float* harmR, uint8_t* voiced, int numSamples,
                           PitchListener* listener) noexcept
{
    const bool runLead = params.tuneEnabled || tuneMix.value > 1.0e-4f;
    const bool runHarm = params.harmEnabled;
    const float harmGain = dbToGain (params.harmLevelDb);

    std::array<float, kNumHarmonyVoices> targetL {}, targetR {};
    for (size_t v = 0; v < targetL.size(); ++v)
    {
        const auto& vp = params.voices[v];
        const float lvl = runHarm && vp.enabled ? dbToGain (vp.levelDb) * harmGain : 0.0f;
        const float pan = clampf (vp.pan, -1.0f, 1.0f);
        const float angle = (pan + 1.0f) * 0.25f * (float) kPi;
        targetL[v] = lvl * std::cos (angle);
        targetR[v] = lvl * std::sin (angle);
    }

    int64_t lastReportedMark = numMarks;

    for (int i = 0; i < numSamples; ++i)
    {
        const float x = in[i];
        inRing[(size_t) (tNow & inMask)] = x;
        ++tNow;
        analyse (x);
        placeMarks();

        const int64_t emit = tNow - outLatency;

        float leadPsola = 0.0f;
        if (runLead)
        {
            synthesise (lead, -1, emit);
            const size_t ei = (size_t) (emit & outMask);
            leadPsola = lead.acc[ei] / std::max (1.0f, lead.wsum[ei]);
            lead.acc[ei] = lead.wsum[ei] = 0.0f;
        }
        else
        {
            lead.nextSynth = emit + unvoicedSpacing;
            lead.markIndex = std::max<int64_t> (0, numMarks - 1);
        }

        const float dry = dryDelay.process (x);
        const float mixT = tuneMix.next (params.tuneEnabled ? 1.0f : 0.0f);
        leadOut[i] = dry + mixT * (leadPsola - dry);

        float hl = 0.0f, hr = 0.0f;
        for (int v = 0; v < kNumHarmonyVoices; ++v)
        {
            auto& h = harmonies[(size_t) v];
            const float gl = panL[(size_t) v].next (targetL[(size_t) v]);
            const float gr = panR[(size_t) v].next (targetR[(size_t) v]);
            if (runHarm && params.voices[(size_t) v].enabled)
            {
                synthesise (h, v, emit);
                const size_t ei = (size_t) (emit & outMask);
                const float y = h.acc[ei] / std::max (1.0f, h.wsum[ei]);
                h.acc[ei] = h.wsum[ei] = 0.0f;
                hl += y * gl;
                hr += y * gr;
            }
            else
            {
                h.nextSynth = emit + unvoicedSpacing;
                h.markIndex = std::max<int64_t> (0, numMarks - 1);
                h.gain = 0.0f;
            }
        }
        harmL[i] += hl;
        harmR[i] += hr;

        if (voiced != nullptr)
        {
            // voicing of the input that is heard now
            const int64_t heard = tNow - 1 - contentLatency;
            bool vo = false;
            if (numMarks > 0)
            {
                int64_t idx = std::max<int64_t> (std::max<int64_t> (0, numMarks - kMarkCap + 1), lead.markIndex);
                // search near the newest marks
                for (int64_t j = numMarks - 1; j >= std::max<int64_t> (0, numMarks - 64); --j)
                    if (markAt (j).pos <= heard) { idx = j; break; }
                vo = markAt (idx).voiced;
            }
            voiced[i] = vo ? 1 : 0;
        }
    }

    // Report analysis frames (at most one per ~5 ms) for the display
    if (listener != nullptr && numMarks > 0)
    {
        static constexpr int64_t minGap = 1;
        int64_t first = std::max<int64_t> (std::max<int64_t> (0, numMarks - kMarkCap + 1), lastReportedMark - 1);
        int64_t lastPos = -1000000;
        const int64_t gap = std::max<int64_t> (minGap, (int64_t) (sr * 0.005));
        for (int64_t j = first; j < numMarks; ++j)
        {
            const Mark& m = markAt (j);
            if (m.pos - lastPos < gap)
                continue;
            lastPos = m.pos;
            PitchFrame f;
            f.position = m.pos;
            f.voiced = m.voiced;
            f.levelDb = curLevelDb;
            if (m.voiced)
            {
                f.inputMidi = m.srcMidi;
                f.outputMidi = m.srcMidi + m.leadShift;
                f.targetMidi = (float) quantizeToScale (m.srcMidi, params.key, params.scale);
                for (int v = 0; v < kNumHarmonyVoices; ++v)
                    f.harmonyMidi[(size_t) v] = m.harmOn[(size_t) v] ? m.srcMidi + m.harm[(size_t) v] : 0.0f;
            }
            listener->pitchFrame (f);
        }
    }
}

//==============================================================================
KeyEstimate PitchEngine::estimateKey() const noexcept
{
    // Krumhansl-Kessler key profiles
    static const double majorProfile[12] = { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 };
    static const double minorProfile[12] = { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 };

    KeyEstimate est;
    double total = 0.0;
    for (double v : pcHistogram) total += v;
    est.seconds = (float) total;
    if (total <= 0.0)
        return est;

    auto correlate = [&] (const double* profile, int key)
    {
        double mx = 0.0, my = 0.0;
        for (int i = 0; i < 12; ++i) { mx += pcHistogram[(size_t) ((i + key) % 12)]; my += profile[i]; }
        mx /= 12.0; my /= 12.0;
        double sxy = 0.0, sxx = 0.0, syy = 0.0;
        for (int i = 0; i < 12; ++i)
        {
            const double dx = pcHistogram[(size_t) ((i + key) % 12)] - mx, dy = profile[i] - my;
            sxy += dx * dy; sxx += dx * dx; syy += dy * dy;
        }
        return sxx > 0.0 && syy > 0.0 ? sxy / std::sqrt (sxx * syy) : 0.0;
    };

    double best = -2.0, second = -2.0;
    for (int k = 0; k < 12; ++k)
    {
        for (int mode = 0; mode < 2; ++mode)
        {
            const double r = correlate (mode == 0 ? majorProfile : minorProfile, k);
            if (r > best)
            {
                second = best;
                best = r;
                est.key = k;
                est.minor = mode == 1;
            }
            else if (r > second)
                second = r;
        }
    }
    est.confidence = (float) clampf ((float) (best * 0.6 + (best - second) * 2.0), 0.0f, 1.0f);
    return est;
}
} // namespace lavc
