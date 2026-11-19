// fss/dsp/shapers.h: memoryless waveshapers, y = f(x) (L5.2; L4.5 will reuse them inside the voice).
//
// A waveshaper bends the transfer curve: small signals pass (nearly) unchanged, big ones are squashed. On a sine
// that bend creates HARMONICS. Which ones, and how fast they fall off, is the whole character of a distortion:
//   - a SYMMETRIC curve (f(−x) = −f(x)) makes odd harmonics only (3, 5, 7...): hollow, "square-ish";
//   - an ASYMMETRIC curve adds even harmonics (2, 4...), and a DC offset: warmer, "tube-ish";
//   - a curve with a KINK (hard clip: the slope jumps from 1 to 0) has harmonics falling as 1/n², forever;
//   - a SMOOTH curve (tanh, sin) has harmonics falling faster than any power of n once they're past the drive's
//     reach, which is why oversampling helps a smooth shaper much more than a hard one (L5.2 journal).
// Every harmonic above Nyquist folds back as an inharmonic alias: dsp/halfband.h is the cure.
//
// All four are bounded for any input (no blow-ups, whatever the drive), and all map 0 to 0, so silence stays
// silence (the FX slots rely on that to sleep). [ZAV] ch. 6, [REISS-FX] ch. 7.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace fss::dsp {

/// APPEND ONLY: the index is what presets and automation store ("Drive type").
enum class ShaperType : std::uint8_t { Soft, Hard, Fold, Tube };
inline constexpr int kNumShaperTypes = 4;

/// tanh: the classic soft clipper. Slope 1 at 0 (transparent for small signals), approaches ±1.
[[nodiscard]] inline float shapeSoft(float x) noexcept { return std::tanh(x); }

/// Hard clip: EXACTLY linear below ±1, flat above. The kink at ±1 is what makes it bright (and aliasy).
[[nodiscard]] inline float shapeHard(float x) noexcept { return std::clamp(x, -1.0f, 1.0f); }

/// Sine folder: sin(x). Slope 1 at 0; past ±π/2 the wave folds back instead of flattening, and every further
/// π folds it again. The harder you push, the more folds per cycle: a West-Coast "wavefolder", FM-like spectra.
[[nodiscard]] inline float shapeFold(float x) noexcept { return std::sin(x); }

/// Asymmetric soft clip: tanh shifted along x by a bias, minus what the bias alone gives (so f(0) = 0).
/// Positive half-waves flatten at 1 − tanh(b) ≈ 0.76, negative ones at −1.24: even harmonics. Its average
/// output depends on the level (a moving DC offset), so the Drive follows it with a DC blocker.
inline constexpr float kTubeBias = 0.25f;
inline constexpr float kTubeOffset = 0.24491866240370913f; // tanh(0.25) (std::tanh isn't constexpr; tested)
[[nodiscard]] inline float shapeTube(float x) noexcept { return std::tanh(x + kTubeBias) - kTubeOffset; }

[[nodiscard]] inline float shape(ShaperType t, float x) noexcept
{
    switch (t) {
    case ShaperType::Hard: return shapeHard(x);
    case ShaperType::Fold: return shapeFold(x);
    case ShaperType::Tube: return shapeTube(x);
    default: return shapeSoft(x);
    }
}

} // namespace fss::dsp
