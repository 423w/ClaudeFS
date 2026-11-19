// fss/fx/crush.h: a bitcrusher: fewer bits and a lower sample rate, on purpose (L5.2).
//
// The two ways a digital signal can be made worse, each the mirror image of a lesson we already learned:
//   BITS  (amplitude quantization, L1.1): round every sample to one of 2^bits levels. The rounding error is a
//         noise that FOLLOWS the signal: silent in silence (zero rounds to zero), loudest on loud, simple waves,
//         where it isn't noise at all but harmonics of the wave (a staircase). Fractional bits are allowed, so
//         the knob sweeps smoothly: step = 2^(1 − bits) of full scale.
//   RATE  (sample & hold): keep a sample, repeat it until the next "tick" of a slower clock. That IS
//         downsampling without the anti-alias filter: everything above rate/2 folds back, inharmonically.
//         The aliasing that L1.8 and the Drive's oversampling fight is here the whole point.
//
//   in ─► hold at rateHz ─► round to 2^bits levels ─► mix (linear) ─► out
//
// No oversampling: the artefacts ARE the effect. The hold clock is a phase accumulator in double, so any
// rate works (not just fs/integer), and its phase is shared by both channels, so the stereo image holds.
#pragma once

#include <fss/dsp/smoother.h>
#include <fss/fx/effect.h>
#include <fss/fx/fx_params.h>

#include <algorithm>
#include <cmath>

namespace fss::fx {

class Crush {
public:
    static constexpr bool kSpillsTail = false;

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sampleRate_ = sampleRate;
        dry_.prepare(sampleRate, 20.0f);
        wet_.prepare(sampleRate, 20.0f);
        setParams(params_);
        reset();
    }

    void reset() noexcept
    {
        phase_ = 1.0 - inc_; // the very first sample is taken at once, then one every 1/inc_ samples
        heldL_ = heldR_ = 0.0f;
        dry_.reset(dry_.target());
        wet_.reset(wet_.target());
    }

    void setParams(const CrushParams& p) noexcept
    {
        params_ = p;
        // Clamped to 1: at or above the sample rate every sample is a new one (no reduction).
        inc_ = std::clamp(static_cast<double>(p.rateHz) / sampleRate_, 1e-6, 1.0);
        const float bits = std::clamp(p.bits, 1.0f, 16.0f);
        step_ = std::exp2(1.0f - bits);
        invStep_ = 1.0f / step_;
        const MixGains m = linearMix(p.mixPct * 0.01f); // the crushed wave is the dry wave, roughened: in phase
        dry_.setTarget(m.dry);
        wet_.setTarget(m.wet);
    }

    /// A held sample can outlast the input by one hold period.
    [[nodiscard]] int tailSamples() const noexcept { return static_cast<int>(std::ceil(1.0 / inc_)) + 1; }

    void process(StereoBlock b) noexcept
    {
        for (int i = 0; i < b.n; ++i) {
            phase_ += inc_;
            if (phase_ >= 1.0) { // the slow clock ticks: take a new sample
                phase_ -= 1.0;
                heldL_ = quantize(b.L[i]);
                heldR_ = quantize(b.R[i]);
            }
            const float d = dry_.tick(), w = wet_.tick();
            b.L[i] = d * b.L[i] + w * heldL_;
            b.R[i] = d * b.R[i] + w * heldR_;
        }
    }

private:
    /// Mid-tread rounding: 0 stays exactly 0 (silence stays silence, and the FX slot can sleep).
    [[nodiscard]] float quantize(float x) const noexcept { return step_ * std::nearbyint(x * invStep_); }

    CrushParams params_{};
    double sampleRate_ = 48000.0;
    double inc_ = 0.25, phase_ = 1.0;
    float step_ = 1.0f / 128.0f, invStep_ = 128.0f;
    float heldL_ = 0.0f, heldR_ = 0.0f;
    dsp::OnePoleSmoother dry_{}, wet_{};
};

static_assert(StereoEffect<Crush>);

} // namespace fss::fx
