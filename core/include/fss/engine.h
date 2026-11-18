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

#include <fss/dsp/lfo.h>
#include <fss/dsp/phasor.h>
#include <fss/dsp/smoother.h>
#include <fss/event.h>
#include <fss/fx/fx_chain.h>
#include <fss/synth/held_notes.h>
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

    /// May allocate (since L5.1 the FX chain's buffers live here), so it isn't noexcept: a host that must not
    /// throw (the CLAP plugin's activate) catches std::bad_alloc and reports failure.
    void prepare(double sampleRate, int maxBlockSize, float stealFadeMs = 5.0f);
    void reset() noexcept;

    /// Replace the whole patch at once (no smoothing). Main thread, while audio is stopped, or offline.
    /// While playing, change parameters with ParamChange events instead (L2.4): they're sample-accurate,
    /// smoothed where it matters, and safe on the audio thread.
    void setPatch(const synth::Patch& patch) noexcept;

    /// A/B demos only: false makes every live parameter change jump (zipper noise). Call before prepare().
    void setSmoothingEnabled(bool on) noexcept { smoothing_ = on; }
    /// A/B demos only (L2.6): false leaves the thread's denormal mode alone inside process().
    void setFlushDenormals(bool on) noexcept { flushDenormals_ = on; }
    [[nodiscard]] const synth::Patch& patch() const noexcept { return patch_; }

    /// Sounding-note limit, 1..kMaxPolyphony (tests and stress renders use small values).
    void setPolyphony(int voices) noexcept;

    /// Render numFrames samples into outL/outR (overwritten, not added), applying `events` at their frames.
    /// Events must be sorted by frame; frames ≥ numFrames are applied at the end of the block.
    void process(std::span<const Event> events, float* outL, float* outR, int numFrames) noexcept;

    // Statistics for tests and renders.
    [[nodiscard]] int busyVoices() const noexcept;
    [[nodiscard]] std::uint64_t steals() const noexcept { return steals_; }
    /// Read-only view of the voice pool, for scenario tests (L2.5): "after these events, these voices
    /// should be in these states".
    [[nodiscard]] const std::array<synth::Voice, kNumVoices>& voices() const noexcept { return voices_; }
    [[nodiscard]] bool sustainPedal() const noexcept { return sustainPedal_; }
    /// How many blocks came out non-finite (NaN/Inf) and were replaced by silence + a voice reset (L6.4).
    /// Should stay 0 forever; a host may poll it and log a warning (never on the audio thread).
    [[nodiscard]] std::uint64_t nonFiniteBlocks() const noexcept { return nonFiniteBlocks_; }
    /// The FX chain's tail in samples (L5.1): how long output can go on after the voices fall silent.
    /// The CLAP plugin reports it to the host (clap.tail).
    [[nodiscard]] int fxTailSamples() const noexcept { return fx_.tailSamples(); }
    /// An effect with a tail (delay, reverb) is still running: the output may change though no voice sounds.
    [[nodiscard]] bool fxRinging() const noexcept { return fx_.ringing(); }
    [[nodiscard]] const fx::FxChain& fx() const noexcept { return fx_; }

private:
    void processEvents(std::span<const Event> events, float* outL, float* outR, int numFrames) noexcept;
    void handle(const Event& e) noexcept;
    void paramChange(const Event& e) noexcept;
    void noteOn(const Event& e) noexcept;
    void noteOff(const Event& e) noexcept;
    void controller(const Event& e) noexcept;
    void monoUpdate(bool pressed, std::uint8_t pressedKey, std::int32_t noteId) noexcept;
    synth::Voice* startVoice(std::uint8_t key, float velocity, std::int32_t noteId) noexcept;
    [[nodiscard]] int glideSamples() const noexcept;
    void allNotesOff() noexcept;
    void render(float* outL, float* outR, int from, int to) noexcept;
    void settleMatrix() noexcept;
    void restartLfos() noexcept;
    [[nodiscard]] synth::Voice* findFreeVoice() noexcept;
    [[nodiscard]] synth::Voice* chooseVictim() noexcept;

    std::array<synth::Voice, kNumVoices> voices_{};
    synth::Patch patch_{};          // the target state: ParamChange events edit it in place
    synth::Algorithm live_{};       // what voices actually use: patch_.algorithm, smoothed per sample
    bool liveSettling_ = false;     // live_ still gliding toward patch_.algorithm?
    float matrixK_ = 1.0f;          // one-pole coefficient for the matrix glide
    fx::FxChain fx_{};              // voice sum → effects → output gain (L5.1; the gain lived here until then)
    dsp::OnePoleSmoother cutoffOctaves_{}, q_{}; // filter base cutoff (log2 Hz) and resonance, smoothed (L1.10)
    dsp::Lfo lfo2_;                 // the global LFO (L1.11)
    dsp::Phasor lfo1Reference_;     // shared phase for voices whose LFO 1 isn't key-synced
    dsp::OnePoleSmoother modWheel_{};
    synth::Algorithm indexScaled_{}; // live_ with every modulation amount scaled by LFO 2 (when routed)
    bool smoothing_ = true;
    bool flushDenormals_ = true;
    int polyphony_ = kMaxPolyphony;
    double sampleRate_ = 48000.0;
    float bend_ = 0.0f;
    dsp::OnePoleSmoother bendSmoothed_{}; // −1..1, smoothed (L2.5)
    // Playing state (L2.5)
    synth::HeldNotes held_;               // keys physically down, in press order
    bool sustainPedal_ = false;           // CC 64
    synth::Voice* monoVoice_ = nullptr;   // the voice mono/legato modes are playing
    float lastPitch_ = 0.0f;              // where the last note started or ended up: glides start here
    bool hasLastPitch_ = false;
    std::uint64_t noteCounter_ = 0, steals_ = 0;
    std::uint64_t nonFiniteBlocks_ = 0;
};

/// The output stage's safety soft clip: transparent below ±0.8, then a smooth knee that never exceeds ±1.
/// Not a sound-design feature. It's there so a pile-up of voices distorts gently instead of wrapping or clipping hard.
[[nodiscard]] float softClip(float x) noexcept;

} // namespace fss
