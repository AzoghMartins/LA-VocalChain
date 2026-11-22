// Headless tests for the LA-VocalChain DSP engine (no JUCE needed).
#include "dsp/VocalEngine.h"

#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <vector>

using namespace lavc;

namespace
{
int failures = 0;
void check (bool ok, const std::string& what)
{
    std::printf ("%s %s\n", ok ? "  ok  " : "  FAIL", what.c_str());
    if (! ok)
        ++failures;
}

constexpr double sr = 48000.0;
float midiToHz (float m) { return 440.0f * std::pow (2.0f, (m - 69.0f) / 12.0f); }

/** Voice-like tone: harmonics with a formant-ish envelope; pitch given per sample in MIDI. */
std::vector<float> voice (const std::function<float (double)>& midiAt, double seconds, float amp = 0.3f)
{
    std::vector<float> v ((size_t) (seconds * sr));
    double phase = 0.0;
    for (size_t i = 0; i < v.size(); ++i)
    {
        const double t = (double) i / sr;
        const float m = midiAt (t);
        if (m <= 0.0f) { v[i] = 0.0f; continue; }
        phase += midiToHz (m) / sr;
        float s = 0.0f;
        for (int h = 1; h <= 12; ++h)
        {
            const float f = midiToHz (m) * (float) h;
            const float env = 1.0f / (1.0f + std::pow ((f - 700.0f) / 500.0f, 2.0f)) + 0.6f / (1.0f + std::pow ((f - 2500.0f) / 800.0f, 2.0f));
            s += env * (float) std::sin (2.0 * kPi * h * phase) / (float) h;
        }
        v[i] = amp * s;
    }
    return v;
}

/** Runs a mono signal through a PitchEngine only (used as a pitch "meter"). */
float measurePitch (const std::vector<float>& x, size_t from, size_t to)
{
    PitchEngine e;
    e.prepare (sr, 512, false);
    PitchParams p;
    e.setParams (p);
    std::vector<float> seg (x.begin() + (long) from, x.begin() + (long) to), l (seg.size()), hl (seg.size()), hr (seg.size());
    std::vector<uint8_t> v (seg.size());
    e.process (seg.data(), l.data(), hl.data(), hr.data(), v.data(), (int) seg.size(), nullptr);
    return e.getLastInputMidi();
}

struct Out { std::vector<float> l, r; };

Out runEngine (VocalEngine& e, const std::vector<float>& inL, const std::vector<float>* inR = nullptr, int block = 256)
{
    Out o { inL, inR ? *inR : inL };
    for (size_t s = 0; s < inL.size(); s += (size_t) block)
    {
        const int n = (int) std::min<size_t> ((size_t) block, inL.size() - s);
        float* ptrs[2] = { o.l.data() + s, o.r.data() + s };
        e.process (ptrs, 2, n, nullptr);
    }
    return o;
}

EngineParams neutral()
{
    EngineParams p;
    p.lowCutHz = 20.0f;
    p.deess.enabled = false;
    p.eq.enabled = false;
    p.comp.enabled = false;
    p.limiterEnabled = false;
    return p;
}

double rms (const std::vector<float>& x, size_t from, size_t to)
{
    double acc = 0.0;
    for (size_t i = from; i < to && i < x.size(); ++i) acc += (double) x[i] * x[i];
    return std::sqrt (acc / (double) std::max<size_t> (1, to - from));
}

float dB (double v) { return 20.0f * (float) std::log10 (v + 1.0e-12); }
} // namespace

