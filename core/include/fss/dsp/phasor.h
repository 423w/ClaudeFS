// fss/dsp/phasor.h: the phase accumulator every CFS oscillator is built on (L1.2).
//
// A phasor is a number that ramps from 0 to 1 (one cycle) and wraps, advancing by f/fs each sample.
// Frequency is *how fast phase moves*; phase is *where in the cycle you are*. Because the oscillator
// only ever adds an increment, changing the frequency changes the slope of the ramp but never makes the
// phase jump: no clicks, and glides come for free.
//
// PHASE UNITS (D5, decided in L1.2): an unsigned 32-bit fraction of a cycle.
//     0 = 0°,  2^30 = 90°,  2^31 = 180°,  2^32 ≡ 0 (wraps back to 0 by itself)
// Why fixed point instead of a double in [0,1):
//   - Wrapping is free and exact. Unsigned overflow is *defined* in C++ (arithmetic modulo 2^32), so
//     `phase += inc` needs no `if (phase >= 1)` and no floor(). (Signed overflow would be undefined
//     behaviour; that's why this is uint32_t and not int32_t.)
//   - Every sample's phase is exact: phase(n) = start + n·inc mod 2^32, bit-for-bit, on any machine, for
//     any note length. Golden-file and block-size-invariance tests (L6.1) depend on that determinism.
//   - The top bits index a sine table directly, with no float→int conversion (see sine.h).
//   - 32-bit lanes vectorize twice as wide as double (SIMD voices, L6.2).
// The cost: frequency is quantized to fs/2^32 ≈ 0.000011 Hz at 48 kHz, i.e. 0.00004 cents at A4.
// That's inaudible, and it's *constant* (no drift that grows with time). The L1.2 journal measures float,
// double and uint32 accumulators side by side. Same approach as the DX7 ([SHIRRIFF-DX7]).
#pragma once

#include <cmath>
#include <cstdint>

namespace fss::dsp {

/// Phase in cycles × 2^32 (see the header comment). Arithmetic on it wraps modulo one cycle.
using Phase = std::uint32_t;

inline constexpr double kPhaseOneCycle = 4294967296.0; // 2^32: one full cycle in Phase units

/// Phase → cycles in [0, 1).
[[nodiscard]] constexpr double phaseToCycles(Phase p) noexcept
{
    return static_cast<double>(p) / kPhaseOneCycle;
}

/// Cycles (any real number) → Phase, wrapped into one cycle and rounded to the nearest step.
/// 1.25 → a quarter cycle; −0.25 → three quarters. Non-finite input gives 0.
[[nodiscard]] inline Phase phaseFromCycles(double cycles) noexcept
{
    if (!std::isfinite(cycles))
        return 0;
    const double frac = cycles - std::floor(cycles); // [0, 1)
    // Round via uint64 so that frac·2^32 rounding up to exactly 2^32 wraps to 0 instead of overflowing.
    return static_cast<Phase>(static_cast<std::uint64_t>(frac * kPhaseOneCycle + 0.5));
}

class Phasor {
public:
    /// Rate-dependent setup. Call before setFrequency(). (No allocation: just one constant.)
    void prepare(double sampleRate) noexcept
    {
        stepsPerHz_ = kPhaseOneCycle / sampleRate;
        setFrequency(frequencyRequestedHz_);
    }

    /// Jump to a phase (default: the start of the cycle). Used on note-on for key-synced oscillators.
    void reset(Phase start = 0) noexcept { phase_ = start; }

    /// Set the frequency. Takes effect from the next tick(); the current phase is untouched (that's
    /// what makes glides and FM click-free). Negative frequencies run the phase backwards, and
    /// frequencies outside ±fs/2 alias exactly as sampling theory says they should (fs + 100 Hz ≡ 100 Hz).
    void setFrequency(double hz) noexcept
    {
        frequencyRequestedHz_ = hz;
        double steps = hz * stepsPerHz_;
        // llround is only defined for values that fit in long long; clamp absurd input, and map NaN to 0
        // (in a real-time path we can't throw, and a NaN frequency must not become undefined behaviour).
        if (!(std::abs(steps) < 4.0e18))
            steps = std::isnan(steps) ? 0.0 : std::copysign(4.0e18, steps);
        // Round half away from zero (what std::llround does) with a cast: llround measured 13 ns per call
        // in L2.6, the cast ~1 ns, and vibrato/glide call this 4× per voice per sample. For |steps| < 2^52
        // adding ±0.5 is exact, so the result equals llround's.
        // int64 → uint32 keeps the low 32 bits (conversion to unsigned is defined as modulo 2^32), which
        // is exactly "wrap the increment into one cycle". −1 step becomes 0xFFFFFFFF, i.e. go backwards.
        const auto rounded = static_cast<std::int64_t>(steps >= 0.0 ? steps + 0.5 : steps - 0.5);
        inc_ = static_cast<Phase>(rounded);
    }

    /// Return the current phase, then advance by one sample.
    [[nodiscard]] Phase tick() noexcept
    {
        const Phase p = phase_;
        phase_ += inc_; // wraps modulo 2^32 by definition
        return p;
    }

    [[nodiscard]] Phase phase() const noexcept { return phase_; }
    [[nodiscard]] Phase increment() const noexcept { return inc_; }

    /// The frequency actually produced after quantization and aliasing, in (−fs/2, fs/2].
    [[nodiscard]] double frequencyHz() const noexcept
    {
        // Read the increment as signed: steps above 2^31 mean "less than half a cycle backwards".
        return static_cast<double>(static_cast<std::int32_t>(inc_)) / stepsPerHz_;
    }

private:
    Phase phase_ = 0;
    Phase inc_ = 0;
    double stepsPerHz_ = kPhaseOneCycle / 48000.0; // overwritten by prepare(); a sane default until then
    double frequencyRequestedHz_ = 0.0;            // kept so prepare() can recompute inc_ at a new rate
};

} // namespace fss::dsp
