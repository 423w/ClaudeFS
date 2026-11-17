// fss/synth/operator.h: an FM operator = phasor + sine + envelope + level (L1.4).
//
// This is *phase* modulation (PM), like the DX7 and every "FM" synth since: the modulator's output is
// added to the carrier's PHASE, not its frequency.
//
//   y[n] = level · env[n] · sin( 2π·phase[n] + pm[n] )        pm[n] in RADIANS
//
// UNITS (the classic FM bug source, so stated once, here):
//   - tick(pmRadians): the phase offset in radians. 2π = one full cycle.
//   - level: unitless. For a CARRIER (its output goes to the speakers) it's the output amplitude.
//     For a MODULATOR (its output feeds another operator's tick()) the same number is the peak
//     MODULATION INDEX in radians, I in Chowning's sin(ωc·t + I·sin(ωm·t)). So "modulator level 2" means
//     "index 2", and an envelope on a modulator is an envelope on brightness.
//
// FM vs PM: true FM adds the modulator to the *frequency*, so the phase gets the modulator's integral.
// For a pure sine modulator that's the same spectrum (cos instead of sin, and I = Δf/fm). They differ
// when the modulator has a DC component or is itself modulated: in FM a DC offset changes the
// carrier's pitch (it accumulates forever); in PM it's only a constant phase shift. PM keeps tuning
// stable under feedback and stacked modulation, which is why CFS (like the DX7) uses it.
#pragma once

#include <fss/dsp/adsr.h>
#include <fss/dsp/phasor.h>
#include <fss/dsp/smoother.h>
#include <fss/dsp/sine.h>

#include <cstdint>
#include <numbers>

namespace fss::synth {

/// Radians → Phase units, wrapping any size of offset into one cycle. 2^32 / 2π steps per radian.
/// Through int64 first: casting a float bigger than ±2^31 straight to an integer type would be undefined.
/// Any |radians| below ~1e9 is safe; a modulation index anywhere near that would be all noise anyway.
[[nodiscard]] inline dsp::Phase radiansToPhase(float radians) noexcept
{
    // The multiply is in double: in float, radians·6.8e8 keeps only 24 bits, so at index 10 the phase
    // would be quantized to 512 steps (≈ −122 dB of phase noise). Double keeps it exact for free.
    constexpr double kStepsPerRadian = dsp::kPhaseOneCycle / (2.0 * std::numbers::pi);
    return static_cast<dsp::Phase>(static_cast<std::int64_t>(static_cast<double>(radians) * kStepsPerRadian));
}

class Operator {
public:
    /// levelSmoothingMs: time constant for live level changes (L2.4); 0 = jump (A/B comparisons only).
    void prepare(double sampleRate, float levelSmoothingMs = 5.0f) noexcept
    {
        phasor_.prepare(sampleRate);
        env_.prepare(sampleRate);
        level_.prepare(sampleRate, levelSmoothingMs);
        level_.reset(level_.target());
    }

    void reset() noexcept
    {
        phasor_.reset();
        env_.reset();
    }

    void setFrequency(double hz) noexcept { phasor_.setFrequency(hz); }
    void setEnvelope(const dsp::AdsrParams& p) noexcept { env_.setParams(p); }
    /// Output amplitude (carrier) or peak modulation index in radians (modulator). See header.
    /// Jumps immediately: for note starts, when the envelope is at zero anyway.
    void setLevel(float level) noexcept { level_.reset(level); }
    /// Glides to the new level (one-pole, see prepare): for live knob changes while a note sounds (L2.4).
    void setLevelSmoothed(float level) noexcept { level_.setTarget(level); }

    /// Key-synced: the phase restarts at 0 on every note so attacks sound the same each time.
    /// (A free-running phase would make every note's attack different. DX7 has a switch for this.)
    void noteOn() noexcept
    {
        phasor_.reset();
        env_.noteOn();
    }
    void noteOff() noexcept { env_.noteOff(); }

    /// One sample. pmRadians is the phase modulation input (sum of the modulators feeding this op).
    [[nodiscard]] float tick(float pmRadians = 0.0f) noexcept
    {
        const dsp::Phase p = phasor_.tick() + radiansToPhase(pmRadians); // unsigned add: wraps for free
        return level_.tick() * env_.tick() * dsp::sineTable(p);
    }

    [[nodiscard]] bool isActive() const noexcept { return env_.isActive(); }
    [[nodiscard]] double frequencyHz() const noexcept { return phasor_.frequencyHz(); }

private:
    dsp::Phasor phasor_;
    dsp::Adsr env_;
    dsp::OnePoleSmoother level_{};
};

} // namespace fss::synth
