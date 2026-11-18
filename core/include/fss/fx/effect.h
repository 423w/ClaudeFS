// fss/fx/effect.h: what an effect is, and the slot that switches one in and out without clicks (L5.1, D16).
//
// THE CONTRACT (D16). An effect is any type that satisfies the StereoEffect concept below: prepare / reset /
// process a StereoBlock in place / say how long its tail is. There is no base class and no virtual call:
// FxChain (fx_chain.h) holds its effects BY VALUE, in a fixed order, in a std::tuple, so every call is a
// direct (inlinable) call and adding an effect is adding one type to that tuple. The concept gives the
// "interface" its compile-time check: a missing noexcept or a wrong signature is a compile error that names
// the requirement, instead of a template error three screens long.
//
// WHAT AN EFFECT OUTPUTS. Its complete output: if it has a dry/wet mix, it applies it itself (a delay's
// "mix" knob is part of the delay). The slot never mixes dry and wet for the effect; it only fades the effect
// in and out as a whole, which is a different job (see FxSlot).
//
// BLOCK-SIZE INVARIANCE (L1.7). The Engine calls the chain on whatever stretch lies between two events, so an
// effect must give bit-identical output however the stream is chopped: per-sample state machines only, no
// "compute this once per block" (coefficients that change per block would make the sound depend on the host's
// buffer size). The slot's own decisions (fades, sleeping) are made per sample for the same reason.
#pragma once

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstring>
#include <numbers>
#include <vector>

namespace fss::fx {

/// One stretch of stereo audio, processed in place. L and R never alias each other.
struct StereoBlock {
    float* L;
    float* R;
    int n;
};

template <class E>
concept StereoEffect = requires(E fx, const E cfx, StereoBlock b, double sampleRate, int maxBlock) {
    { fx.prepare(sampleRate, maxBlock) };              // allocate here (delay buffers), never in process
    { fx.reset() } noexcept;                           // clear the state (silence), snap smoothers to targets
    { fx.process(b) } noexcept;                        // real-time safe: docs/RT-RULES.md
    { cfx.tailSamples() } noexcept -> std::convertible_to<int>; // output may go on this long after silent input
    { E::kSpillsTail } -> std::convertible_to<bool>;   // bypass lets the tail ring out (delay, reverb)?
};

// ---- dry/wet laws ---------------------------------------------------------------------------------------
// Effects with a mix knob pick one of these. Which is right depends on how alike dry and wet are:
//   LINEAR   dry = 1 − m, wet = m. Correct when wet ≈ dry (a short chorus, a filter): two coherent signals
//            add as amplitudes, so 0.5 + 0.5 keeps the level, where equal power would bump it by +3 dB.
//   EQUAL POWER  dry = cos(m·π/2), wet = sin(m·π/2). Correct when wet is UNCORRELATED with dry (echoes,
//            reverb): uncorrelated signals add as powers, so cos² + sin² = 1 keeps the loudness constant,
//            where linear would dip by 3 dB in the middle.
struct MixGains {
    float dry, wet;
};
[[nodiscard]] inline MixGains linearMix(float mix) noexcept
{
    const float m = std::clamp(mix, 0.0f, 1.0f);
    return {1.0f - m, m};
}
[[nodiscard]] inline MixGains equalPowerMix(float mix) noexcept
{
    const float a = std::clamp(mix, 0.0f, 1.0f) * (std::numbers::pi_v<float> * 0.5f);
    // Snap the ends: cos(π/2) in float is 4e-8, not 0, and "mix 100 %" should mean no dry at all.
    if (mix >= 1.0f)
        return {0.0f, 1.0f};
    return {std::cos(a), std::sin(a)};
}

/// The time an enable/disable fade takes. 10 ms: far shorter than a note, long enough that the ramp's
/// spectrum stays below ~100 Hz (no click). Same order as the parameter smoothing (L2.4).
inline constexpr float kSlotFadeMs = 10.0f;

/// One effect in the chain, plus its on/off switch.
///
/// SWITCHING (the click problem). Turning an effect on or off instantly is a step from one signal to another:
/// a click. So the slot ramps a gain g from 0 to 1 (or back) over kSlotFadeMs, linearly, per sample. Linear,
/// because the two signals it fades between (the input and the effect's output) are strongly correlated, the
/// same reasoning as linearMix above. What the ramp is applied to depends on the effect:
///
///   CROSSFADE (kSpillsTail = false: width, drive, chorus)
///       out = (1 − g)·in + g·fx(in)          fading out = back to the dry input; the effect stops.
///   SPILL (kSpillsTail = true: delay, reverb)
///       out = (1 − g)·in + fx(g·in)          fading out = stop FEEDING the effect, but keep playing its
///                                            output, so the echoes already in flight ring out naturally
///                                            instead of being cut off ("tail spillover", what hardware does).
///   At g = 1 both are just fx(in); at g = 0 the first is `in`, the second `in + tail`.
///
/// SLEEPING (CPU). An effect costs nothing when it isn't needed. The slot resets it and stops calling it:
///   - CROSSFADE: from the sample where g reaches 0;
///   - any effect with a tail: once its input has been exact silence for tailSamples() samples in a row
///     (by then the tail has decayed below audibility by the effect's own definition of its tail);
/// and wakes it on the first sample that needs it. An effect with tailSamples() == 0 (width) never sleeps
/// on silence while on: it's cheap, and sleeping on every exact-zero sample would chop its blocks into
/// crumbs. While asleep, the effect's output counts as exact silence, which is why reset() must leave an
/// effect that turns silence into silence.
///
/// All of this is decided per sample, so it is block-size invariant like everything else.
template <StereoEffect E>
class FxSlot {
public:
    void prepare(double sampleRate, int maxBlock)
    {
        fx_.prepare(sampleRate, maxBlock);
        const auto n = static_cast<std::size_t>(std::max(1, maxBlock));
        dryL_.assign(n, 0.0f); // the only allocations: here, never in process()
        dryR_.assign(n, 0.0f);
        ramp_.assign(n, 0.0f);
        maxBlock_ = static_cast<int>(n);
        step_ = static_cast<float>(1.0 / (static_cast<double>(kSlotFadeMs) * 0.001 * sampleRate));
        reset();
    }

