// fss/dsp/svf.h: the TPT (topology-preserving transform) state-variable filter, CFS's synth filter (L1.10).
//
// WHY NOT THE BIQUAD? A biquad has the right *frequency response*, but its states (past samples or partial
// sums) are not the states of any circuit; under fast cutoff modulation it becomes a time-varying system
// with no physical meaning, and it can misbehave (L1.9, and this lesson's torture test). The TPT SVF instead
// discretizes the analog state-variable *circuit* itself: two integrators in a feedback loop. Its two states
// are the integrators' contents (in a real circuit: the charge on two capacitors), so when the cutoff moves,
// the stored energy stays put and only how fast it flows changes. That's why it can be modulated hard,
// even at audio rate. [ZAV] ch. 3–4, [SIMPER-SVF].
//
// THE ANALOG PROTOTYPE (s = jω/ωc, normalized to the cutoff; k = 1/Q is the damping):
//     LP = 1/(s² + k·s + 1)      BP = s/(s² + k·s + 1)      HP = s²/(s² + k·s + 1)
// all three at once, from one structure: HP → integrate → BP → integrate → LP, with BP·k and LP fed back.
//
// THE DISCRETIZATION. Each integrator is replaced by the trapezoidal rule (= bilinear transform), and the
// cutoff is PREWARPED, g = tan(π·fc/fs), so the digital cutoff lands exactly on fc despite the bilinear
// transform squashing the whole analog frequency axis into 0..fs/2. Trapezoidal integrators need this
// sample's input to produce this sample's output, and the loop feeds outputs back to inputs: a "delay-free
// loop". Instead of breaking it with a fake one-sample delay (which detunes and destabilizes the filter),
// the loop is SOLVED algebraically: that's the "zero-delay feedback" (ZDF) in the name. Solving it gives
// Simper's equations below (a1..a3 are the solved loop, ic1eq/ic2eq are the integrators' states written
// as "equivalent currents" of a trapezoidal capacitor model).
//
// In exact arithmetic the LP/BP/HP responses equal the RBJ biquad's (same prototype, same transform,
// same prewarping): tests/test_svf.cpp checks that. The difference is entirely in behaviour under modulation.
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <numbers>

namespace fss::dsp {

enum class SvfMode : std::uint8_t { LowPass, BandPass, HighPass, Notch };

class Svf {
public:
    struct Out {
        float low, band, high; // band is the raw integrator output: peak gain Q (1/k) at the cutoff
    };

    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        piOverFs_ = static_cast<float>(std::numbers::pi / sampleRate);
        maxCutoffHz_ = static_cast<float>(0.49 * sampleRate);
        set(cutoffHz_, q_);
        reset();
    }

    void reset() noexcept { ic1eq_ = ic2eq_ = 0.0f; }

    /// cutoffHz is clamped to [1 Hz, 0.49·fs] (tan() runs off to infinity at fs/2); q to ≥ 0.05.
    /// Cheap enough to call every sample: one tan and one divide.
    void set(float cutoffHz, float q) noexcept
    {
        cutoffHz_ = std::clamp(cutoffHz, 1.0f, maxCutoffHz_);
        q_ = std::max(q, 0.05f);
        const float g = std::tan(piOverFs_ * cutoffHz_); // prewarped integrator gain
        k_ = 1.0f / q_;                                   // damping: 2 = two real poles, → 0 = self-oscillation
        a1_ = 1.0f / (1.0f + g * (g + k_));               // the solved delay-free loop
        a2_ = g * a1_;
        a3_ = g * a2_;
    }

    /// Take another filter's settings (coefficients) but keep this one's state (L4.4: the voice's right-channel
    /// filter follows the left one without paying for a second tan()).
    void setFrom(const Svf& o) noexcept
    {
        cutoffHz_ = o.cutoffHz_;
        q_ = o.q_;
        k_ = o.k_;
        a1_ = o.a1_;
        a2_ = o.a2_;
        a3_ = o.a3_;
    }

    /// One sample; all three outputs at once (they cost the same as one).
    [[nodiscard]] Out tick(float x) noexcept
    {
        const float v3 = x - ic2eq_;
        const float v1 = a1_ * ic1eq_ + a2_ * v3; // band-pass (first integrator's output)
        const float v2 = ic2eq_ + a2_ * ic1eq_ + a3_ * v3; // low-pass (second integrator's output)
        ic1eq_ = 2.0f * v1 - ic1eq_; // trapezoidal state update: the "capacitor" now holds this
        ic2eq_ = 2.0f * v2 - ic2eq_;
        return {v2, v1, x - k_ * v1 - v2};
    }

    /// One sample of one response. BandPass is normalized to 0 dB at the cutoff (k·band), like RBJ's BPF.
    [[nodiscard]] float tick(float x, SvfMode mode) noexcept
    {
        const Out o = tick(x);
        switch (mode) {
        case SvfMode::LowPass: return o.low;
        case SvfMode::BandPass: return k_ * o.band;
        case SvfMode::HighPass: return o.high;
        case SvfMode::Notch: return o.low + o.high; // = x − k·band: the input minus its resonant band
        }
        return o.low;
    }

    [[nodiscard]] float cutoffHz() const noexcept { return cutoffHz_; }
    [[nodiscard]] float q() const noexcept { return q_; }

    /// Computed response of the *digital* filter at `hz` for the current settings: the analog prototype
    /// evaluated at the bilinear-mapped frequency s = (1/g)·(1 − z⁻¹)/(1 + z⁻¹).
    [[nodiscard]] std::complex<double> responseAt(double hz, SvfMode mode) const noexcept
    {
        const double g = std::tan(std::numbers::pi * static_cast<double>(cutoffHz_) / sampleRate_);
        const double k = 1.0 / static_cast<double>(q_);
        const std::complex<double> zInv = std::polar(1.0, -2.0 * std::numbers::pi * hz / sampleRate_);
        const std::complex<double> s = (1.0 - zInv) / (g * (1.0 + zInv));
        const std::complex<double> den = s * s + k * s + 1.0;
        switch (mode) {
        case SvfMode::LowPass: return 1.0 / den;
        case SvfMode::BandPass: return k * s / den;
        case SvfMode::HighPass: return s * s / den;
        case SvfMode::Notch: return (s * s + 1.0) / den;
        }
        return 1.0 / den;
    }

private:
    double sampleRate_ = 48000.0;
    float piOverFs_ = static_cast<float>(std::numbers::pi / 48000.0);
    float maxCutoffHz_ = 0.49f * 48000.0f;
    float cutoffHz_ = 1000.0f, q_ = 0.7071f;
    float k_ = 1.4142f, a1_ = 0.0f, a2_ = 0.0f, a3_ = 0.0f;
    float ic1eq_ = 0.0f, ic2eq_ = 0.0f;
};

} // namespace fss::dsp