int main()
{
    std::printf ("LA-VocalChain engine tests\n");

    // ---- Pitch detection -------------------------------------------------------------------------
    {
        bool ok = true;
        for (float m : { 43.0f, 50.3f, 57.0f, 64.6f, 72.0f, 79.0f })
        {
            const auto x = voice ([m] (double) { return m; }, 1.0);
            const float got = measurePitch (x, 0, x.size());
            ok &= std::abs (got - m) < 0.08f;
            if (std::abs (got - m) >= 0.08f) std::printf ("        detected %.3f for %.3f\n", got, m);
        }
        check (ok, "pitch detection within 8 cents from G2 to G5");
    }

    // ---- Pitch correction ---------------------------------------------------------------------------
    {
        VocalEngine e;
        auto p = neutral();
        p.pitch.tuneEnabled = true;
        p.pitch.speedMs = 0.0f;
        p.pitch.toleranceCents = 0.0f;
        p.pitch.key = 0;
        p.pitch.scale = Scale::major; // C major: 63.4 (Eb+) should go to 64 (E)
        e.prepare (sr, 512, false);
        e.setParams (p);
        e.reset();
        const auto x = voice ([] (double) { return 63.6f; }, 1.5);
        const auto out = runEngine (e, x);
        const float got = measurePitch (out.l, 24000, out.l.size());
        check (std::abs (got - 64.0f) < 0.1f, "hard tune moves 63.6 to the C-major note E4 (" + std::to_string (got) + ")");

        p.pitch.amount = 0.5f;
        e.setParams (p);
        e.reset();
        const auto half = runEngine (e, x);
        const float gotHalf = measurePitch (half.l, 24000, half.l.size());
        check (std::abs (gotHalf - 63.8f) < 0.1f, "50 % amount corrects half way (" + std::to_string (gotHalf) + ")");

        p.pitch.amount = 1.0f;
        p.pitch.transpose = 5.0f;
        p.pitch.scale = Scale::chromatic;
        e.setParams (p);
        e.reset();
        const auto up = runEngine (e, x);
        const float gotUp = measurePitch (up.l, 24000, up.l.size());
        check (std::abs (gotUp - 69.0f) < 0.12f, "transpose +5 st on top of correction (" + std::to_string (gotUp) + ")");
    }

    // ---- Retune speed keeps slow vibrato when slow, flattens it when hard -------------------------
    {
        auto vib = [] (double t) { return 60.0f + 0.4f * (float) std::sin (2.0 * kPi * 5.0 * t); };
        const auto x = voice (vib, 2.0);
        auto spread = [&] (float speed)
        {
            VocalEngine e;
            auto p = neutral();
            p.pitch.tuneEnabled = true;
            p.pitch.speedMs = speed;
            p.pitch.toleranceCents = 0.0f;
            e.prepare (sr, 512, false);
            e.setParams (p);
            e.reset();
            const auto o = runEngine (e, x);
            float lo = 1000.0f, hi = -1000.0f;
            for (size_t s = 30000; s + 4800 < o.l.size(); s += 1200)
            {
                const float m = measurePitch (o.l, s, s + 4800);
                lo = std::min (lo, m);
                hi = std::max (hi, m);
            }
            return hi - lo;
        };
        const float hard = spread (0.0f), soft = spread (200.0f);
        check (hard < 0.25f && soft > hard + 0.2f, "hard tune flattens vibrato, slow speed keeps it (" + std::to_string (hard) + " vs " + std::to_string (soft) + ")");
    }

    // ---- Harmonies ----------------------------------------------------------------------------------
    {
        VocalEngine e;
        auto p = neutral();
        p.pitch.harmEnabled = true;
        p.pitch.harmHumanize = 0.0f;
        p.pitch.key = 0;
        p.pitch.scale = Scale::major;
        p.pitch.voices[0] = { true, 2, 0.0f, -1.0f, 0.0f }; // a third above, hard left
        p.mix = 1.0f;
        e.prepare (sr, 512, false);
        e.setParams (p);
        e.reset();
        const auto x = voice ([] (double) { return 64.0f; }, 1.5); // E4 -> G4 (diatonic third)
        const auto out = runEngine (e, x);
        // Right channel = lead only, left = lead + harmony. Subtract to isolate the harmony
        std::vector<float> harm (out.l.size());
        for (size_t i = 0; i < harm.size(); ++i) harm[i] = out.l[i] - out.r[i];
        const float got = measurePitch (harm, 24000, harm.size());
        check (std::abs (got - 67.0f) < 0.15f, "scale harmony: a third above E4 in C major is G4 (" + std::to_string (got) + ")");
        check (dB (rms (harm, 24000, harm.size())) > dB (rms (x, 24000, x.size())) - 10.0f, "harmony voice is audible");

        p.pitch.harmMode = 2; // MIDI chord
        e.setParams (p);
        e.reset();
        e.getPitchEngine().noteOn (59); // B3
        const auto outM = runEngine (e, x);
        for (size_t i = 0; i < harm.size(); ++i) harm[i] = outM.l[i] - outM.r[i];
        const float gotM = measurePitch (harm, 24000, harm.size());
        check (std::abs (gotM - 59.0f) < 0.15f, "MIDI chord harmony sings the held note B3 (" + std::to_string (gotM) + ")");
    }

    // ---- Formant shift keeps pitch --------------------------------------------------------------------
    {
        VocalEngine e;
        auto p = neutral();
        p.pitch.tuneEnabled = true;
        p.pitch.amount = 0.0f;
        p.pitch.formant = 4.0f;
        e.prepare (sr, 512, false);
        e.setParams (p);
        e.reset();
        const auto x = voice ([] (double) { return 57.0f; }, 1.5);
        const auto out = runEngine (e, x);
        const float got = measurePitch (out.l, 24000, out.l.size());
        check (std::abs (got - 57.0f) < 0.1f, "formant shift does not change the pitch (" + std::to_string (got) + ")");
    }

    // ---- Key detection ------------------------------------------------------------------------------
    {
        PitchEngine pe;
        pe.prepare (sr, 512, false);
        PitchParams pp;
        pe.setParams (pp);
        pe.setKeyLearning (true);
        // A natural minor melody emphasising A, C, E
        const float melody[] = { 69, 72, 76, 74, 72, 71, 69, 64, 65, 67, 69, 72, 71, 69, 76, 69 };
        auto mAt = [&] (double t) { const int i = (int) (t / 0.35); return melody[i % 16]; };
        auto x = voice (mAt, 0.35 * 16 * 2);
        std::vector<float> l (x.size()), hl (x.size()), hr (x.size());
        std::vector<uint8_t> v (x.size());
        pe.process (x.data(), l.data(), hl.data(), hr.data(), v.data(), (int) x.size(), nullptr);
        const auto k = pe.estimateKey();
        check (k.key == 9 && k.minor, std::string ("key detection finds A minor (got ") + keyName (k.key) + (k.minor ? " minor)" : " major)"));
    }

    // ---- Latency & transparency ------------------------------------------------------------------------
    for (bool tracking : { false, true })
    {
        VocalEngine e;
        auto p = neutral();
        e.prepare (sr, 512, tracking);
        e.setParams (p);
        e.reset();
        std::vector<float> x ((size_t) sr, 0.0f);
        x[1000] = 1.0f;
        const auto out = runEngine (e, x);
        size_t peak = 0;
        for (size_t i = 0; i < out.l.size(); ++i)
            if (std::abs (out.l[i]) > std::abs (out.l[peak])) peak = i;
        const int lat = e.getLatency();
        check ((int) peak - 1000 == lat && std::abs (out.l[peak] - 1.0f) < 0.02f,
               std::string (tracking ? "tracking" : "mixing") + " mode: neutral chain is a pure delay of the reported latency ("
                   + std::to_string (lat) + " samples = " + std::to_string (1000.0 * lat / sr).substr (0, 5) + " ms)");
    }
    {
        VocalEngine a, b;
        a.prepare (sr, 512, false);
        b.prepare (sr, 512, true);
        check (b.getLatency() < a.getLatency() / 3, "tracking mode has much lower latency");
    }

    // ---- Stereo image kept when Tune is off ---------------------------------------------------------
    {
        VocalEngine e;
        auto p = neutral();
        e.prepare (sr, 512, false);
        e.setParams (p);
        e.reset();
        std::vector<float> l ((size_t) sr / 2, 0.0f), r ((size_t) sr / 2, 0.0f);
        l[100] = 1.0f;
        const auto out = runEngine (e, l, &r);
        const int lat = e.getLatency();
        check (std::abs (out.l[(size_t) (100 + lat)] - 1.0f) < 0.02f && std::abs (out.r[(size_t) (100 + lat)]) < 0.02f,
               "stereo input passes with its image when Tune is off");
    }

    // ---- Compressor -------------------------------------------------------------------------------------
    {
        VocalEngine e;
        auto p = neutral();
        p.comp.enabled = true;
        p.comp.mode = CompMode::clean;
        p.comp.thresholdDb = -20.0f;
        p.comp.ratio = 4.0f;
        p.comp.makeupDb = 0.0f;
        e.prepare (sr, 512, false);
        e.setParams (p);
        e.reset();
        std::vector<float> x ((size_t) sr);
        for (size_t i = 0; i < x.size(); ++i) x[i] = 0.5f * (float) std::sin (2.0 * kPi * 1000.0 * i / sr); // -6 dBFS peak
        const auto out = runEngine (e, x);
        const float inDb = dB (rms (x, 20000, 40000)), outDb = dB (rms (out.l, 30000, 46000));
        const float gr = inDb - outDb; // expect (20 - 6) * 0.75 = 10.5 dB
        check (gr > 8.5f && gr < 12.5f, "clean compressor 4:1 at -20 dB reduces a -6 dBFS tone by ~10.5 dB (" + std::to_string (gr) + ")");

        bool allModes = true;
        for (int m = 0; m < (int) CompMode::numModes; ++m)
        {
            p.comp.mode = (CompMode) m;
            e.setParams (p);
            e.reset();
            const auto o = runEngine (e, x);
            const float g = inDb - dB (rms (o.l, 30000, 46000));
            allModes &= g > 3.0f && g < 20.0f;
            if (! (g > 3.0f && g < 20.0f)) std::printf ("        mode %d: %.2f dB\n", m, g);
        }
        check (allModes, "all five compressor characters compress");
    }

    // ---- De-esser ---------------------------------------------------------------------------------------
    {
        VocalEngine e;
        auto p = neutral();
        p.deess.enabled = true;
        p.deess.thresholdDb = -40.0f;
        p.deess.rangeDb = 12.0f;
        e.prepare (sr, 512, false);
        e.setParams (p);
        e.reset();
        std::mt19937 rng (3);
        std::normal_distribution<float> n (0.0f, 1.0f);
        Biquad hp;
        hp.setHighPass (sr, 6000.0);
        std::vector<float> x ((size_t) sr / 2);
        for (auto& v : x) v = 0.2f * hp.process (n (rng));
        const auto out = runEngine (e, x);
        const float red = dB (rms (x, 8000, 16000)) - dB (rms (out.l, 8000 + (size_t) e.getLatency(), 16000 + (size_t) e.getLatency()));
        check (red > 6.0f, "de-esser reduces a loud 'sss' by more than 6 dB (" + std::to_string (red) + ")");

        std::vector<float> low ((size_t) sr / 2);
        for (size_t i = 0; i < low.size(); ++i) low[i] = 0.3f * (float) std::sin (2.0 * kPi * 300.0 * i / sr);
        const auto outLow = runEngine (e, low);
        const float lowRed = dB (rms (low, 8000, 16000)) - dB (rms (outLow.l, 8000 + (size_t) e.getLatency(), 16000 + (size_t) e.getLatency()));
        check (std::abs (lowRed) < 0.5f, "de-esser leaves a 300 Hz tone alone");
    }

    // ---- Gate and breath control --------------------------------------------------------------------
    {
        VocalEngine e;
        auto p = neutral();
        p.gate.enabled = true;
        p.gate.thresholdDb = -40.0f;
        p.gate.rangeDb = -40.0f;
        e.prepare (sr, 512, false);
        e.setParams (p);
        e.reset();
        std::mt19937 rng (5);
        std::uniform_real_distribution<float> u (-1.0f, 1.0f);
        auto x = voice ([] (double t) { return t < 0.5 ? 60.0f : 0.0f; }, 1.2);
        for (auto& v : x) v += 0.002f * u (rng); // -60 dB noise floor
        const auto out = runEngine (e, x);
        const int lat = e.getLatency();
        const float noiseIn = dB (rms (x, 40000, 52000)), noiseOut = dB (rms (out.l, (size_t) (40000 + lat), (size_t) (52000 + lat)));
        const float voiceDiff = dB (rms (x, 6000, 20000)) - dB (rms (out.l, (size_t) (6000 + lat), (size_t) (20000 + lat)));
        std::printf ("        noise %.1f -> %.1f, voice diff %.2f\n", noiseIn, noiseOut, voiceDiff);
        check (noiseIn - noiseOut > 30.0f && std::abs (voiceDiff) < 0.5f, "gate removes the noise floor between phrases, keeps the voice");
    }
    {
        VocalEngine e;
        auto p = neutral();
        p.gate.breathDb = -20.0f;
        p.gate.breathSens = 60.0f;
        e.prepare (sr, 512, false);
        e.setParams (p);
        e.reset();
        // phrase, then a soft noisy breath, then a phrase
        auto x = voice ([] (double t) { return (t < 0.6 || t > 1.2) ? 62.0f : 0.0f; }, 1.8);
        std::mt19937 rng (9);
        std::normal_distribution<float> n (0.0f, 1.0f);
        Biquad bp;
        bp.setBandPass (sr, 1500.0, 0.7);
        for (size_t i = (size_t) (0.7 * sr); i < (size_t) (1.1 * sr); ++i) x[i] += 0.02f * bp.process (n (rng));
        const auto out = runEngine (e, x);
        const int lat = e.getLatency();
        const float red = dB (rms (x, (size_t) (0.8 * sr), (size_t) (1.05 * sr)))
                          - dB (rms (out.l, (size_t) (0.8 * sr) + (size_t) lat, (size_t) (1.05 * sr) + (size_t) lat));
        const float phrase = dB (rms (x, 4000, 24000)) - dB (rms (out.l, (size_t) (4000 + lat), (size_t) (24000 + lat)));
        check (red > 10.0f && std::abs (phrase) < 0.5f, "breath control lowers a breath by " + std::to_string (red) + " dB, phrases untouched");
    }

    // ---- Auto level ---------------------------------------------------------------------------------------
    {
        VocalEngine e;
        auto p = neutral();
        p.level.enabled = true;
        p.level.targetDb = -20.0f;
        p.level.rangeDb = 12.0f;
        p.level.speedMs = 100.0f;
        e.prepare (sr, 512, false);
        e.setParams (p);
        e.reset();
        auto quiet = voice ([] (double) { return 60.0f; }, 2.0, 0.03f);
        auto loud = voice ([] (double) { return 60.0f; }, 2.0, 0.3f);
        const auto oq = runEngine (e, quiet);
        e.reset();
        const auto ol = runEngine (e, loud);
        const float before = dB (rms (loud, 60000, 90000)) - dB (rms (quiet, 60000, 90000));
        const float after = dB (rms (ol.l, 60000, 90000)) - dB (rms (oq.l, 60000, 90000));
        check (before > 19.0f && after < 8.0f, "auto level evens out a 20 dB level difference (" + std::to_string (after) + " dB left)");
    }

    // ---- Sculpt pulls the tone towards the target -----------------------------------------------------
    {
        VocalEngine e;
        auto p = neutral();
        p.sculpt.enabled = true;
        p.sculpt.amount = 1.0f;
        p.sculpt.speedMs = 100.0f;
        p.sculpt.style = 0;
        e.prepare (sr, 512, false);
        e.setParams (p);
        e.reset();
        // very bright input: white noise
        std::mt19937 rng (11);
        std::normal_distribution<float> n (0.0f, 0.1f);
        std::vector<float> x ((size_t) sr * 2);
        for (auto& v : x) v = n (rng);
        runEngine (e, x);
        const auto& m = e.getMeters();
        check (m.sculpt[7] < -4.0f && m.sculpt[1] > 2.0f, "sculpt darkens a too-bright source (10 kHz " + std::to_string (m.sculpt[7]) + " dB)");
    }

    // ---- Limiter ------------------------------------------------------------------------------------------
    {
        VocalEngine e;
        auto p = neutral();
        p.limiterEnabled = true;
        p.ceilingDb = -3.0f;
        p.inputGainDb = 12.0f;
        e.prepare (sr, 512, false);
        e.setParams (p);
        e.reset();
        const auto x = voice ([] (double) { return 55.0f; }, 1.0, 0.5f);
        const auto out = runEngine (e, x);
        float peak = 0.0f;
        for (float v : out.l) peak = std::max (peak, std::abs (v));
        check (peak <= dbToGain (-3.0f) + 1.0e-4f, "limiter holds the -3 dB ceiling with +12 dB of drive");
    }

    // ---- Module order matters and parses -----------------------------------------------------------------
    {
        auto o = orderFromString ("6543210");
        check (o[0] == 6 && o[6] == 0, "module order string round trip");
        check (orderFromString ("0012345") == defaultModuleOrder(), "invalid order strings fall back to the default");

        auto render = [] (const ModuleOrder& order)
        {
            VocalEngine e;
            auto p = neutral();
            p.order = order;
            p.sat.enabled = true;
            p.sat.driveDb = 18.0f;
            p.comp.enabled = true;
            p.comp.thresholdDb = -30.0f;
            p.comp.ratio = 10.0f;
            e.prepare (sr, 512, false);
            e.setParams (p);
            e.reset();
            return runEngine (e, voice ([] (double) { return 57.0f; }, 0.5, 0.5f)).l;
        };
        auto a = render (defaultModuleOrder());
        ModuleOrder swapped = defaultModuleOrder();
        std::swap (swapped[4], swapped[6]); // comp <-> sat
        auto b = render (swapped);
        double diff = 0.0;
        for (size_t i = 0; i < a.size(); ++i) diff += std::abs (a[i] - b[i]);
        check (diff / (double) a.size() > 1.0e-3, "changing the module order changes the sound");
    }

    // ---- Effects produce tails --------------------------------------------------------------------------
    {
        VocalEngine e;
        auto p = neutral();
        p.delay.enabled = true;
        p.delay.timeMs = 250.0f;
        p.delay.sync = 0;
        p.delay.duck = 0.0f;
        p.delay.mix = 0.5f;
        p.reverb.enabled = true;
        p.reverb.mix = 0.3f;
        p.reverb.duck = 0.0f;
        e.prepare (sr, 512, false);
        e.setParams (p);
        e.reset();
        std::vector<float> x ((size_t) sr * 2, 0.0f);
        for (size_t i = 0; i < 2400; ++i) x[i] = 0.5f * (float) std::sin (2.0 * kPi * 440.0 * i / sr);
        const auto out = runEngine (e, x);
        const int lat = e.getLatency();
        const size_t echo = (size_t) lat + (size_t) (0.25 * sr);
        check (dB (rms (out.l, echo, echo + 2400)) > dB (rms (x, 0, 2400)) - 12.0f, "delay echo arrives after 250 ms");
        check (dB (rms (out.l, (size_t) (1.2 * sr), (size_t) (1.5 * sr))) > -80.0f, "reverb tail rings out");
    }

    // ---- Stability: everything on, random settings ----------------------------------------------------------
    {
        std::mt19937 rng (1234);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        bool finite = true;
        float maxPeak = 0.0f;
        for (int trial = 0; trial < 12; ++trial)
        {
            EngineParams p;
            p.pitch.tuneEnabled = true;
            p.pitch.speedMs = u (rng) * 200.0f;
            p.pitch.transpose = std::round (u (rng) * 24.0f - 12.0f);
            p.pitch.formant = u (rng) * 24.0f - 12.0f;
            p.pitch.scale = (Scale) (int) (u (rng) * 11.99f);
            p.pitch.harmEnabled = true;
            for (auto& v : p.pitch.voices) v = { true, (int) (u (rng) * 28.0f) - 14, -6.0f, u (rng) * 2.0f - 1.0f, u (rng) * 24.0f - 12.0f };
            p.pitch.harmMode = (int) (u (rng) * 1.99f);
            p.gate = { true, -60.0f, -40.0f, 1.0f, 50.0f, 100.0f, -15.0f, 50.0f };
            p.level.enabled = true;
            p.deess.enabled = true;
            p.sculpt = { true, (int) (u (rng) * 8.99f), u (rng), 200.0f, u (rng) * 12.0f - 6.0f };
            for (auto& g : p.eq.gain) g = u (rng) * 36.0f - 18.0f;
            p.comp = { true, (CompMode) (int) (u (rng) * 4.99f), -30.0f, 6.0f, 2.0f, 100.0f, 6.0f, 1.0f };
            p.sat = { true, (SatMode) (int) (u (rng) * 4.99f), u (rng) * 24.0f, u (rng) * 2.0f - 1.0f, 1.0f };
            p.doubler.enabled = p.mod.enabled = p.delay.enabled = p.reverb.enabled = true;
            p.mod.mode = (ModMode) (int) (u (rng) * 2.99f);
            p.mod.feedback = 0.9f;
            p.delay.mode = (DelayMode) (int) (u (rng) * 3.99f);
            p.delay.feedback = 0.95f;
            p.reverb.type = (ReverbType) (int) (u (rng) * 3.99f);
            p.reverb.decayS = 10.0f;
            p.width = 2.0f;
            ModuleOrder o = defaultModuleOrder();
            std::shuffle (o.begin(), o.end(), rng);
            p.order = o;

            VocalEngine e;
            e.prepare (trial % 2 ? 44100.0 : 96000.0, 333, trial % 3 == 0);
            e.setParams (p);
            e.reset();
            auto x = voice ([trial] (double t) { return 45.0f + (float) trial * 2.0f + 5.0f * (float) std::sin (t * 3.0); }, 2.0, 0.6f);
            std::mt19937 nr ((unsigned) trial);
            std::normal_distribution<float> nn (0.0f, 0.05f);
            for (size_t i = x.size() / 2; i < x.size() / 2 + 8000; ++i) x[i] = nn (nr);
            const auto out = runEngine (e, x, nullptr, 333);
            for (size_t i = 0; i < out.l.size(); ++i)
            {
                finite &= std::isfinite (out.l[i]) && std::isfinite (out.r[i]);
                maxPeak = std::max ({ maxPeak, std::abs (out.l[i]), std::abs (out.r[i]) });
            }
        }
        check (finite && maxPeak <= 1.0f, "12 random full-chain settings stay finite and under the limiter ceiling");
    }

    std::printf (failures == 0 ? "All engine tests passed\n" : "%d engine test(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
