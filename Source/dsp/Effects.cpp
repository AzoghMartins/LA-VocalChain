#include "Effects.h"

namespace lavc
{
//==============================================================================
void MicroShifter::prepare (double sampleRate, float windowMs, float maxExtraDelayMs)
{
    sr = sampleRate;
    window = (float) (windowMs * 0.001 * sr);
    line.prepare ((int) ((windowMs + maxExtraDelayMs) * 0.001 * sr) + 8);
    reset();
}

void MicroShifter::reset()
{
    line.reset();
    phase = 0.0;
}

float MicroShifter::process (float x, float cents, float extraDelay) noexcept
{
    line.push (x);
    const double ratio = std::pow (2.0, cents / 1200.0);
    phase += (1.0 - ratio) / window;
    phase -= std::floor (phase);
    const double p2 = phase + 0.5 - std::floor (phase + 0.5);
    const float w1 = (float) std::pow (std::sin (kPi * phase), 2.0);
    const float w2 = 1.0f - w1;
    const float d1 = 1.0f + extraDelay + (float) phase * window;
    const float d2 = 1.0f + extraDelay + (float) p2 * window;
    return w1 * line.read (d1) + w2 * line.read (d2);
}

//==============================================================================
void DoublerModule::prepare (double sampleRate)
{
    sr = sampleRate;
    for (auto& s : shifters) s.prepare (sr, 24.0f, 80.0f);
    for (auto& l : timeLfo) l.setRate (sr, 0.4f);
    for (auto& l : pitchLfo) l.setRate (sr, 0.7f);
    for (auto& t : tone) t.setLowPass (sr, 9000.0, 0.6);
    amountSm.setTime (sr, 20.0f);
    reset();
}

void DoublerModule::reset()
{
    for (auto& s : shifters) s.reset();
    for (auto& t : tone) t.reset();
    amountSm.snap (params.amount);
}

void DoublerModule::process (float* l, float* r, int n) noexcept
{
    const float width = clampf (params.width, 0.0f, 1.0f);
    std::array<float, 2> gl {}, gr {};
    for (int v = 0; v < 2; ++v)
    {
        const float pan = (v == 0 ? -1.0f : 1.0f) * width;
        const float angle = (pan + 1.0f) * 0.25f * (float) kPi;
        gl[(size_t) v] = std::cos (angle) * 0.85f;
        gr[(size_t) v] = std::sin (angle) * 0.85f;
    }
    const float msToS = (float) (0.001 * sr);

    for (int i = 0; i < n; ++i)
    {
        const float amount = amountSm.next (clampf (params.amount, 0.0f, 1.0f));
        const float m = 0.5f * (l[i] + r[i]);
        float al = 0.0f, ar = 0.0f;
        for (int v = 0; v < 2; ++v)
        {
            const size_t k = (size_t) v;
            const float sign = v == 0 ? -1.0f : 1.0f;
            const float cents = sign * params.detuneCents + pitchLfo[k].next() * params.detuneCents * 0.5f * params.drift;
            const float delayMs = std::max (0.5f, params.delayMs * (v == 0 ? 1.0f : 1.35f) + timeLfo[k].next() * 7.0f * params.drift);
            const float y = tone[k].process (shifters[k].process (m, cents, delayMs * msToS));
            al += y * gl[k];
            ar += y * gr[k];
        }
        l[i] += amount * al;
        r[i] += amount * ar;
    }
}

//==============================================================================
void ModulationModule::prepare (double sampleRate)
{
    sr = sampleRate;
    for (auto& l : lines) l.prepare ((int) (0.05 * sr));
    reset();
}

void ModulationModule::reset()
{
    for (auto& l : lines) l.reset();
    fb.fill (0.0f);
    for (auto& s : apState) s.fill (0.0f);
    lfo.phase = 0.0;
}

void ModulationModule::process (float* l, float* r, int n) noexcept
{
    lfo.setRate (sr, std::max (0.01f, params.rateHz));
    const float depth = clampf (params.depth, 0.0f, 1.0f);
    const float feedback = clampf (params.feedback, 0.0f, 0.95f);
    const float mix = clampf (params.mix, 0.0f, 1.0f);
    const float ms = (float) (0.001 * sr);
    float* ch[2] = { l, r };

    for (int i = 0; i < n; ++i)
    {
        const double base = lfo.phase;
        lfo.next();
        for (int c = 0; c < 2; ++c)
        {
            const size_t k = (size_t) c;
            const float mod = (float) std::sin (2.0 * kPi * (base + 0.25 * c));
            const float x = ch[c][i];
            float y = 0.0f, out = x;
            switch (params.mode)
            {
                case ModMode::chorus:
                {
                    const float d = (14.0f + 6.0f * depth * mod) * ms;
                    y = lines[k].read (d);
                    lines[k].push (x + feedback * 0.3f * y);
                    out = x + mix * (y - x) * 0.85f + mix * 0.15f * y;
                    break;
                }
                case ModMode::flanger:
                {
                    const float d = (0.4f + 4.0f * depth * (0.5f + 0.5f * mod)) * ms;
                    y = lines[k].read (std::max (1.0f, d));
                    lines[k].push (x + feedback * y);
                    out = x + mix * (y - x) * 0.5f;
                    break;
                }
                case ModMode::phaser:
                case ModMode::numModes:
                {
                    const float f = 200.0f * std::pow (10.0f, (0.5f + 0.5f * mod) * (0.3f + 1.0f * depth));
                    const float t = std::tan ((float) kPi * std::min (f, (float) sr * 0.45f) / (float) sr);
                    const float a = (t - 1.0f) / (t + 1.0f);
                    float s = x + feedback * fb[k];
                    for (auto& st : apState[k])
                    {
                        const float yy = a * s + st;
                        st = s - a * yy;
                        s = yy;
                    }
                    fb[k] = s;
                    out = (1.0f - 0.5f * mix) * x + 0.5f * mix * s;
                    break;
                }
            }
            ch[c][i] = out;
        }
    }
}

//==============================================================================
namespace
{
struct SyncOption { const char* name; double beats; };
const SyncOption syncOptions[] = {
    { "Free", 0.0 },  { "1/32", 0.125 }, { "1/16T", 1.0 / 6.0 }, { "1/16", 0.25 }, { "1/16D", 0.375 },
    { "1/8T", 1.0 / 3.0 }, { "1/8", 0.5 }, { "1/8D", 0.75 }, { "1/4T", 2.0 / 3.0 }, { "1/4", 1.0 },
    { "1/4D", 1.5 }, { "1/2", 2.0 }, { "1/2D", 3.0 }, { "1 Bar", 4.0 },
};
} // namespace

int numDelaySyncOptions() noexcept { return (int) (sizeof (syncOptions) / sizeof (syncOptions[0])); }
const char* delaySyncName (int i) noexcept { return syncOptions[std::max (0, std::min (numDelaySyncOptions() - 1, i))].name; }
double delaySyncBeats (int i) noexcept { return syncOptions[std::max (0, std::min (numDelaySyncOptions() - 1, i))].beats; }

void DelayModule::prepare (double sampleRate)
{
    sr = sampleRate;
    for (auto& l : lines) l.prepare ((int) (2.6 * sr) + 16);
    timeSm.setTime (sr, 60.0f);
    duckEnv.setTimes (sr, 5.0f, 300.0f);
    wow.setRate (sr, 0.55);
    flutter.setRate (sr, 6.0f);
    lastHp = lastLp = -1.0f;
    setParams (params, bpm);
    reset();
}

void DelayModule::reset()
{
    for (auto& l : lines) l.reset();
    for (auto& f : hp) f.reset();
    for (auto& f : lp) f.reset();
    duckEnv.reset();
    float ms = params.sync > 0 ? (float) (delaySyncBeats (params.sync) * 60000.0 / std::max (20.0, bpm)) : params.timeMs;
    timeSm.snap (ms * 0.001f * (float) sr);
}

void DelayModule::setParams (const DelayParams& p, double newBpm) noexcept
{
    params = p;
    bpm = newBpm > 0.0 ? newBpm : 120.0;
    float lpHz = p.lpHz;
    if (p.mode == DelayMode::tape) lpHz = std::min (lpHz, 7000.0f);
    if (p.mode == DelayMode::slapback) lpHz = std::min (lpHz, 5000.0f);
    if (std::abs (p.hpHz - lastHp) > 0.5f)
    {
        lastHp = p.hpHz;
        for (auto& f : hp) f.setHighPass (sr, p.hpHz);
    }
    if (std::abs (lpHz - lastLp) > 0.5f)
    {
        lastLp = lpHz;
        for (auto& f : lp) f.setLowPass (sr, lpHz);
    }
}

void DelayModule::process (float* l, float* r, int n) noexcept
{
    const float ms = params.sync > 0 ? (float) (delaySyncBeats (params.sync) * 60000.0 / bpm) : params.timeMs;
    const float target = clampf (ms, 1.0f, 2500.0f) * 0.001f * (float) sr;
    float feedback = clampf (params.feedback, 0.0f, 0.95f);
    if (params.mode == DelayMode::slapback) feedback *= 0.3f;
    const float width = clampf (params.width, 0.0f, 1.0f);
    const float mix = clampf (params.mix, 0.0f, 1.0f);
    const bool tape = params.mode == DelayMode::tape;
    const bool ping = params.mode == DelayMode::pingPong;

    for (int i = 0; i < n; ++i)
    {
        float d = timeSm.next (target);
        if (tape)
            d += (float) (0.0012 * sr) * wow.next() + (float) (0.00015 * sr) * flutter.next();
        const float dl = l[i], dr = r[i];
        const float dryMono = 0.5f * (dl + dr);
        const float yl = lines[0].read (std::max (1.0f, d));
        const float yr = lines[1].read (std::max (1.0f, ping ? d : d * (1.0f + 0.02f * width)));
        float fl = lp[0].process (hp[0].process (yl));
        float fr = lp[1].process (hp[1].process (yr));
        if (tape)
        {
            fl = std::tanh (fl * 1.3f) / 1.3f;
            fr = std::tanh (fr * 1.3f) / 1.3f;
        }
        if (ping)
        {
            lines[0].push (dryMono + feedback * fr);
            lines[1].push (feedback * fl);
        }
        else
        {
            lines[0].push (dl + feedback * fl);
            lines[1].push (dr + feedback * fr);
        }

        const float mid = 0.5f * (fl + fr), side = 0.5f * (fl - fr) * width;
        const float env = duckEnv.process (std::abs (dryMono));
        const float duck = 1.0f / (1.0f + params.duck * 12.0f * env);
        l[i] = dl + mix * duck * (mid + side);
        r[i] = dr + mix * duck * (mid - side);
    }
}

//==============================================================================
void ReverbModule::prepare (double sampleRate)
{
    sr = sampleRate;
    predelay.prepare ((int) (0.35 * sr));
    for (auto& d : diffusers) d.prepare ((int) (0.05 * sr));
    for (auto& l : lines) l.prepare ((int) (0.25 * sr));
    mod[0].setRate (sr, 0.31);
    mod[1].setRate (sr, 0.47);
    duckEnv.setTimes (sr, 8.0f, 400.0f);
    needsConfigure = true;
    configure();
    reset();
}

void ReverbModule::reset()
{
    predelay.reset();
    for (auto& d : diffusers) d.reset();
    for (auto& l : lines) l.reset();
    dampState.fill (0.0f);
    for (auto& f : lowCut) f.reset();
    duckEnv.reset();
}

void ReverbModule::setParams (const ReverbParams& p) noexcept
{
    params = p;
    if (p.type != configured.type || p.decayS != configured.decayS || p.size != configured.size
        || p.damping != configured.damping || p.lowCutHz != configured.lowCutHz)
        needsConfigure = true;
}

void ReverbModule::configure() noexcept
{
    needsConfigure = false;
    configured = params;
    float scale = 1.0f, er = 0.0f;
    switch (params.type)
    {
        case ReverbType::plate:   scale = 0.6f; diffGain = 0.75f; er = 0.0f; break;
        case ReverbType::room:    scale = 0.35f; diffGain = 0.6f; er = 0.55f; break;
        case ReverbType::hall:    scale = 1.25f; diffGain = 0.7f; er = 0.15f; break;
        case ReverbType::chamber: scale = 0.75f; diffGain = 0.65f; er = 0.35f; break;
        case ReverbType::numTypes: break;
    }
    scale *= 0.5f + clampf (params.size, 0.0f, 1.0f);
    erGain = er;

    static const float baseMs[kLines] = { 29.7f, 37.1f, 41.1f, 43.7f, 53.0f, 59.9f, 67.7f, 73.1f };
    static const float diffMs[4] = { 4.7f, 3.6f, 12.7f, 9.3f };
    static const float erMs[6] = { 7.0f, 11.0f, 17.0f, 23.0f, 31.0f, 41.0f };
    const float decay = std::max (0.1f, params.decayS);
    for (int i = 0; i < kLines; ++i)
    {
        lineLen[(size_t) i] = std::max (8.0f, baseMs[i] * scale * 0.001f * (float) sr);
        lineGain[(size_t) i] = std::pow (10.0f, -3.0f * lineLen[(size_t) i] / (decay * (float) sr));
    }
    const float diffScale = params.type == ReverbType::plate ? 1.0f : 0.3f + 0.8f * scale;
    for (int i = 0; i < 4; ++i)
        diffLen[(size_t) i] = std::max (4, (int) (diffMs[i] * diffScale * 0.001f * (float) sr));
    for (int i = 0; i < 6; ++i)
        erTaps[(size_t) i] = std::max (2, (int) (erMs[i] * std::max (0.4f, scale) * 0.001f * (float) sr));

    dampCoeff = 0.05f + clampf (params.damping, 0.0f, 1.0f) * 0.7f;
    if (params.type == ReverbType::plate)
        dampCoeff *= 0.7f;
    modDepth = (float) (0.0008 * sr);
    for (auto& f : lowCut) f.setHighPass (sr, std::max (20.0f, params.lowCutHz));
}

void ReverbModule::process (float* l, float* r, int n) noexcept
{
    if (needsConfigure)
        configure();

    const float mix = clampf (params.mix, 0.0f, 1.0f);
    const float width = clampf (params.width, 0.0f, 1.0f);
    const float pre = std::max (1.0f, params.predelayMs * 0.001f * (float) sr);
    constexpr float norm = 0.35355339f; // 1 / sqrt (8)

    for (int i = 0; i < n; ++i)
    {
        const float dl = l[i], dr = r[i];
        const float m = 0.5f * (dl + dr);
        predelay.push (m);
        float x = predelay.read (pre);

        // Early reflections tapped from the pre-delayed signal
        float erL = 0.0f, erR = 0.0f;
        if (erGain > 0.0f)
        {
            for (int t = 0; t < 6; ++t)
            {
                const float v = predelay.read (pre + (float) erTaps[(size_t) t]) * (1.0f - 0.12f * t);
                (t % 2 == 0 ? erL : erR) += v;
            }
            erL *= erGain * 0.3f;
            erR *= erGain * 0.3f;
        }

        // Input diffusion (Schroeder allpasses)
        for (size_t k = 0; k < diffusers.size(); ++k)
        {
            const float delayed = diffusers[k].readInt (diffLen[k]);
            const float w = x + diffGain * delayed;
            diffusers[k].push (w);
            x = delayed - diffGain * w;
        }

        // FDN
        std::array<float, kLines> o {};
        const float m0 = mod[0].next() * modDepth, m1 = mod[1].next() * modDepth;
        for (int k = 0; k < kLines; ++k)
        {
            const size_t kk = (size_t) k;
            float len = lineLen[kk];
            if (k == 1) len += m0;
            if (k == 5) len += m1;
            float v = lines[kk].read (len);
            dampState[kk] = v + dampCoeff * (dampState[kk] - v);
            o[kk] = dampState[kk] * lineGain[kk];
        }

        // Fast Walsh-Hadamard transform (orthogonal feedback matrix)
        std::array<float, kLines> h = o;
        for (int len = 1; len < kLines; len <<= 1)
            for (int a = 0; a < kLines; a += len << 1)
                for (int b = a; b < a + len; ++b)
                {
                    const float u = h[(size_t) b], w = h[(size_t) (b + len)];
                    h[(size_t) b] = u + w;
                    h[(size_t) (b + len)] = u - w;
                }
        for (int k = 0; k < kLines; ++k)
            lines[(size_t) k].push (h[(size_t) k] * norm + x * ((k & 1) ? -0.4f : 0.4f));

        float wl = (o[0] + o[2] + o[4] + o[6]) * 0.6f + erL;
        float wr = (o[1] + o[3] + o[5] + o[7]) * 0.6f + erR;
        wl = lowCut[0].process (wl);
        wr = lowCut[1].process (wr);
        const float mid = 0.5f * (wl + wr), side = 0.5f * (wl - wr) * width;
        const float env = duckEnv.process (std::abs (m));
        const float duck = 1.0f / (1.0f + params.duck * 12.0f * env);
        l[i] = dl + mix * duck * (mid + side);
        r[i] = dr + mix * duck * (mid - side);
    }
}
} // namespace lavc
