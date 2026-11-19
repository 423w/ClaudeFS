// fss/dsp/op_waves.h: the waveforms an FM operator can have (L4.2).
//
// THE SINE VARIANTS (TX81Z / OPL style). Yamaha's later 4-operator synths (TX81Z, DX11) and the OPL chips
// (AdLib, Sound Blaster) got many extra timbres from a few cheap tricks on the one sine table they had: play only
// part of it, fold the negative half up, or run through it twice as fast. One cycle, phase t in [0, 1):
//   Half sine     sin(2πt) for t < ½, else 0         a fundamental at ½ + even harmonics, 2/(π(4k²−1))
//   Abs sine      |sin(2πt)|                          only even harmonics: it sounds an octave up
//   Quarter sine  |sin(2πt)| for t in [0,¼) ∪ [½,¾)   two "humps" that jump back to 0: harmonics at 1/n, bright
//   Alt sine      sin(4πt) for t < ½, else 0          a whole sine squeezed into half a cycle: nasal
//   Camel         |sin(4πt)| for t < ½, else 0        two humps then silence: thin, bright
// (The names describe the shapes; this is not a copy of any chip's exact table set.)
//
// ZERO MEAN. All but the alt sine have a DC component (their average isn't 0). A modulator doesn't care (in PM a
// constant is just a fixed phase shift), but a carrier would put DC into the output: a thump at every note start
// and end, and lost headroom. So each variant has its exact mean subtracted: 1/π, 2/π, 1/π, 0, 1/π.
//
// THE VA SHAPES. Saw, square and triangle are L1.8's PolyBLEP/PolyBLAMP waveforms, so a carrier can be a
// subtractive oscillator. PolyBLEP needs the phase step per sample (dt) to place its correction; inside an
// operator, phase modulation changes the step every sample, so the operator passes the INSTANTANEOUS step (this
// sample's total phase minus the last one's). Between 0 and ½ cycle the correction is right; outside (deep
// modulation running the phase backwards or faster than Nyquist) dt is passed as 0 and the wave is naive.
//
// The sine variants are NOT band-limited (neither were the chips'): quarter sine and camel jump, half/abs/alt
// have corners. As modulators that barely matters (the carrier's sidebands dominate); as bright carriers on high
// notes they alias like naive waveforms. Measured in the L4.2 journal.
#pragma once

#include <fss/dsp/phasor.h>
#include <fss/dsp/sine.h>
#include <fss/dsp/waveforms.h>

#include <cmath>
#include <cstdint>
#include <numbers>

namespace fss::dsp {

/// APPEND ONLY: the index is what presets and automation store (an operator's "Wave").
enum class OpWave : std::uint8_t { Sine, HalfSine, AbsSine, QuarterSine, AltSine, Camel, Saw, Square, Triangle };
inline constexpr int kNumOpWaves = 9;

namespace detail {
inline constexpr float kInvPi = std::numbers::inv_pi_v<float>;
inline constexpr Phase kHalfCycle = 0x80000000u;
} // namespace detail

/// One sample of waveform w at phase p (the operator's total phase: its own plus the modulation).
/// dt = the phase step since the last sample in cycles, in [0, ½] (0 = unknown: no band-limiting).
[[nodiscard]] inline float opWave(OpWave w, Phase p, double dt) noexcept
{
    using detail::kHalfCycle;
    using detail::kInvPi;
    switch (w) {
    case OpWave::Sine: return sineTable(p);
    case OpWave::HalfSine: return (p < kHalfCycle ? sineTable(p) : 0.0f) - kInvPi;
    case OpWave::AbsSine: return std::abs(sineTable(p)) - 2.0f * kInvPi;
    case OpWave::QuarterSine: return ((p >> 30) % 2 == 0 ? std::abs(sineTable(p)) : 0.0f) - kInvPi; // quarters 0 and 2
    case OpWave::AltSine: return p < kHalfCycle ? sineTable(p << 1) : 0.0f; // twice the speed (the shift wraps)
    case OpWave::Camel: return (p < kHalfCycle ? std::abs(sineTable(p << 1)) : 0.0f) - kInvPi;
    case OpWave::Saw: return blepSaw(phaseToCycles(p), dt);
    case OpWave::Square: return blepSquare(phaseToCycles(p), dt);
    case OpWave::Triangle: return blepTriangle(phaseToCycles(p), dt);
    }
    return sineTable(p);
}

} // namespace fss::dsp
