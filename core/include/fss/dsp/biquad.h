// fss/dsp/biquad.h: the biquad, the workhorse second-order IIR filter, with RBJ cookbook designs (L1.9).
//
// DIFFERENCE EQUATION (a0 normalized to 1):
//     y[n] = b0·x[n] + b1·x[n−1] + b2·x[n−2] − a1·y[n−1] − a2·y[n−2]
// TRANSFER FUNCTION:
//     H(z) = (b0 + b1·z⁻¹ + b2·z⁻²) / (1 + a1·z⁻¹ + a2·z⁻²)
// "Bi-quad" = a ratio of two quadratics. The numerator's roots are the ZEROS (frequencies the filter
// kills: a notch's zeros sit right on the unit circle at its centre frequency), the denominator's roots are
// the POLES (frequencies the filter rings at: the closer a pole is to the unit circle, the higher and
// narrower the resonant peak; on or outside it, the filter is unstable). Every RBJ design below just places
// two poles and two zeros. [JOS-FILTERS] ch. "Elementary filter sections", [RBJ], [EARLEVEL] biquads.
//
// Coefficient design (slow: cos, sin, pow; on parameter change) is a free function returning plain data.
// Processing (fast: 5 multiply-adds per sample) is a tiny class. Keeping them apart means the same
// coefficients can drive either structure below, and responseAt() needs no filter object at all.
//
// TWO STRUCTURES for the same equation (they're identical in exact arithmetic, not in float):
//   Direct Form I       remembers x[n−1], x[n−2], y[n−1], y[n−2] (4 states): the equation as written.
//   Transposed DF II    remembers 2 partial sums (s1, s2): fewer states, the usual float choice.
// They differ in rounding noise and, importantly for synths, in what happens when coefficients change
// while running: DF1's states are plain past signal values, still valid under new coefficients; TDF2's
// states are sums built *with the old coefficients*, so a coefficient jump injects a transient. L1.9's
// renders measure both (DF1 won narrowly, see `Biquad` below); L1.10 replaces both with the TPT SVF for
// modulated synth filters.
#pragma once

#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>