    /// Silence: the effect cleared and asleep, the switch at its target (no fade pending).
    void reset() noexcept
    {
        fx_.reset();
        gain_ = target_;
        asleep_ = true;
        silentRun_ = 0;
    }

    void setEnabled(bool on) noexcept { target_ = on ? 1.0f : 0.0f; }
    [[nodiscard]] bool enabled() const noexcept { return target_ > 0.0f; }
    /// The effect is running (awake): it may still be producing a tail even if the slot is switched off.
    [[nodiscard]] bool awake() const noexcept { return !asleep_; }
    /// How long this slot may keep sounding after its input goes silent (0 when it's off and asleep).
    [[nodiscard]] int tailSamples() const noexcept { return (enabled() || awake()) ? fx_.tailSamples() : 0; }

    [[nodiscard]] E& effect() noexcept { return fx_; }
    [[nodiscard]] const E& effect() const noexcept { return fx_; }

    /// Process in place. n ≤ the maxBlock given to prepare() (FxChain splits longer stretches).
    void process(float* L, float* R, int n) noexcept
    {
        if (n <= 0)
            return;
        if (gain_ == 0.0f && target_ == 0.0f && asleep_)
            return; // switched off and silent: the input passes through untouched (bit-exact bypass)
        const int tail = fx_.tailSamples();
        const bool steadyOn = gain_ == 1.0f && target_ == 1.0f;
        if (steadyOn && tail == 0) { // the common case for a cheap effect: no copies, no bookkeeping
            asleep_ = false;
            fx_.process({L, R, n});
            return;
        }

        // 1. Remember the dry input, compute this block's fade gains, and turn L/R into the effect's input.
        if (!steadyOn) {
            std::memcpy(dryL_.data(), L, sizeof(float) * static_cast<std::size_t>(n));
            std::memcpy(dryR_.data(), R, sizeof(float) * static_cast<std::size_t>(n));
            for (int i = 0; i < n; ++i) {
                gain_ = target_ > gain_ ? std::min(target_, gain_ + step_) : std::max(target_, gain_ - step_);
                ramp_[static_cast<std::size_t>(i)] = gain_;
                if constexpr (E::kSpillsTail) {
                    L[i] *= gain_; // spill: what gets INTO the effect fades; what comes out is never cut
                    R[i] *= gain_;
                }
            }
        }

        // 2. Run the effect on the stretches where it's awake; asleep, its output is exact silence.
        auto wakes = [&](int i) {
            if constexpr (!E::kSpillsTail)
                if (!steadyOn && ramp_[static_cast<std::size_t>(i)] == 0.0f)
                    return false; // crossfade: faded out, nothing needs the effect
            return tail == 0 || L[i] != 0.0f || R[i] != 0.0f;
        };
        int i = 0;
        while (i < n) {
            if (asleep_) {
                int j = i;
                while (j < n && !wakes(j)) {
                    L[j] = 0.0f;
                    R[j] = 0.0f;
                    ++j;
                }
                if (j == n)
                    break;
                asleep_ = false;
                silentRun_ = 0;
                i = j;
            }
            int j = i;
            bool sleepNext = false;
            while (j < n) {
                if constexpr (!E::kSpillsTail)
                    if (!steadyOn && ramp_[static_cast<std::size_t>(j)] == 0.0f) {
                        sleepNext = true; // sample j doesn't need the effect: stop before it
                        break;
                    }
                silentRun_ = (L[j] == 0.0f && R[j] == 0.0f) ? silentRun_ + 1 : 0;
                ++j;
                if (tail > 0 && silentRun_ >= tail) {
                    sleepNext = true; // the tail is over: this sample is the last one the effect needs
                    break;
                }
            }
            fx_.process({L + i, R + i, j - i});
            i = j;
            if (sleepNext) {
                fx_.reset();
                asleep_ = true;
            }
        }

        // 3. Mix with the dry input according to the fade.
        if (!steadyOn)
            for (int k = 0; k < n; ++k) {
                const auto u = static_cast<std::size_t>(k);
                const float g = ramp_[u];
                const float wet = E::kSpillsTail ? 1.0f : g; // spill: the effect's input was already faded
                L[k] = (1.0f - g) * dryL_[u] + wet * L[k];
                R[k] = (1.0f - g) * dryR_[u] + wet * R[k];
            }
    }

    [[nodiscard]] int maxBlock() const noexcept { return maxBlock_; }

private:
    E fx_{};
    std::vector<float> dryL_, dryR_, ramp_;
    int maxBlock_ = 0;
    float step_ = 1.0f;
    float gain_ = 0.0f, target_ = 0.0f; // the switch: current fade gain and where it's heading
    bool asleep_ = true;
    int silentRun_ = 0; // consecutive exact-zero input samples (for the tail sleep)
};

} // namespace fss::fx
