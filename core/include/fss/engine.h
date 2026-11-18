// fss/engine.h: the public API of fss_core. Every host talks to the synth through this (L1.7).
//
//   prepare(sampleRate, maxBlock)   once, before audio starts (may allocate; today nothing needs to)
//   setPatch(patch)                 not real-time safe yet: offline / between blocks only (L2.4 makes it safe)
//   process(events, L, R, n)        real-time safe: no allocation, locks, I/O or exceptions
//
// process() is SAMPLE-ACCURATE: it renders up to each event's frame, applies the event, and carries on,
// so the output doesn't depend on how the host chops time into blocks. The block-size invariance test
// (tests/test_engine.cpp) holds that to bit-exactness.
//
// VOICES (D8): kMaxPolyphony = 16 sounding notes, in a pool of 16 + kStealHeadroom physical voices.
// When a new note would exceed the polyphony, a victim is chosen (see below) and *faded* over
// stealFadeMs (5 ms default) in its own slot while the new note starts at once in a spare slot. So a steal
// costs neither a click (no instant cut) nor latency (the new note isn't delayed until the fade ends).
#pragma once

#include <fss/dsp/smoother.h>
#include <fss/event.h>
#include <fss/synth/patch.h>
#include <fss/synth/voice.h>

#include <array>
#include <cstdint>
#include <span>

namespace fss {

class Engine {
public:
    static constexpr int kMaxPolyphony = 16;
    static constexpr int kStealHeadroom = 4; // voices that can be fading out while 16 others sound
    static constexpr int kNumVoices = kMaxPolyphony + kStealHeadroom;

    void prepare(double sampleRate, int maxBlockSize, float stealFadeMs = 5.0f) noexcept;
    void reset() noexcept;

    /// Replace the whole patch at once (no smoothing). Main thread, while audio is stopped, or offline.
    /// While playing, change parameters with ParamChange events instead (L2.4): they're sample-accurate,
    /// smoothed where it matters, and safe on the audio thread.
    void setPatch(const synth::Patch& patch) noexcept;

    /// A/B demos only: false makes every live parameter change jump (zipper noise). Call before prepare().
    void setSmoothingEnabled(bool on) noexcept { smoothing_ = on; }
    [[nodiscard]] const synth::Patch& patch() const noexcept { return patch_; }

    /// Sounding-note limit, 1..kMaxPolyphony (tests and stress renders use small values).
    void setPolyphony(int voices) noexcept;

    /// Render numFrames samples into outL/outR (overwritten, not added), applying `events` at their frames.
    /// Events must be sorted by frame; frames ≥ numFrames are applied at the end of the block.
    void process(std::span<const Event> events, float* outL, float* outR, int numFrames) noexcept;

    // Statistics for tests and renders.
    [[nodiscard]] int busyVoices() const noexcept;
    [[nodiscard]] std::uint64_t steals() const noexcept { return steals_; }

private:
    void handle(const Event& e) noexcept;
    void paramChange(const Event& e) noexcept;
    void noteOn(const Event& e) noexcept;
    void noteOff(const Event& e) noexcept;
    void render(float* outL, float* outR, int from, int to) noexcept;
    void settleMatrix() noexcept;
    [[nodiscard]] synth::Voice* findFreeVoice() noexcept;
    [[nodiscard]] synth::Voice* chooseVictim() noexcept;

    std::array<synth::Voice, kNumVoices> voices_{};
    synth::Patch patch_{};          // the target state: ParamChange events edit it in place
    synth::Algorithm live_{};       // what voices actually use: patch_.algorithm, smoothed per sample
    bool liveSettling_ = false;     // live_ still gliding toward patch_.algorithm?
    float matrixK_ = 1.0f;          // one-pole coefficient for the matrix glide
    dsp::OnePoleSmoother gain_{};   // output gain, smoothed per sample
    dsp::OnePoleSmoother cutoffOctaves_{}, q_{}; // filter base cutoff (log2 Hz) and resonance, smoothed (L1.10)
    bool smoothing_ = true;
    int polyphony_ = kMaxPolyphony;
    float bend_ = 0.0f;
    std::uint64_t noteCounter_ = 0, steals_ = 0;
};

/// The output stage's safety soft clip: transparent below ±0.8, then a smooth knee that never exceeds ±1.
/// Not a sound-design feature. It's there so a pile-up of voices distorts gently instead of wrapping or clipping hard.
[[nodiscard]] float softClip(float x) noexcept;

} // namespace fss