namespace fss::dsp {

enum class BiquadType : std::uint8_t { LowPass, HighPass, BandPass, Notch, Peak, LowShelf, HighShelf };

/// Normalized coefficients (a0 = 1). Designed in double; the filters convert to their own sample type.
struct BiquadCoeffs {
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
};

/// RBJ Audio EQ Cookbook [RBJ]. `q` is the quality factor (0.7071 = Butterworth for LP/HP: maximally flat).
/// gainDb is used by Peak and the shelves only. The cutoff is clamped just inside (0, fs/2).
[[nodiscard]] inline BiquadCoeffs designBiquad(BiquadType type, double sampleRate, double cutoffHz, double q,
                                               double gainDb = 0.0) noexcept
{
    const double nyquist = 0.5 * sampleRate;
    const double fc = std::fmin(std::fmax(cutoffHz, 1e-3), nyquist * 0.9999);
    const double w0 = 2.0 * std::numbers::pi * fc / sampleRate; // cutoff in radians per sample
    const double cw = std::cos(w0), sw = std::sin(w0);
    const double alpha = sw / (2.0 * std::fmax(q, 1e-3)); // bandwidth term: smaller alpha = higher Q
    const double A = std::pow(10.0, gainDb / 40.0);       // amplitude (sqrt of power gain) for peak/shelves
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;
    switch (type) {
    case BiquadType::LowPass:
        b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
        break;
    case BiquadType::HighPass:
        b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
        break;
    case BiquadType::BandPass: // constant 0 dB peak gain
        b0 = alpha; b1 = 0; b2 = -alpha; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
        break;
    case BiquadType::Notch:
        b0 = 1; b1 = -2 * cw; b2 = 1; a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha;
        break;
    case BiquadType::Peak:
        b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A; a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A;
        break;
    case BiquadType::LowShelf: {
        const double s = 2 * std::sqrt(A) * alpha;
        b0 = A * ((A + 1) - (A - 1) * cw + s);
        b1 = 2 * A * ((A - 1) - (A + 1) * cw);
        b2 = A * ((A + 1) - (A - 1) * cw - s);
        a0 = (A + 1) + (A - 1) * cw + s;
        a1 = -2 * ((A - 1) + (A + 1) * cw);
        a2 = (A + 1) + (A - 1) * cw - s;
        break;
    }
    case BiquadType::HighShelf: {
        const double s = 2 * std::sqrt(A) * alpha;
        b0 = A * ((A + 1) + (A - 1) * cw + s);
        b1 = -2 * A * ((A - 1) + (A + 1) * cw);
        b2 = A * ((A + 1) + (A - 1) * cw - s);
        a0 = (A + 1) - (A - 1) * cw + s;
        a1 = 2 * ((A - 1) - (A + 1) * cw);
        a2 = (A + 1) - (A - 1) * cw - s;
        break;
    }
    }
    return {b0 / a0, b1 / a0, b2 / a0, a1 / a0, a2 / a0};
}

/// H(e^{jω}) at `hz`: evaluate the transfer function on the unit circle (z⁻¹ = e^{−jω}).
/// |H| is the gain at that frequency, arg(H) the phase shift. This is the *computed* response that the
/// tests compare against a *measured* one (DFT of the impulse response).
[[nodiscard]] inline std::complex<double> responseAt(const BiquadCoeffs& c, double hz, double sampleRate) noexcept
{
    const std::complex<double> z1 = std::polar(1.0, -2.0 * std::numbers::pi * hz / sampleRate);
    const std::complex<double> z2 = z1 * z1;
    return (c.b0 + c.b1 * z1 + c.b2 * z2) / (1.0 + c.a1 * z1 + c.a2 * z2);
}

/// Pole radius: the larger |root| of z² + a1·z + a2. < 1 means stable; the closer to 1, the longer it rings.
[[nodiscard]] inline double poleRadius(const BiquadCoeffs& c) noexcept
{
    const double disc = c.a1 * c.a1 - 4.0 * c.a2;
    if (disc < 0.0)
        return std::sqrt(c.a2); // complex-conjugate pair: |p|² = product of the roots = a2
    const double r = std::sqrt(disc);
    return std::fmax(std::abs((-c.a1 + r) * 0.5), std::abs((-c.a1 - r) * 0.5));
}

/// Transposed Direct Form II. T = float (the default) or double (reference / low-cutoff precision).
template <typename T>
class BiquadTdf2 {
public:
    void setCoeffs(const BiquadCoeffs& c) noexcept
    {
        b0_ = static_cast<T>(c.b0); b1_ = static_cast<T>(c.b1); b2_ = static_cast<T>(c.b2);
        a1_ = static_cast<T>(c.a1); a2_ = static_cast<T>(c.a2);
    }
    void reset() noexcept { s1_ = s2_ = T(0); }

    [[nodiscard]] T tick(T x) noexcept
    {
        const T y = b0_ * x + s1_;
        s1_ = b1_ * x - a1_ * y + s2_; // the partial sums carry the "past" forward
        s2_ = b2_ * x - a2_ * y;
        return y;
    }

private:
    T b0_ = T(1), b1_ = T(0), b2_ = T(0), a1_ = T(0), a2_ = T(0);
    T s1_ = T(0), s2_ = T(0);
};

/// Direct Form I: the difference equation exactly as written, with 4 states.
template <typename T>
class BiquadDf1 {
public:
    void setCoeffs(const BiquadCoeffs& c) noexcept
    {
        b0_ = static_cast<T>(c.b0); b1_ = static_cast<T>(c.b1); b2_ = static_cast<T>(c.b2);
        a1_ = static_cast<T>(c.a1); a2_ = static_cast<T>(c.a2);
    }
    void reset() noexcept { x1_ = x2_ = y1_ = y2_ = T(0); }

    [[nodiscard]] T tick(T x) noexcept
    {
        const T y = b0_ * x + b1_ * x1_ + b2_ * x2_ - a1_ * y1_ - a2_ * y2_;
        x2_ = x1_; x1_ = x;
        y2_ = y1_; y1_ = y;
        return y;
    }

private:
    T b0_ = T(1), b1_ = T(0), b2_ = T(0), a1_ = T(0), a2_ = T(0);
    T x1_ = T(0), x2_ = T(0), y1_ = T(0), y2_ = T(0);
};

/// CFS's default biquad: Direct Form I in float. Measured in L1.9 (journal): its rounding noise is ~2-4 dB
/// lower than TDF2's at every cutoff tried, and its states stay meaningful when coefficients change. The
/// cost is two extra floats of state. At very low cutoffs the bigger error is the float *coefficients*
/// (−55 dB at 20 Hz), which no choice of structure fixes: use BiquadDf1<double> there.
using Biquad = BiquadDf1<float>;

} // namespace fss::dsp
