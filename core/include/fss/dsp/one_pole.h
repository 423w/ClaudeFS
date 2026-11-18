// fss/dsp/one_pole.h: the one-pole filter, the simplest IIR filter there is (L1.9).
//
// One line of state, one multiply-add:
//
//     lp[n] = lp[n−1] + a·(x[n] − lp[n−1])          ("move a fraction a of the way toward the input")
//     hp[n] = x[n] − lp[n]                           (whatever the low-pass took out)
//
// It's everywhere in a synth: parameter smoothing (smoother.h is this filter aimed at a target), envelope
// segments (adsr.h), DC blocking (a high-pass at ~10 Hz), gentle tone controls, and the four stages of a
// Moog ladder. [JOS-FILTERS] "simplest lowpass", [EARLEVEL] one-pole.
//
// THE COEFFICIENT. a = 1 − e^(−2π·fc/fs), not the "obvious" 2π·fc/fs. An analog RC low-pass, fed nothing,
// decays as e^(−t/RC) with RC = 1/(2π·fc). Sampled every 1/fs seconds, that is a decay by the factor
// p = e^(−2π·fc/fs) per sample, and p is exactly the filter's POLE (the state is multiplied by p = 1 − a each
// sample). So 1 − e^(...) makes the digital filter's impulse response the analog one, sampled ("impulse
// invariance"). 2π·fc/fs is only the first term of its Taylor series: close at low fc, wrong higher up, and
// above fc = fs/π it makes a > 2, the pole leaves the unit circle, and the filter explodes (L1.9 journal).
//
// TRANSFER FUNCTIONS (z⁻¹ = "one sample ago"):
//     H_lp(z) = a / (1 − (1−a)·z⁻¹)        one pole at z = 1−a, no zeros (well: a zero at z = 0)
//     H_hp(z) = 1 − H_lp(z)
// responseAt() evaluates them on the unit circle, z = e^{jω}, ω = 2π·f/fs.
#pragma once

#include <cmath>
#include <complex>
#include <numbers>

namespace fss::dsp {

/// a = 1 − e^(−2π·fc/fs), clamped to (0, 1]: a cutoff at or above Nyquist just passes the input through.
[[nodiscard]] inline double onePoleCoefficient(double cutoffHz, double sampleRate) noexcept
{
    const double a = 1.0 - std::exp(-2.0 * std::numbers::pi * cutoffHz / sampleRate);
    return a < 0.0 ? 0.0 : (a > 1.0 ? 1.0 : a);
}

class OnePole {
public:
    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        setCutoff(cutoffHz_);
    }

    void setCutoff(double cutoffHz) noexcept
    {
        cutoffHz_ = cutoffHz;
        a_ = static_cast<float>(onePoleCoefficient(cutoffHz, sampleRate_));
    }

    void reset(float state = 0.0f) noexcept { lp_ = state; }

    /// One sample through the low-pass.
    [[nodiscard]] float tickLowPass(float x) noexcept
    {
        lp_ += a_ * (x - lp_);
        return lp_;
    }

    /// One sample through the high-pass (the input minus its low-passed part). At 5–20 Hz: a DC blocker.
    [[nodiscard]] float tickHighPass(float x) noexcept { return x - tickLowPass(x); }

    [[nodiscard]] float coefficient() const noexcept { return a_; }

    /// Computed frequency response at `hz`: complex gain (magnitude and phase) of the low- or high-pass.
    [[nodiscard]] std::complex<double> responseAt(double hz, bool highPass = false) const noexcept
    {
        const double a = static_cast<double>(a_);
        const std::complex<double> zInv = std::polar(1.0, -2.0 * std::numbers::pi * hz / sampleRate_); // z⁻¹ = e^{−jω}
        const std::complex<double> lp = a / (1.0 - (1.0 - a) * zInv);
        return highPass ? 1.0 - lp : lp;
    }

private:
    double sampleRate_ = 48000.0;
    double cutoffHz_ = 1000.0;
    float a_ = 0.0f;
    float lp_ = 0.0f;
};

} // namespace fss::dsp
