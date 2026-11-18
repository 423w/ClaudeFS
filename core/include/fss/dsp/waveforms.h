// fss/dsp/waveforms.h: saw, square and triangle, naive and band-limited (L1.8).
//
// The Fourier series (one cycle, phase t in [0, 1)):
//   saw      2t − 1          = −(2/π) Σ_{h≥1}      sin(2πht) / h      every harmonic, at 1/h
//   square   ±1              =  (4/π) Σ_{h odd}    sin(2πht) / h      odd harmonics, at 1/h
//   triangle 1 − 2|2t − 1|   = −(8/π²) Σ_{h odd}   cos(2πht) / h²     odd harmonics, at 1/h²
// Those sums never end. A sampled signal can only hold frequencies below Nyquist (fs/2), so when we
// *sample* the ideal waveform (the "naive" versions below), every harmonic above Nyquist folds back to
// |h·f0 − m·fs| for some integer m: an *alias*. Aliases are not multiples of f0, so they sound inharmonic,
// and when the pitch rises they move *down*. That's the metallic, "digital" fizz of a cheap saw.
//
// The harmonics come from the sharp features of the waveform:
//   - a JUMP (saw at the wrap, square at both edges) makes harmonics fall at 1/h (−6 dB/octave);
//   - a CORNER, a sudden change of slope (triangle), makes them fall at 1/h² (−12 dB/octave).
// So the cure is local: fix the waveform only near its jumps and corners.
//
// PolyBLEP ([VALIMAKI07], [BLEP-SNDKIT]). A band-limited step ("BLEP") is what a jump looks like after
// a perfect low-pass filter: a smooth S-curve with ripples. Its difference from the naive step (the
// *residual*) is nonzero only near the jump, and it is what we add to the naive waveform. The exact residual
// is infinitely long; PolyBLEP replaces it with a 2-sample polynomial: integrate a triangle-shaped
// "impulse" 2 samples wide (instead of the ideal sinc) and subtract the naive step. That gives, at distance x
// samples from a jump of height 1:
//       residual(x) = +(x + 1)² / 2      for x in (−1, 0]   (just before the jump)
//       residual(x) = −(1 − x)² / 2      for x in [0, 1)    (just after it)
//       residual(x) = 0                  otherwise
// It's not perfect (a triangle kernel is a poor low-pass, see the journal), but it costs a compare and a few
// multiplies per sample and removes most of the alias energy.
//
// PolyBLAMP ([ESQUEDA16]). A corner is a jump in the *slope*, i.e. the integral of a jump. So its residual
// is the integral of the BLEP residual (a "BLAMP": band-limited ramp). Integrate residual(u) from −1 to x:
//       x in (−1, 0]:  ∫_{−1}^{x} (u+1)²/2 du = (x + 1)³ / 6
//       x in [0, 1):   1/6 + ∫_0^x −(1−u)²/2 du = 1/6 + [(1−u)³/6]_0^x = (1 − x)³ / 6
//       otherwise 0   (and it returns to exactly 0 at x = 1: the residual's integral over (−1, 1) is 1/6 − 1/6 = 0)
// It's continuous at the corner (both pieces give 1/6) and its slope jumps there by −1, cancelling the
// naive waveform's slope jump. For a slope change of `ds` (output units per SAMPLE) at the corner, the
// correction to add is ds·D(x) with D(x) above. (Checked numerically in tests/test_waveforms.cpp.)
//
// The "t < dt / t > 1 − dt" tests below find the samples within 1 sample of a jump at phase 0. With
// phase t in cycles and dt = f0/fs (cycles per sample), the distance from the jump in samples is t/dt just
// after the wrap and (t − 1)/dt just before it. Valid for 0 ≤ dt ≤ 0.5 (0 Hz up to Nyquist).
#pragma once

#include "fss/dsp/phasor.h"
#include "fss/dsp/sine.h"

#include <cmath>

