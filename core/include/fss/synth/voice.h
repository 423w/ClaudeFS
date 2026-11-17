// fss/synth/voice.h: one slot in the Engine's voice pool: an FmVoice plus the bookkeeping the
// allocator needs (key, age, released?, being stolen?) and a short "steal fade" (L1.7, D8).
#pragma once

#include <fss/synth/fm_voice.h>

#include <algorithm>
#include <cstdint>

namespace fss::synth {

class Voice {
public:
    void prepare(double sampleRate, float stealFadeMs, float levelSmoothingMs = 5.0f) noexcept
    {
        fm_.prepare(sampleRate, levelSmoothingMs);
        // Linear fade length; at least 1 sample so a "0 ms" fade is an instant cut, not a divide by zero.
        fadeStep_ = 1.0f / std::max(1.0f, stealFadeMs * 0.001f * static_cast<float>(sampleRate));
        reset();
    }

    void reset() noexcept
    {
        fm_.reset();
        busy_ = released_ = fading_ = false;
        fadeGain_ = 1.0f;
        noteId_ = -1;
    }

    void start(const NoteContext& note, const Patch& patch, std::uint64_t age, std::int32_t noteId) noexcept
    {
        patch_ = &patch;
        fm_.start(note, patch);
        busy_ = true;
        released_ = fading_ = false;
        fadeGain_ = 1.0f;
        age_ = age;
        noteId_ = noteId;
    }

    /// Note-off: envelopes enter release; the voice stays busy until its carriers fall silent.
    void release() noexcept
    {
        if (busy_ && !released_) {
            fm_.noteOff();
            released_ = true;
        }
    }

    /// Steal: fade to silence over the steal-fade time, then become free. Never an instant cut (click).
    void kill() noexcept
    {
        if (busy_)
            fading_ = true;
    }

    void setBend(float bend) noexcept { fm_.setBend(bend); }
    void refreshFrequencies() noexcept { fm_.refreshFrequencies(); }
    void refreshLevels() noexcept { fm_.refreshLevels(); }
    void refreshEnvelopes() noexcept { fm_.refreshEnvelopes(); }

    /// One sample under `alg`: the Engine's live (smoothed) copy of the patch's matrix (L2.4).
    [[nodiscard]] float tick(const Algorithm& alg) noexcept
    {
        float y = fm_.tick(alg);
        if (fading_) {
            fadeGain_ -= fadeStep_;
            if (fadeGain_ <= 0.0f) {
                fadeGain_ = 0.0f;
                busy_ = false; // silent: free the slot
            }
            y *= fadeGain_;
        } else if (!fm_.isActive(alg)) {
            busy_ = false; // release finished on its own
        }
        return y;
    }

    [[nodiscard]] bool busy() const noexcept { return busy_; }
    [[nodiscard]] bool released() const noexcept { return released_; }
    [[nodiscard]] bool fading() const noexcept { return fading_; }
    [[nodiscard]] float fadeGain() const noexcept { return fadeGain_; }
    [[nodiscard]] std::uint8_t key() const noexcept { return fm_.key(); }
    [[nodiscard]] std::int32_t noteId() const noexcept { return noteId_; }
    [[nodiscard]] std::uint64_t age() const noexcept { return age_; }

private:
    FmVoice fm_;
    const Patch* patch_ = nullptr;
    bool busy_ = false, released_ = false, fading_ = false;
    float fadeGain_ = 1.0f, fadeStep_ = 1.0f;
    std::uint64_t age_ = 0; // note-on counter value when started: smaller = older
    std::int32_t noteId_ = -1;
};

} // namespace fss::synth
