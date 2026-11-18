// fss/synth/voice.h: one slot in the Engine's voice pool: an FmVoice, the voice's filter (L1.10), plus
// the bookkeeping the allocator needs (key, age, released?, being stolen?) and a short "steal fade" (L1.7, D8).
//
//   FmVoice (4 operators) ──► Svf (mode, cutoff from base + filter env + key + velocity) ──► steal fade ──►
#pragma once

#include <fss/dsp/adsr.h>
#include <fss/dsp/svf.h>
#include <fss/synth/fm_voice.h>

#include <algorithm>
#include <cstdint>

namespace fss::synth {

/// Per-sample values the Engine computes ONCE and every voice reads (L1.10): the live (smoothed) modulation
/// matrix and the smoothed global filter controls. Computing them per voice would multiply the smoothing
/// cost by 20, and they'd still have to agree across voices.
struct VoiceControl {
    const Algorithm* alg = nullptr;
    float cutoffOctaves = 0.0f; // log2(base cutoff in Hz), smoothed
    float q = 0.7071f;          // resonance, smoothed
};

// FilterMode is Off followed by the SvfMode values in the same order (tick() relies on it).
static_assert(static_cast<int>(FilterMode::LowPass) == static_cast<int>(dsp::SvfMode::LowPass) + 1 &&
              static_cast<int>(FilterMode::Notch) == static_cast<int>(dsp::SvfMode::Notch) + 1);

class Voice {
public:
    void prepare(double sampleRate, float stealFadeMs, float levelSmoothingMs = 5.0f) noexcept
    {
        fm_.prepare(sampleRate, levelSmoothingMs);
        filterEnv_.prepare(sampleRate);
        svf_.prepare(sampleRate);
        // Linear fade length; at least 1 sample so a "0 ms" fade is an instant cut, not a divide by zero.
        fadeStep_ = 1.0f / std::max(1.0f, stealFadeMs * 0.001f * static_cast<float>(sampleRate));
        reset();
    }

    void reset() noexcept
    {
        fm_.reset();
        filterEnv_.reset();
        svf_.reset();
        busy_ = released_ = fading_ = false;
        fadeGain_ = 1.0f;
        noteId_ = -1;
    }

    void start(const NoteContext& note, const Patch& patch, std::uint64_t age, std::int32_t noteId) noexcept
    {
        patch_ = &patch;
        fm_.start(note, patch);
        // The filter's per-note offset (key tracking + velocity) is fixed for the note, in octaves; it is
        // re-read in tick() through the patch so live knob changes still apply (cheap: two multiplies).
        velocity_ = note.velocity;
        filterEnv_.setParams(patch.filter.env);
        filterEnv_.noteOn();
        svf_.reset(); // a fresh note starts with empty "capacitors": no leftover ringing from the slot's last note
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
            filterEnv_.noteOff();
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
    void refreshEnvelopes() noexcept
    {
        fm_.refreshEnvelopes();
        if (patch_)
            filterEnv_.setParams(patch_->filter.env);
    }

    /// One sample. `ctl` carries the Engine's live (smoothed) matrix (L2.4) and filter controls (L1.10).
    [[nodiscard]] float tick(const VoiceControl& ctl) noexcept
    {
        float y = fm_.tick(*ctl.alg);
        const float env = filterEnv_.tick(); // always runs, so switching the filter on mid-note finds it in place
        if (const FilterParams& f = patch_->filter; f.mode != FilterMode::Off) {
            const float octaves = ctl.cutoffOctaves + f.envOctaves * env +
                                  f.keyTrack * (static_cast<float>(fm_.key()) - kKeyBreakpoint) / 12.0f +
                                  f.velOctaves * (velocity_ - 1.0f);
            // Octaves → Hz, then the SVF prewarps (tan). Two transcendentals per sample per voice: measured
            // in L2.6. The SVF clamps the result to [1 Hz, 0.49·fs].
            svf_.set(std::exp2(octaves), ctl.q);
            y = svf_.tick(y, static_cast<dsp::SvfMode>(static_cast<int>(f.mode) - 1)); // FilterMode = SvfMode + 1
        }
        if (fading_) {
            fadeGain_ -= fadeStep_;
            if (fadeGain_ <= 0.0f) {
                fadeGain_ = 0.0f;
                busy_ = false; // silent: free the slot
            }
            y *= fadeGain_;
        } else if (!fm_.isActive(*ctl.alg)) {
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
    dsp::Adsr filterEnv_;
    dsp::Svf svf_;
    float velocity_ = 1.0f;
    const Patch* patch_ = nullptr;
    bool busy_ = false, released_ = false, fading_ = false;
    float fadeGain_ = 1.0f, fadeStep_ = 1.0f;
    std::uint64_t age_ = 0; // note-on counter value when started: smaller = older
    std::int32_t noteId_ = -1;
};

} // namespace fss::synth
