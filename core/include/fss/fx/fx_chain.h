// fss/fx/fx_chain.h: the global FX chain after the voice sum (L5.1, D16).
//
//   voices ─► [ drive ] ─► [ crush ] ─► [ chorus ] ─► [ flanger ] ─► [ phaser ] ─► [ delay ] ─► [ reverb ] ─► [ width ] ─► output ─► clip
//             └── FxSlots, fixed order; later lessons insert theirs ──────────────────────────────┘
//
// The planned order, each lesson inserting its slot where it belongs:
//   drive → crush (L5.2) → chorus → flanger → phaser (L5.4) → delay (L5.3) → reverb (L5.5) → EQ (L5.6) → width → output
// Why this order: distortion before anything that spreads the sound in time (distorting a reverb makes mud);
// modulation before echoes, so every repeat carries the chorus; reverb after the delay, so the echoes sit in
// the room; EQ on the finished sound; width last, so it widens what the stereo effects made; the output level
// last of all, right before the Engine's safety clip. It's the order of a classic synth FX section (and of
// most guitar pedalboards), and it's FIXED: D16 chose a fixed order over reorderable slots (DECISIONS.md).
//
// The slots live in a std::tuple and are visited with a fold expression: static dispatch, no heap, no
// virtual calls. Adding an effect = one type in the tuple, one line in setParams().
#pragma once

#include <fss/fx/chorus.h>
#include <fss/fx/crush.h>
#include <fss/fx/delay.h>
#include <fss/fx/drive.h>
#include <fss/fx/effect.h>
#include <fss/fx/flanger.h>
#include <fss/fx/fx_params.h>
#include <fss/fx/gain.h>
#include <fss/fx/phaser.h>
#include <fss/fx/reverb.h>
#include <fss/fx/width.h>

#include <algorithm>
#include <tuple>

namespace fss::fx {

class FxChain {
public:
    /// Allocates every effect's buffers (maxBlock samples of scratch per slot). Not real-time safe.
    void prepare(double sampleRate, int maxBlock, float outputSmoothingMs = 10.0f)
    {
        maxBlock_ = std::clamp(maxBlock, 1, 1 << 16); // longer stretches are processed in pieces
        std::apply([&](auto&... slot) { (slot.prepare(sampleRate, maxBlock_), ...); }, slots_);
        output_.prepare(sampleRate, maxBlock_, outputSmoothingMs);
    }

    /// Silence every effect and snap every switch and smoother to its target.
    void reset() noexcept
    {
        std::apply([](auto&... slot) { (slot.reset(), ...); }, slots_);
        output_.reset();
    }

    /// New settings (targets: switches fade, smoothed values glide). Real-time safe.
    void setParams(const FxParams& p) noexcept
    {
        auto& drive = std::get<FxSlot<Drive>>(slots_);
        drive.setEnabled(p.drive.enabled);
        drive.effect().setParams(p.drive);
        auto& crush = std::get<FxSlot<Crush>>(slots_);
        crush.setEnabled(p.crush.enabled);
        crush.effect().setParams(p.crush);
        auto& chorus = std::get<FxSlot<Chorus>>(slots_);
        chorus.setEnabled(p.chorus.enabled);
        chorus.effect().setParams(p.chorus);
        auto& flanger = std::get<FxSlot<Flanger>>(slots_);
        flanger.setEnabled(p.flanger.enabled);
        flanger.effect().setParams(p.flanger);
        auto& phaser = std::get<FxSlot<Phaser>>(slots_);
        phaser.setEnabled(p.phaser.enabled);
        phaser.effect().setParams(p.phaser);
        auto& delay = std::get<FxSlot<StereoDelay>>(slots_);
        delay.setEnabled(p.delay.enabled);
        delay.effect().setParams(p.delay);
        auto& reverb = std::get<FxSlot<Reverb>>(slots_);
        reverb.setEnabled(p.reverb.enabled);
        reverb.effect().setParams(p.reverb);
        auto& width = std::get<FxSlot<StereoWidth>>(slots_);
        width.setEnabled(p.width.enabled);
        width.effect().setParams(p.width);
    }

    /// The host tempo (L5.3), for tempo-synced effects.
    void setTempo(double bpm) noexcept { std::get<FxSlot<StereoDelay>>(slots_).effect().setTempo(bpm); }

    void setOutputDb(float db) noexcept { output_.setDb(db); }
    void snapOutputDb(float db) noexcept { output_.snapDb(db); }

    /// Process a stereo stretch in place, any length.
    void process(float* L, float* R, int n) noexcept
    {
        for (int start = 0; start < n; start += maxBlock_) {
            const int len = std::min(maxBlock_, n - start);
            float* l = L + start;
            float* r = R + start;
            std::apply([&](auto&... slot) { (slot.process(l, r, len), ...); }, slots_);
            output_.process({l, r, len});
        }
    }

    /// The chain's tail: serial effects' tails add up (an echo of a reverb of an echo...).
    [[nodiscard]] int tailSamples() const noexcept
    {
        return std::apply([](const auto&... slot) { return (0 + ... + slot.tailSamples()); }, slots_);
    }
    /// Is any effect with a tail still running? (Then the output may still change though the input is silent.)
    [[nodiscard]] bool ringing() const noexcept
    {
        return std::apply([](const auto&... slot) { return (false || ... || slot.ringing()); }, slots_);
    }

    // ---- latency (L5.2) ----
    /// The chain's delay in samples (serial delays add). Constant until commitLatency().
    [[nodiscard]] int latencySamples() const noexcept
    {
        return std::apply([](const auto&... slot) { return (0 + ... + slot.latencySamples()); }, slots_);
    }
    /// Some effect asks for a different latency than the one in use (the oversampling factor changed).
    [[nodiscard]] bool latencyPending() const noexcept
    {
        return std::apply([](const auto&... slot) { return (false || ... || slot.latencyPending()); }, slots_);
    }
    /// Switch every effect to the latency it asks for. Real-time safe, but a jump in time (see FxSlot).
    void commitLatency() noexcept
    {
        std::apply([](auto&... slot) { (slot.commitLatency(), ...); }, slots_);
    }

    [[nodiscard]] const FxSlot<StereoWidth>& width() const noexcept { return std::get<FxSlot<StereoWidth>>(slots_); }
    [[nodiscard]] const FxSlot<Drive>& drive() const noexcept { return std::get<FxSlot<Drive>>(slots_); }
    [[nodiscard]] const FxSlot<StereoDelay>& delay() const noexcept { return std::get<FxSlot<StereoDelay>>(slots_); }

private:
    std::tuple<FxSlot<Drive>, FxSlot<Crush>, FxSlot<Chorus>, FxSlot<Flanger>, FxSlot<Phaser>, FxSlot<StereoDelay>, FxSlot<Reverb>, FxSlot<StereoWidth>> slots_{};
    Gain output_{};
    int maxBlock_ = 512;
};

} // namespace fss::fx
