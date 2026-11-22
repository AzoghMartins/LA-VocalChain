#pragma once
#include <algorithm>
#include <cmath>

namespace lavc
{
constexpr double kPi = 3.14159265358979323846;

/** Transposed direct form II biquad with RBJ cookbook designs. */
class Biquad
{
public:
    void reset() noexcept { z1 = z2 = 0.0; }

    void setHighPass (double sr, double f, double q = 0.70710678) noexcept
    {
        const double w0 = omega (sr, f), cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        set ((1.0 + cw) * 0.5, -(1.0 + cw), (1.0 + cw) * 0.5, 1.0 + alpha, -2.0 * cw, 1.0 - alpha);
    }

    void setLowPass (double sr, double f, double q = 0.70710678) noexcept
    {
        const double w0 = omega (sr, f), cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        set ((1.0 - cw) * 0.5, 1.0 - cw, (1.0 - cw) * 0.5, 1.0 + alpha, -2.0 * cw, 1.0 - alpha);
    }

    /** Constant 0 dB peak gain band-pass. */
    void setBandPass (double sr, double f, double q) noexcept
    {
        const double w0 = omega (sr, f), cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        set (alpha, 0.0, -alpha, 1.0 + alpha, -2.0 * cw, 1.0 - alpha);
    }

    void setPeak (double sr, double f, double q, double gainDb) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = omega (sr, f), cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        set (1.0 + alpha * A, -2.0 * cw, 1.0 - alpha * A, 1.0 + alpha / A, -2.0 * cw, 1.0 - alpha / A);
    }

    void setLowShelf (double sr, double f, double gainDb, double slope = 1.0) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = omega (sr, f), cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / 2.0 * std::sqrt ((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
        const double t = 2.0 * std::sqrt (A) * alpha;
        set (A * ((A + 1) - (A - 1) * cw + t), 2 * A * ((A - 1) - (A + 1) * cw), A * ((A + 1) - (A - 1) * cw - t),
             (A + 1) + (A - 1) * cw + t, -2 * ((A - 1) + (A + 1) * cw), (A + 1) + (A - 1) * cw - t);
    }

    void setHighShelf (double sr, double f, double gainDb, double slope = 1.0) noexcept
    {
        const double A = std::pow (10.0, gainDb / 40.0);
        const double w0 = omega (sr, f), cw = std::cos (w0), sw = std::sin (w0);
        const double alpha = sw / 2.0 * std::sqrt ((A + 1.0 / A) * (1.0 / slope - 1.0) + 2.0);
        const double t = 2.0 * std::sqrt (A) * alpha;
        set (A * ((A + 1) + (A - 1) * cw + t), -2 * A * ((A - 1) + (A + 1) * cw), A * ((A + 1) + (A - 1) * cw - t),
             (A + 1) - (A - 1) * cw + t, 2 * ((A - 1) - (A + 1) * cw), (A + 1) - (A - 1) * cw - t);
    }

    void setAllPass (double sr, double f, double q = 0.70710678) noexcept
    {
        const double w0 = omega (sr, f), cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        set (1.0 - alpha, -2.0 * cw, 1.0 + alpha, 1.0 + alpha, -2.0 * cw, 1.0 - alpha);
    }

    void setBypass() noexcept { b0 = 1.0; b1 = b2 = a1 = a2 = 0.0; }

    inline float process (float x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }

    /** Magnitude response in dB at frequency f (for displays). */
    double magnitudeDb (double sr, double f) const noexcept
    {
        const double w = 2.0 * kPi * f / sr;
        const double cr = std::cos (w), ci = -std::sin (w), c2r = std::cos (2 * w), c2i = -std::sin (2 * w);
        const double nr = b0 + b1 * cr + b2 * c2r, ni = b1 * ci + b2 * c2i;
        const double dr = 1.0 + a1 * cr + a2 * c2r, di = a1 * ci + a2 * c2i;
        const double mag2 = (nr * nr + ni * ni) / std::max (1.0e-20, dr * dr + di * di);
        return 10.0 * std::log10 (std::max (1.0e-20, mag2));
    }

private:
    static double omega (double sr, double f) noexcept
    {
        f = f < 10.0 ? 10.0 : (f > sr * 0.49 ? sr * 0.49 : f);
        return 2.0 * kPi * f / sr;
    }

    void set (double nb0, double nb1, double nb2, double na0, double na1, double na2) noexcept
    {
        b0 = nb0 / na0; b1 = nb1 / na0; b2 = nb2 / na0; a1 = na1 / na0; a2 = na2 / na0;
    }

    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;
};

/** 4th order Butterworth (two cascaded biquads). */
class Filter24
{
public:
    void reset() noexcept { s1.reset(); s2.reset(); }
    void setHighPass (double sr, double f) noexcept { s1.setHighPass (sr, f, 0.54119610); s2.setHighPass (sr, f, 1.30656296); }
    void setLowPass (double sr, double f) noexcept { s1.setLowPass (sr, f, 0.54119610); s2.setLowPass (sr, f, 1.30656296); }
    void setBypass() noexcept { s1.setBypass(); s2.setBypass(); }
    inline float process (float x) noexcept { return s2.process (s1.process (x)); }

private:
    Biquad s1, s2;
};

/** Linkwitz-Riley 4th order crossover: low + high sum to an allpass response. */
class LR4Crossover
{
public:
    void setFrequency (double sr, double f) noexcept
    {
        for (auto* b : { &lp1, &lp2 }) b->setLowPass (sr, f, 0.70710678);
        for (auto* b : { &hp1, &hp2 }) b->setHighPass (sr, f, 0.70710678);
    }
    void reset() noexcept { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); }
    inline void process (float x, float& low, float& high) noexcept
    {
        low = lp2.process (lp1.process (x));
        high = hp2.process (hp1.process (x));
    }

private:
    Biquad lp1, lp2, hp1, hp2;
};
} // namespace lavc
