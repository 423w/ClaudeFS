// fss/synth/voice.h: one slot in the Engine's voice pool: an FmVoice, the voice's filter (L1.10), plus
// the bookkeeping the allocator needs (key, age, released?, being stolen?) and a short "steal fade" (L1.7, D8).
//
//   LFO 1 (vibrato) ─► pitch
//   FmVoice (4 operators) ──► Svf (mode, cutoff from base + filter env + key + velocity) ──► steal fade ──►
#pragma once

#include <fss/dsp/adsr.h>
#include <fss/dsp/lfo.h>
#include <fss/dsp/svf.h>
#include <fss/synth/fm_voice.h>

#include <algorithm>
#include <cstdint>

namespace fss::synth {

/// Per-sample values the Engine computes ONCE and every voice reads (L1.10): the live (smoothed) modulation
/// matrix and the smoothed global filter controls. Computing them per voice would multiply the smoothing
/// cost by 20, and they'd still have to agree across voices.
struct VoiceControl {
    const Algorithm* alg = nullptr; // live matrix (L2.4), scaled by LFO 2 when it modulates the index (L1.11)
    float cutoffOctaves = 0.0f; // log2(base cutoff in Hz), smoothed, plus LFO 2's cutoff route
    float q = 0.7071f;          // resonance, smoothed
    float modWheel = 0.0f;      // 0..1, smoothed (L1.11)
    float bendSemitones = 0.0f; // pitch bend × bend range, smoothed (L2.5): a pitch offset like vibrato
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
        lfo1_.prepare(sampleRate);
        // Linear fade length; at least 1 sample so a "0 ms" fade is an instant cut, not a divide by zero.
        fadeStep_ = 1.0f / std::max(1.0f, stealFadeMs * 0.001f * static_cast<float>(sampleRate));
        reset();
    }

    void reset() noexcept
    {
        fm_.reset();
        filterEnv_.reset();
        svf_.reset();
        busy_ = released_ = fading_ = sustained_ = false;
        fadeGain_ = 1.0f;
        glide_ = glideStep_ = 0.0f;
        noteId_ = -1;
    }

    /// lfo1Phase: where a free-running (not key-synced) LFO 1 starts: the Engine's shared reference phase,
    /// so all free voices move together like one LFO.
    void start(const NoteContext& note, const Patch& patch, std::uint64_t age, std::int32_t noteId,
               dsp::Phase lfo1Phase = 0) noexcept
    {
        patch_ = &patch;
        fm_.start(note, patch);
        // The filter's per-note offset (key tracking + velocity) is fixed for the note, in octaves; it is
        // re-read in tick() through the patch so live knob changes still apply (cheap: two multiplies).
        velocity_ = note.velocity;
        filterEnv_.setParams(patch.filter.env);
        filterEnv_.noteOn();
        svf_.reset(); // a fresh note starts with empty "capacitors": no leftover ringing from the slot's last note
        lfo1_.setRate(patch.lfo1.rateHz);
        lfo1_.setDelayFade(patch.lfo1.delayMs, patch.lfo1.fadeMs);
        // Seed from the note counter: deterministic (renders reproduce), different per note (S&H voices differ).
        lfo1_.restart(patch.lfo1.keySync ? 0u : lfo1Phase, static_cast<std::uint32_t>(age * 2654435761u));
        busy_ = true;
        released_ = fading_ = sustained_ = false;
        glide_ = glideStep_ = 0.0f;
        fadeGain_ = 1.0f;
        age_ = age;
        noteId_ = noteId;
    }

    /// Note-off: envelopes enter release; the voice stays busy until its carriers fall silent.
    void release() noexcept
    {
        sustained_ = false;
        if (busy_ && !released_) {
            fm_.noteOff();
            filterEnv_.noteOff();
            released_ = true;
        }
    }

    /// Sustain pedal (L2.5): the key came up while the pedal is down. The note keeps its gate (envelopes stay
    /// in decay/sustain) and is released when the pedal comes up. `hold()` clears it (the key is held again).
    void sustain() noexcept
    {
        if (busy_ && !released_)
            sustained_ = true;
    }
    void hold() noexcept { sustained_ = false; }

    /// Portamento (L2.5): start `semitones` away from the note's key and slide to it, linearly in semitones,
    /// in exactly `samples` samples (constant TIME: a fifth and two octaves take equally long).
    void glideFrom(float semitones, int samples) noexcept
    {
        glide_ = samples > 0 ? semitones : 0.0f;
        glideStep_ = samples > 0 ? std::abs(semitones) / static_cast<float>(samples) : 0.0f;
    }

