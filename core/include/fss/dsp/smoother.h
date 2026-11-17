// fss/dsp/smoother.h: one-pole parameter smoothing, the cure for zipper noise (L2.4).
//
// A knob changes a value maybe 60 times a second (GUI frames) or 100 times a second (MIDI CC). Applied as
// a step, each change is a tiny discontinuity in the signal: together they make a buzzy "zipper" sound.
// A one-pole low-pass on the *value* turns each step into a quick exponential glide:
//
//     value += (target − value) · k,     k = 1 − e^(−1 / (τ · fs))
//
// After τ (the time constant) it has covered 63 % of the jump; after ~5τ it has effectively arrived. It's
// the same "approach a target" step as the ADSR segments (L1.3), here aimed exactly at the target.
#pragma once

#include <cmath>

namespace fss::dsp {

class OnePoleSmoother {
public:
    /// timeConstantMs ≤ 0 disables smoothing (jumps straight to the target): used for A/B comparisons.
    void prepare(double sampleRate, float timeConstantMs) noexcept
    {
        k_ = timeConstantMs > 0.0f
                 ? static_cast<float>(1.0 - std::exp(-1.0 / (static_cast<double>(timeConstantMs) * 0.001 * sampleRate)))
                 : 1.0f;
    }

    /// Jump (no glide): for initialization and note starts.
    void reset(float value) noexcept { value_ = target_ = value; }
    void setTarget(float target) noexcept { target_ = target; }

    [[nodiscard]] float tick() noexcept
    {
        value_ += (target_ - value_) * k_;
        return value_;
    }

    [[nodiscard]] float value() const noexcept { return value_; }
    [[nodiscard]] float target() const noexcept { return target_; }

private:
    float k_ = 1.0f;
    float value_ = 0.0f, target_ = 0.0f;
};

} // namespace fss::dsp