namespace fss::dsp {

enum class Waveform { Sine, Saw, Square, Triangle };

// ---- naive: sample the ideal waveform. Phase t in cycles, [0, 1). ------------------------------------

/// −1 → +1 ramp, jump back down at the wrap.
[[nodiscard]] inline float naiveSaw(double t) noexcept { return static_cast<float>(2.0 * t - 1.0); }

/// +1 for the first half cycle, −1 for the second. Jumps up at t = 0, down at t = 0.5.
[[nodiscard]] inline float naiveSquare(double t) noexcept { return t < 0.5 ? 1.0f : -1.0f; }

/// −1 at t = 0, +1 at t = 0.5: the integral of naiveSquare (scaled), so square and triangle line up.
/// Corners at t = 0 (slope −4 → +4 per cycle) and t = 0.5 (+4 → −4).
[[nodiscard]] inline float naiveTriangle(double t) noexcept
{
    return static_cast<float>(1.0 - 2.0 * std::abs(2.0 * t - 1.0));
}

// ---- the polynomial residuals -------------------------------------------------------------------

/// PolyBLEP residual for a jump of height **2** at phase 0 (i.e. 2·residual(x) from the header comment),
/// because the saw and square jump by 2. t = phase in cycles [0, 1), dt = phase increment in cycles/sample.
/// This is the classic form from [VALIMAKI07]/[BLEP-SNDKIT], so it can be compared line by line.
[[nodiscard]] inline double polyBlep(double t, double dt) noexcept
{
    if (t < dt) {                 // the first sample after the jump: x = t/dt in [0, 1)
        const double x = t / dt;
        return x + x - x * x - 1.0; // = −(1 − x)²
    }
    if (t > 1.0 - dt) {           // the last sample before the jump: x = (t − 1)/dt in (−1, 0)
        const double x = (t - 1.0) / dt;
        return x * x + x + x + 1.0; // = (x + 1)²
    }
    return 0.0;                   // every other sample: the naive waveform is already right
}

/// PolyBLAMP residual D(x) for a slope change of **1 per sample** at phase 0 (see header comment).
/// Multiply by the slope change in output units per sample. Same t/dt convention as polyBlep.
[[nodiscard]] inline double polyBlamp(double t, double dt) noexcept
{
    if (t < dt) {                 // just after the corner: x in [0, 1)
        const double u = 1.0 - t / dt;
        return u * u * u * (1.0 / 6.0);
    }
    if (t > 1.0 - dt) {           // just before it: x in (−1, 0)
        const double u = (t - 1.0) / dt + 1.0;
        return u * u * u * (1.0 / 6.0);
    }
    return 0.0;
}

// ---- band-limited (PolyBLEP / PolyBLAMP) -----------------------------------------------------------

namespace detail {
/// t + ½ cycle, wrapped. Exact in double: t comes from a 32-bit phase, so adding 0.5 loses no bits.
[[nodiscard]] inline double halfCycleLater(double t) noexcept { return t < 0.5 ? t + 0.5 : t - 0.5; }
} // namespace detail

/// Saw: the naive ramp jumps by −2 at the wrap, so subtract the height-2 residual.
[[nodiscard]] inline float blepSaw(double t, double dt) noexcept
{
    return static_cast<float>(2.0 * t - 1.0 - polyBlep(t, dt));
}

/// Square: +2 jump at t = 0, −2 jump at t = 0.5. Evaluating polyBlep at t + ½ moves its "jump at phase 0"
/// to t = 0.5, so one function serves both edges.
[[nodiscard]] inline float blepSquare(double t, double dt) noexcept
{
    const double naive = t < 0.5 ? 1.0 : -1.0;
    return static_cast<float>(naive + polyBlep(t, dt) - polyBlep(detail::halfCycleLater(t), dt));
}

/// Triangle: slope +4 cycles⁻¹ on the way up, −4 on the way down. At t = 0 the slope changes by +8 per
/// cycle = +8·dt per sample; at t = 0.5 by −8·dt. Add ds·D(x) at each corner.
[[nodiscard]] inline float blepTriangle(double t, double dt) noexcept
{
    const double naive = 1.0 - 2.0 * std::abs(2.0 * t - 1.0);
    return static_cast<float>(naive + 8.0 * dt * (polyBlamp(t, dt) - polyBlamp(detail::halfCycleLater(t), dt)));
}

// ---- oscillator -----------------------------------------------------------------------------------

/// A Phasor plus the band-limited waveforms. Real-time safe (RT-RULES.md): no allocation, no locks, no
/// I/O, no loops, noexcept throughout. Just a few compares and multiplies per sample.
///
/// Two ways to pick the waveform, measured against each other in `fss_render l1.8`:
///   tick<Waveform::Saw>()   compile-time: the compiler generates one function per waveform, no switch;
///   tick(Waveform::Saw)     runtime: a switch per sample. What a synth with a waveform *parameter* needs.
class BlepOscillator {
public:
    void prepare(double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        phasor_.prepare(sampleRate);
        setFrequency(frequencyHz_);
    }

    void reset(Phase start = 0) noexcept { phasor_.reset(start); }

    /// Frequency in Hz, clamped to [0, fs/2]. PolyBLEP's "within one sample of the jump" logic assumes the
    /// phase moves forward by at most half a cycle per sample (0 ≤ dt ≤ 0.5). Negative frequencies would
    /// need the jump direction flipped, and a waveform parameter has no use for them, so they become 0.
    void setFrequency(double hz) noexcept
    {
        if (!(hz > 0.0)) // also catches NaN, which compares false with everything
            hz = 0.0;
        if (hz > 0.5 * sampleRate_)
            hz = 0.5 * sampleRate_;
        frequencyHz_ = hz;
        phasor_.setFrequency(hz);
        dt_ = phaseToCycles(phasor_.increment()); // the *quantized* increment, so dt matches the real phase step
    }

    /// One sample of waveform W. `if constexpr` keeps only W's branch in each instantiation.
    template <Waveform W>
    [[nodiscard]] float tick() noexcept
    {
        const Phase p = phasor_.tick();
        if constexpr (W == Waveform::Sine)
            return sineTable(p); // already band-limited: a single harmonic
        else {
            const double t = phaseToCycles(p);
            if constexpr (W == Waveform::Saw)
                return blepSaw(t, dt_);
            else if constexpr (W == Waveform::Square)
                return blepSquare(t, dt_);
            else
                return blepTriangle(t, dt_);
        }
    }

    /// One sample of waveform `w`, chosen at run time.
    [[nodiscard]] float tick(Waveform w) noexcept
    {
        switch (w) {
        case Waveform::Sine: return tick<Waveform::Sine>();
        case Waveform::Saw: return tick<Waveform::Saw>();
        case Waveform::Square: return tick<Waveform::Square>();
        case Waveform::Triangle: return tick<Waveform::Triangle>();
        }
        return 0.0f; // unreachable for valid enum values; keeps every path returning
    }

    [[nodiscard]] double phaseIncrementCycles() const noexcept { return dt_; }
    [[nodiscard]] Phase phase() const noexcept { return phasor_.phase(); }

private:
    Phasor phasor_;
    double sampleRate_ = 48000.0; // overwritten by prepare()
    double frequencyHz_ = 0.0;
    double dt_ = 0.0;             // phase increment in cycles per sample = f0/fs
};

} // namespace fss::dsp