    /// Legato (L2.5): change the note's key WITHOUT restarting its envelopes or phases; the pitch continues
    /// from where it is now and glides (or jumps, with 0 samples) to the new key.
    void legatoTo(std::uint8_t key, int glideSamples) noexcept
    {
        const float from = pitch();
        fm_.setKey(key);
        glideFrom(from - static_cast<float>(key), glideSamples);
    }

    /// Current pitch in semitones (MIDI key numbers), including an unfinished glide; excludes bend and vibrato.
    [[nodiscard]] float pitch() const noexcept { return static_cast<float>(fm_.key()) + glide_; }

    /// Steal: fade to silence over the steal-fade time, then become free. Never an instant cut (click).
    void kill() noexcept
    {
        if (busy_)
            fading_ = true;
    }

    void refreshFrequencies() noexcept { fm_.refreshFrequencies(); }
    void refreshLevels() noexcept { fm_.refreshLevels(); }
    void refreshLfo() noexcept
    {
        if (patch_)
            lfo1_.setRate(patch_->lfo1.rateHz);
    }
    void refreshEnvelopes() noexcept
    {
        fm_.refreshEnvelopes();
        if (patch_)
            filterEnv_.setParams(patch_->filter.env);
    }

    /// One sample. `ctl` carries the Engine's live (smoothed) matrix (L2.4) and filter controls (L1.10).
    [[nodiscard]] float tick(const VoiceControl& ctl) noexcept
    {
        // Vibrato (L1.11): depth in semitones, so it sounds the same on every note. The patch's depth waits
        // for LFO 1's delay/fade; the mod wheel's is immediate (the player asked for it now).
        lfo1_.setShape(patch_->lfo1.shape);
        const float lfo1 = lfo1_.tick();
        const ModRoutes& r = patch_->routes;
        if (glide_ != 0.0f) // portamento: one constant step per sample toward 0, landing exactly on it
            glide_ = std::abs(glide_) <= glideStep_ ? 0.0f : glide_ - std::copysign(glideStep_, glide_);
        // All pitch modulation meets here, in semitones: bend (L2.5) + glide (L2.5) + vibrato (L1.11).
        fm_.setPitchOffset(ctl.bendSemitones + glide_ +
                           lfo1 * (r.vibratoSemitones * lfo1_.fadeGain() + ctl.modWheel * r.wheelVibratoSemitones));

        float y = fm_.tick(*ctl.alg);
        const float env = filterEnv_.tick(); // always runs, so switching the filter on mid-note finds it in place
        if (const FilterParams& f = patch_->filter; f.mode != FilterMode::Off) {
            const float octaves = ctl.cutoffOctaves + f.envOctaves * env +
                                  f.keyTrack * (pitch() - kKeyBreakpoint) / 12.0f + // follows a glide smoothly
                                  f.velOctaves * (velocity_ - 1.0f);
            // Octaves → Hz, then the SVF prewarps (tan). Two transcendentals per sample per voice: measured
            // in L2.6. The SVF clamps the result to [1 Hz, 0.49·fs].
            svf_.set(util::pow2(octaves), ctl.q); // e^(x·ln2): ~14x cheaper than std::exp2 here (L2.6)
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
    [[nodiscard]] bool sustained() const noexcept { return sustained_; }
    [[nodiscard]] float fadeGain() const noexcept { return fadeGain_; }
    [[nodiscard]] std::uint8_t key() const noexcept { return fm_.key(); }
    [[nodiscard]] std::int32_t noteId() const noexcept { return noteId_; }
    [[nodiscard]] std::uint64_t age() const noexcept { return age_; }

private:
    FmVoice fm_;
    dsp::Adsr filterEnv_;
    dsp::Svf svf_;
    dsp::Lfo lfo1_;
    float velocity_ = 1.0f;
    float glide_ = 0.0f, glideStep_ = 0.0f; // remaining glide offset (semitones) and its per-sample step
    bool sustained_ = false;                // key up, held by the sustain pedal
    const Patch* patch_ = nullptr;
    bool busy_ = false, released_ = false, fading_ = false;
    float fadeGain_ = 1.0f, fadeStep_ = 1.0f;
    std::uint64_t age_ = 0; // note-on counter value when started: smaller = older
    std::int32_t noteId_ = -1;
};

} // namespace fss::synth
