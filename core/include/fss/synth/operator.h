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
#include <fss/dsp/op_waves.h>
#include <fss/dsp/phasor.h>
#include <fss/dsp/smoother.h>
#include <fss/dsp/sine.h>
#include <fss/util/inline.h>

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

    /// The operator's own frequency (from the note, ratio, detune, bend). Pitch modulation multiplies it.
    void setFrequency(double hz) noexcept
    {
        baseHz_ = hz;
        phasor_.setFrequency(baseHz_ * pitchRatio_);
    }
    /// Pitch modulation as a frequency ratio (vibrato, glide: L1.11/L2.5). 1.0 = none; multiplying by exactly
    /// 1.0 changes nothing, so unmodulated notes are bit-identical to before. Phase stays continuous.
    void setPitchRatio(double ratio) noexcept
    {
        pitchRatio_ = ratio;
        phasor_.setFrequency(baseHz_ * pitchRatio_);
    }
    void setEnvelope(const dsp::AdsrParams& p) noexcept { env_.setParams(p); }
    /// Output amplitude (carrier) or peak modulation index in radians (modulator). See header.
    /// Jumps immediately: for note starts, when the envelope is at zero anyway.
    void setLevel(float level) noexcept { level_.reset(level); }
    /// A per-sample level multiplier from the mod matrix (L4.1, "Op X level"); 1 = unmodulated. Applied after
    /// the smoothed level, so (level · 1) · env · sine is bit-identical to the unmodulated product.
    void setLevelMod(float m) noexcept { levelMod_ = m; }
    /// Jump the oscillator's phase (L4.4: unison copies start at random phases, so they don't add up in phase).
    void setPhase(dsp::Phase p) noexcept
    {
        phasor_.reset(p);
        last_ = p - phasor_.increment();
    }
    /// The waveform (L4.2, dsp/op_waves.h). Sine is the operator of before L4.2, bit for bit. The sine path doesn't
    /// track the previous phase (one store per operator per sample saved), so a switch AWAY from sine estimates it
    /// from the oscillator alone; only the first sample's band-limiting can be off by the modulation's step.
    void setWave(dsp::OpWave w) noexcept
    {
        if (w != wave_ && wave_ == dsp::OpWave::Sine)
            last_ = phasor_.phase() - phasor_.increment();
        wave_ = w;
    }
    [[nodiscard]] dsp::OpWave wave() const noexcept { return wave_; }
    /// Glides to the new level (one-pole, see prepare): for live knob changes while a note sounds (L2.4).
    void setLevelSmoothed(float level) noexcept { level_.setTarget(level); }

    /// Key-synced: the phase restarts at 0 on every note so attacks sound the same each time.
    /// (A free-running phase would make every note's attack different. DX7 has a switch for this.)
    void noteOn() noexcept
    {
        phasor_.reset();
        last_ = 0u - phasor_.increment(); // as if the phase had been running: the first step is a normal one
        env_.noteOn();
    }
    void noteOff() noexcept { env_.noteOff(); }

    /// One sample. pmRadians is the phase modulation input (sum of the modulators feeding this op).
    [[nodiscard]] float tick(float pmRadians = 0.0f) noexcept
    {
        const dsp::Phase p = phasor_.tick() + radiansToPhase(pmRadians); // unsigned add: wraps for free
        if (wave_ == dsp::OpWave::Sine) // the common case, exactly the pre-L4.2 arithmetic
            return level_.tick() * levelMod_ * env_.tick() * dsp::sineTable(p);
        return level_.tick() * levelMod_ * env_.tick() * otherWave(p); // L4.2, out of line (util/inline.h)
    }

    /// The other waves (L4.2). The band-limited ones need this sample's phase step, modulation included: the
    /// difference of two phases, read as a fraction of a cycle. Above 1/2 cycle it's either faster than Nyquist or
    /// the phase went backwards (deep modulation): no valid correction, so 0 (the naive wave).
    [[nodiscard]] FSS_NOINLINE float otherWave(dsp::Phase p) noexcept
    {
        const double step = dsp::phaseToCycles(p - last_);
        last_ = p;
        return dsp::opWave(wave_, p, step <= 0.5 ? step : 0.0);
    }

    [[nodiscard]] bool isActive() const noexcept { return env_.isActive(); }
    [[nodiscard]] double frequencyHz() const noexcept { return phasor_.frequencyHz(); }

private:
    dsp::Phasor phasor_;
    double baseHz_ = 0.0, pitchRatio_ = 1.0;
    dsp::Adsr env_;
    dsp::OnePoleSmoother level_{};
    float levelMod_ = 1.0f;
    dsp::OpWave wave_ = dsp::OpWave::Sine;
    dsp::Phase last_ = 0; // the previous sample's total phase (for the instantaneous step, L4.2)
};

} // namespace fss::synth
