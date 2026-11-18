// fss/util/math.h: the "just-enough math" of L0.2, as small helpers.
//
// Pitch and loudness are both *exponential* in the physical quantity (Hz, amplitude) and *linear* in how
// we hear them (semitones, decibels). These helpers convert between the two worlds:
//
//   pitch:    f = 440 Hz · 2^((note − 69) / 12)     MIDI note 69 = A4 = 440 Hz; +12 notes = ×2 = one octave
//   cents:    ratio = 2^(cents / 1200)              100 cents = 1 semitone, 1200 cents = 1 octave
//   loudness: dB = 20 · log10(gain)                 ×2 ≈ +6.02 dB, ×0.5 ≈ −6.02 dB, ×0.001 = −60 dB
//
// Units are in the names (CLAUDE.md): `note` is a MIDI note number (fractional allowed: 60.5 is a quarter
// tone above middle C), `hz` is frequency in Hz, `db` is decibels relative to gain 1.0 (= 0 dBFS when the
// signal is a sample value), `gain` is a linear amplitude factor.
//
// Why `inline`, not `constexpr`: in C++20 the <cmath> functions (exp2, log2, pow, log10) are not
// constexpr, so a constexpr wrapper could never actually run at compile time. Later standards are making
// <cmath> constexpr; when CFS can use that, these become `constexpr` with no other change.
//
// Real-time safe: no allocation, no locks, no exceptions. They do call exp2/log2/pow, which cost tens
// of cycles each. Fine per note or per block. If one ever shows up per sample in a profile (L2.6),
// that's the moment to reach for a table or a polynomial approximation, not before.
//
// Resources: [DSPGUIDE] ch. 1–3 (sampling), [PUCKETTE] ch. 1 (sinusoids, pitch and dB as used in music).
#pragma once

#include <algorithm>
#include <cmath>
#include <numbers>

namespace fss::util {

// ---- constants ---------------------------------------------------------------------------------

inline constexpr float kPi    = std::numbers::pi_v<float>;
inline constexpr float kTwoPi = 2.0f * std::numbers::pi_v<float>;

inline constexpr float kA4Hz   = 440.0f; // concert pitch
inline constexpr float kA4Note = 69.0f;  // MIDI note number of A4

// The quietest level gainToDb() will report. Real silence (gain 0) has no finite dB value
// (log10(0) = −∞), and a −∞ that leaks into later math turns into NaN. −144 dB is about the noise
// floor of 24-bit audio (6.02 dB per bit × 24), i.e. "silent by any practical measure".
inline constexpr float kSilenceDb = -144.0f;

// ---- pitch -------------------------------------------------------------------------------------

/// MIDI note number (fractional allowed) → frequency in Hz. midiToHz(69) == 440 exactly.
[[nodiscard]] inline float midiToHz(float note) noexcept
{
    // exp2(x) is 2^x. (note − 69)/12 counts octaves away from A4; each octave doubles the frequency.
    return kA4Hz * std::exp2((note - kA4Note) / 12.0f);
}

/// Frequency in Hz (must be > 0) → MIDI note number, fractional. Inverse of midiToHz.
[[nodiscard]] inline float hzToMidi(float hz) noexcept
{
    // log2(hz / 440) counts octaves above A4 (negative below it); ×12 turns octaves into semitones.
    return kA4Note + 12.0f * std::log2(hz / kA4Hz);
}

/// Interval in semitones → frequency ratio. +12 → 2.0, −12 → 0.5, +7 → ≈1.4983 (an equal-tempered fifth).
[[nodiscard]] inline float semitonesToRatio(float semitones) noexcept
{
    return std::exp2(semitones / 12.0f);
}

/// Interval in cents → frequency ratio. 1200 cents = one octave = ×2. Used for detune.
[[nodiscard]] inline float centsToRatio(float cents) noexcept
{
    return std::exp2(cents / 1200.0f);
}

/// Frequency ratio (must be > 0) → interval in cents. Inverse of centsToRatio.
[[nodiscard]] inline float ratioToCents(float ratio) noexcept
{
    return 1200.0f * std::log2(ratio);
}

// ---- loudness ----------------------------------------------------------------------------------

/// Decibels → linear gain. 0 dB → 1, −6.0206 dB → 0.5, −20 dB → 0.1, −60 dB → 0.001.
[[nodiscard]] inline float dbToGain(float db) noexcept
{
    // 20 (not 10) because dB measures *power*, and power goes as amplitude squared:
    // 10·log10(gain²) = 20·log10(gain). So gain = 10^(dB/20).
    return std::pow(10.0f, db / 20.0f);
}

/// Linear gain → decibels, never below kSilenceDb.
/// Uses |gain|: a gain of −0.5 is "half as loud, polarity flipped", which is −6.02 dB, not undefined.
[[nodiscard]] inline float gainToDb(float gain) noexcept
{
    // Clamp *before* the log so gain 0 (and denormal-tiny gains) map to the floor instead of −∞.
    // 10^(−144/20) ≈ 6.3e-8, a normal float, so the log stays finite and fast.
    static constexpr float kMinGain = 6.30957344e-8f; // == dbToGain(kSilenceDb)
    return 20.0f * std::log10(std::max(std::abs(gain), kMinGain));
}

// ---- fast exponentials for per-sample paths (L2.6) ------------------------------------------------
// Measured in L2.6 (MSVC 19.44, Release, AMD Ryzen 7 7735HS): std::exp2 costs ~35 ns per call, std::exp
// ~3.7 ns (double) / ~2.4 ns (float). Mathematically 2^x = e^(x·ln 2); the two differ only in the last bit
// or so (rounding of x·ln 2), far below anything audible (1e-15 relative ≈ 2e-12 cents). Use these where an
// exponential runs per sample per voice (pitch modulation, cutoff); keep std::exp2 where it runs once per note
// (bit-exact continuity with earlier renders).
[[nodiscard]] inline double pow2(double x) noexcept { return std::exp(x * std::numbers::ln2); }
[[nodiscard]] inline float pow2(float x) noexcept { return std::exp(x * std::numbers::ln2_v<float>); }

} // namespace fss::util
