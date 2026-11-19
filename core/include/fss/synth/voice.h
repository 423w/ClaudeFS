// fss/synth/voice.h: one slot in the Engine's voice pool: an FmVoice, the voice's filter (L1.10), plus
// the bookkeeping the allocator needs (key, age, released?, being stolen?) and a short "steal fade" (L1.7, D8).
//
//   LFO 1 (vibrato) ─► pitch
//   FmVoice (4 operators) ──► Svf (mode, cutoff from base + filter env + key + velocity) ──► steal fade ──►
// Since L4.1 every modulation goes through the mod matrix (mod_matrix.h): the voice fills in its own sources
// (LFO 1, envelopes, velocity, key, random), adds the per-voice slots to the Engine's global sums, and applies
// the result to pitch, cutoff, resonance, index, amp, LFO 1 rate and each operator's level and pitch.
// Since L4.4 the FM core can run as 1–8 UNISON copies, detuned and panned, so a voice is stereo:
//
//   FmVoice copy 0 ─┐  detune d₀, pan p₀, gain g₀
//   FmVoice copy 1 ─┼─► Σ (power-normalized, constant-power pan) ─► SVF L / SVF R ─► amp, pan ─► steal fade
//   ...           ──┘
// With one centred copy (unison 1, nothing panning) the voice takes the old mono path, bit for bit.
#pragma once

#include <fss/dsp/adsr.h>
#include <fss/dsp/halfband.h>
#include <fss/dsp/lfo.h>
#include <fss/dsp/svf.h>
#include <fss/synth/fm_voice.h>
#include <fss/synth/mod_matrix.h>
#include <fss/util/inline.h>
#include <fss/util/math.h>
#include <fss/util/random.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace fss::synth {

/// Per-sample values the Engine computes ONCE and every voice reads (L1.10): the live (smoothed) modulation
/// matrix and the smoothed global filter controls. Computing them per voice would multiply the smoothing
/// cost by 20, and they'd still have to agree across voices.
struct VoiceControl {
    const Algorithm* alg = nullptr; // live matrix (L2.4), scaled by the GLOBAL index modulation (LFO 2...)
    float cutoffOctaves = 0.0f; // log2(base cutoff in Hz), smoothed, plus the global cutoff modulation
    float q = 0.7071f;          // resonance, smoothed
    float modWheel = 0.0f;      // 0..1, smoothed (L1.11)
    float bendSemitones = 0.0f; // pitch bend × bend range, smoothed (L2.5): a pitch offset like vibrato
    // The mod matrix (L4.1). The Engine fills the global sources and sums the global slots once per sample.
    const Algorithm* liveAlg = nullptr;   // the unscaled live matrix, for a voice that scales the index itself
    const ModValues* sources = &kNoModSources; // global source values ([None] = 1; per-voice entries unused)
    const ModSums* global = &kNoModSums;       // the global slots' sums, per destination (cutoff/index already applied)
    const ModRouting* routing = &kNoModRouting; // the per-voice slots
};

inline constexpr int kMaxUnison = 8;

/// One sample of a stereo voice (L4.4).
struct StereoSample {
    float l, r;
};

/// Where unison copy k of n sits, −1..1: evenly spaced, the INNER positions first (copy 0 nearest the centre),
/// so adding or removing copies moves copy 0 as little as possible.
///   n = 3: 0, −1, +1     n = 4: −⅓, +⅓, −1, +1     n = 2: −1, +1
[[nodiscard]] constexpr float unisonPosition(int k, int n) noexcept
{
    if (n <= 1)
        return 0.0f;
    const float step = 2.0f / static_cast<float>(n - 1);
    if (n % 2 == 1) {
        if (k == 0)
            return 0.0f;
        const float p = static_cast<float>((k + 1) / 2) * step;
        return (k % 2 == 1) ? -p : p;
    }
    const float p = (static_cast<float>(k / 2) + 0.5f) * step;
    return (k % 2 == 0) ? -p : p;
}

// FilterMode is Off followed by the SvfMode values in the same order (tick() relies on it).
static_assert(static_cast<int>(FilterMode::LowPass) == static_cast<int>(dsp::SvfMode::LowPass) + 1 &&
              static_cast<int>(FilterMode::Notch) == static_cast<int>(dsp::SvfMode::Notch) + 1);

class Voice {
public:
    /// fmFactor (L4.2): 1, or 2 = the FM core (every unison copy's operators) runs at twice the sample rate and
    /// a halfband decimator brings it down; everything after it (filter, amp, fade) stays at the sample rate.
    void prepare(double sampleRate, float stealFadeMs, float levelSmoothingMs = 5.0f, int fmFactor = 1) noexcept
    {
        fmFactor_ = fmFactor == 2 ? 2 : 1;
        for (FmVoice& f : fm_)
            f.prepare(sampleRate * fmFactor_, levelSmoothingMs);
        for (dsp::Halfband<63>* d : {&downL_, &downR_})
            d->design(9.6);
        for (UnisonCopy& c : copy_) { // 20 ms: a count change fades a copy in or out; detune/pan glide
            c.gain.prepare(sampleRate, 20.0f);
            c.detune.prepare(sampleRate, 20.0f);
            c.pan.prepare(sampleRate, 20.0f);
        }
        filterEnv_.prepare(sampleRate);
        modEnv_.prepare(sampleRate);
        svf_.prepare(sampleRate);
        svfR_.prepare(sampleRate);
        lfo1_.prepare(sampleRate);
        // Linear fade length; at least 1 sample so a "0 ms" fade is an instant cut, not a divide by zero.
        fadeStep_ = 1.0f / std::max(1.0f, stealFadeMs * 0.001f * static_cast<float>(sampleRate));
        reset();
    }

    void reset() noexcept
    {
        for (FmVoice& f : fm_)
            f.reset();
        resetDecimators();
        filterEnv_.reset();
        modEnv_.reset();
        svf_.reset();
        svfR_.reset();
        copies_ = target_ = 1;
        stereo_ = false;
        host_.fill(0.0f);
        lfoRateMod_ = 0.0f;
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
        resetDecimators(); // L4.2: a 2x voice must not replay the slot's previous note through its filter
        // Unison (L4.4): every copy starts the note; copy 0 at phase 0 (as before L4.4), the others at random
        // phases, so N copies don't start as one N-times-louder waveform. The layout snaps (no glide).
        target_ = copies_ = std::clamp<int>(patch.unison.voices, 1, kMaxUnison);
        for (int k = 0; k < copies_; ++k) {
            fm_[static_cast<std::size_t>(k)].start(note, patch);
            if (k > 0)
                fm_[static_cast<std::size_t>(k)].randomizePhases(static_cast<std::uint32_t>(age * 2654435761u + 977u * static_cast<std::uint32_t>(k)));
        }
        layoutUnison(patch.unison, true);
        stereo_ = false;
        // The filter's per-note offset (key tracking + velocity) is fixed for the note, in octaves; it is
        // re-read in tick() through the patch so live knob changes still apply (cheap: two multiplies).
        velocity_ = note.velocity;
        filterEnv_.setParams(patch.filter.env);
        filterEnv_.noteOn();
        modEnv_.setParams(patch.mod.env); // L4.1
        modEnv_.noteOn();
        // A random value per note (L4.1), from the note counter: deterministic for renders, different per note.
        util::XorShift32 rng(static_cast<std::uint32_t>(age * 2246822519u + 1u));
        (void)rng.next();
        random_ = rng.bipolar();
        host_.fill(0.0f);   // the host's per-note modulation belongs to the note before; a new note starts clean
        lfoRateMod_ = 0.0f;
        opModActive_ = false; // fm_.start() reset every operator's offsets
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
            for (int k = 0; k < copies_; ++k)
                fm_[static_cast<std::size_t>(k)].noteOff();
            filterEnv_.noteOff();
            modEnv_.noteOff();
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
        for (int k = 0; k < copies_; ++k)
            fm_[static_cast<std::size_t>(k)].setKey(key);
        glideFrom(from - static_cast<float>(key), glideSamples);
    }

    /// Current pitch in semitones (MIDI key numbers), including an unfinished glide; excludes bend and vibrato.
    [[nodiscard]] float pitch() const noexcept { return static_cast<float>(fm_[0].key()) + glide_; }

    /// Steal: fade to silence over the steal-fade time, then become free. Never an instant cut (click).
    void kill() noexcept
    {
        if (busy_)
            fading_ = true;
    }

    void refreshFrequencies() noexcept
    {
        for (int k = 0; k < copies_; ++k)
            fm_[static_cast<std::size_t>(k)].refreshFrequencies();
    }
    void refreshLevels() noexcept
    {
        for (int k = 0; k < copies_; ++k)
            fm_[static_cast<std::size_t>(k)].refreshLevels();
    }
    void refreshWaves() noexcept // L4.2
    {
        for (int k = 0; k < copies_; ++k)
            fm_[static_cast<std::size_t>(k)].refreshWaves();
    }

    /// Unison settings changed while the note sounds (L4.4): new copies start as clones of copy 0 (same
    /// envelope state, own random phases) and fade in; removed ones fade out; detune and pan glide.
    void refreshUnison() noexcept
    {
        if (!patch_)
            return;
        const int n = std::clamp<int>(patch_->unison.voices, 1, kMaxUnison);
        for (int k = copies_; k < n; ++k) { // copies that aren't running yet: clone copy 0
            fm_[static_cast<std::size_t>(k)] = fm_[0];
            fm_[static_cast<std::size_t>(k)].randomizePhases(static_cast<std::uint32_t>(age_ * 2654435761u + 977u * static_cast<std::uint32_t>(k) + 13u));
            copy_[static_cast<std::size_t>(k)].gain.reset(0.0f);
        }
        target_ = n;
        copies_ = std::max(copies_, n);
        layoutUnison(patch_->unison, false);
    }
    void refreshLfo() noexcept
    {
        if (patch_)
            lfo1_.setRate(lfoRateMod_ != 0.0f ? patch_->lfo1.rateHz * util::pow2(lfoRateMod_) : patch_->lfo1.rateHz);
    }
    void refreshEnvelopes() noexcept
    {
        for (int k = 0; k < copies_; ++k)
            fm_[static_cast<std::size_t>(k)].refreshEnvelopes();
        if (patch_) {
            filterEnv_.setParams(patch_->filter.env);
            modEnv_.setParams(patch_->mod.env);
        }
    }

    /// CLAP polyphonic modulation (L4.1): the host's offset for this note, in the destination's unit
    /// (only Cutoff and Resonance are host-modulatable). Replaces the previous value for that destination.
    void setHostMod(ModDst d, float value) noexcept { host_[idx(d)] = value; }

    /// One sample. `ctl` carries the Engine's live (smoothed) matrix (L2.4) and filter controls (L1.10).
    [[nodiscard]] FSS_FORCEINLINE StereoSample tick(const VoiceControl& ctl) noexcept // inlined: util/inline.h
    {
        // 1. This voice's modulation sources (L4.1). Envelopes and LFO 1 always run, so a slot switched on
        //    mid-note finds them in place.
        lfo1_.setShape(patch_->lfo1.shape);
        const float lfo1 = lfo1_.tick();
        const float env = filterEnv_.tick();
        const float modEnv = modEnv_.tick();
        // 2. The per-voice slots (the patch Vibrato and Wheel > vib routes are two of them: LFO 1 × fade and
        //    LFO 1 × wheel → pitch), plus the host's per-note modulation. Destinations nobody modulates stay
        //    exactly 0, and adding an exact 0 changes no bit (the golden test). With no per-voice slot at all
        //    the source table isn't even filled in (most patches: measured in the L4.1 journal).
        ModSums v{};
        if (const ModRouting& rt = *ctl.routing; rt.numVoice > 0) {
            ModValues src = *ctl.sources; // the global ones (LFO 2, wheel, bend) and [None] = 1
            const std::uint32_t used = rt.voiceSources; // fill in only what a slot reads (measured, L4.1)
            auto uses = [used](ModSrc s) { return (used & (1u << idx(s))) != 0; };
            src[idx(ModSrc::Lfo1)] = lfo1;
            if (uses(ModSrc::Lfo1Fade))
                src[idx(ModSrc::Lfo1Fade)] = lfo1_.fadeGain();
            src[idx(ModSrc::ModEnv)] = modEnv;
            src[idx(ModSrc::FilterEnv)] = env;
            src[idx(ModSrc::Velocity)] = velocity_;
            if (uses(ModSrc::Key))
                src[idx(ModSrc::Key)] = (pitch() - kKeyBreakpoint) / 64.0f;
            src[idx(ModSrc::Random)] = random_;
            accumulate(rt.voice.data(), rt.numVoice, src, v);
        }
        const std::uint32_t touched = ctl.routing->touched;
        v[idx(ModDst::Cutoff)] += host_[idx(ModDst::Cutoff)];
        v[idx(ModDst::Resonance)] += host_[idx(ModDst::Resonance)];
        const ModSums& g = *ctl.global;
        auto both = [&](ModDst d) { return g[idx(d)] + v[idx(d)]; };

        if (glide_ != 0.0f) // portamento: one constant step per sample toward 0, landing exactly on it
            glide_ = std::abs(glide_) <= glideStep_ ? 0.0f : glide_ - std::copysign(glideStep_, glide_);
        // All pitch modulation meets here, in semitones: bend (L2.5) + glide (L2.5) + the matrix (vibrato...).
        const float pitchOffset = ctl.bendSemitones + glide_ + g[idx(ModDst::Pitch)] + v[idx(ModDst::Pitch)];
        // Operators: only while a slot reaches one (and once more after the last such slot goes, to put them
        // back to unmodulated: otherwise they'd keep the last offset).
        const bool opsNow = (touched & kOpDestBits) != 0;
        if (opsNow || opModActive_)
            for (int k = 0; k < kNumOps; ++k) {
                const auto d = static_cast<std::size_t>(k);
                const float op = both(static_cast<ModDst>(idx(ModDst::OpAPitch) + d));
                const float lv = both(static_cast<ModDst>(idx(ModDst::OpALevel) + d));
                for (int c = 0; c < copies_; ++c) {
                    fm_[static_cast<std::size_t>(c)].setOpPitch(k, op);
                    fm_[static_cast<std::size_t>(c)].setOpLevelMod(k, lv != 0.0f ? std::max(0.0f, 1.0f + lv) : 1.0f);
                }
            }
        opModActive_ = opsNow;
        if (const float rate = both(ModDst::Lfo1Rate); rate != lfoRateMod_) {
            lfoRateMod_ = rate;
            refreshLfo();
        }

        // 3. The FM index: the Engine already scaled ctl.alg by the GLOBAL index modulation; a per-voice one
        //    needs this voice's own copy of the matrix.
        const Algorithm* alg = ctl.alg;
        if (const float vi = v[idx(ModDst::Index)]; vi != 0.0f) {
            const float k = std::max(0.0f, 1.0f + g[idx(ModDst::Index)] + vi);
            scaled_ = *ctl.liveAlg;
            for (auto& row : scaled_.mod)
                for (float& m : row)
                    m *= k;
            alg = &scaled_;
        }

        // 4. The FM core: one copy, centred and unpanned (the voice of before L4.4, bit for bit), or the unison
        //    copies, each detuned and panned, summed into a stereo pair (L4.4).
        const float panMod = both(ModDst::Pan);
        float yl = 0.0f, yr = 0.0f;
        const bool mono = copies_ == 1 && unisonCentred() && panMod == 0.0f;
        if (mono) {
            fm_[0].setPitchOffset(pitchOffset);
            if (fmFactor_ == 1) {
                yl = yr = fm_[0].tick(*alg);
            } else { // L4.2: two FM samples at 2x, one out of the decimator
                const float e = fm_[0].tick(*alg);
                const float o = fm_[0].tick(*alg);
                yl = yr = decimate(downL_, padL_, e, o);
            }
        } else {
            if (!stereo_) { // entering stereo: the right filter takes over the left one's state (no click)
                svfR_ = svf_;
                downR_ = downL_; // ... and the right decimator the left one's (L4.2)
                padR_ = padL_;
                stereo_ = true;
            }
            // Per copy, once per sample: its smoothers, pitch and pan gains (at 2x they serve both FM samples).
            std::array<float, kMaxUnison> gL{}, gR{};
            float power = 0.0f;
            double base = 0.0; // 2^(pitchOffset/12), computed at most once per sample, only if a copy needs it
            bool haveBase = false;
            for (int k = 0; k < copies_; ++k) {
                UnisonCopy& c = copy_[static_cast<std::size_t>(k)];
                const float gk = c.gain.tick(), dk = c.detune.tick(), pk = c.pan.tick();
                FmVoice& f = fm_[static_cast<std::size_t>(k)];
                // Pitch: the voice's ratio times the copy's detune ratio (cached: the detune only moves while the
                // unison layout glides). One pow2 per voice instead of one per copy (L4.4's open item).
                if (const float semis = pitchOffset + dk; semis != f.pitchOffset()) {
                    if (!haveBase) {
                        base = util::pow2(static_cast<double>(pitchOffset) / 12.0);
                        haveBase = true;
                    }
                    if (dk != c.lastDetune) {
                        c.detuneRatio = util::pow2(static_cast<double>(dk) / 12.0);
                        c.lastDetune = dk;
                    }
                    f.setPitchOffsetRatio(semis, base * c.detuneRatio);
                }
                // Constant-power pan, scaled by √2 so a centred copy has gain 1 (the mono level): see the journal.
                const float pan = std::clamp(pk + panMod, -1.0f, 1.0f);
                if (pan != c.lastPan) {
                    const float th = (1.0f + pan) * (std::numbers::pi_v<float> * 0.25f);
                    c.gl = std::numbers::sqrt2_v<float> * std::cos(th);
                    c.gr = std::numbers::sqrt2_v<float> * std::sin(th);
                    c.lastPan = pan;
                }
                gL[static_cast<std::size_t>(k)] = gk * c.gl;
                gR[static_cast<std::size_t>(k)] = gk * c.gr;
                power += gk * gk;
            }
            // Then the FM copies themselves: once at 1x, twice (and decimated) at 2x.
            auto sumCopies = [&](float& l, float& r) {
                l = r = 0.0f;
                for (int k = 0; k < copies_; ++k) {
                    const float y = fm_[static_cast<std::size_t>(k)].tick(*alg);
                    l += gL[static_cast<std::size_t>(k)] * y;
                    r += gR[static_cast<std::size_t>(k)] * y;
                }
            };
            if (fmFactor_ == 1) {
                sumCopies(yl, yr);
            } else {
                float l0 = 0.0f, r0 = 0.0f, l1 = 0.0f, r1 = 0.0f;
                sumCopies(l0, r0);
                sumCopies(l1, r1);
                yl = decimate(downL_, padL_, l0, l1);
                yr = decimate(downR_, padR_, r0, r1);
            }
            // Copies at random phases are uncorrelated: normalize by power, so N copies are as loud as one.
            const float norm = power > 1e-9f ? 1.0f / std::sqrt(power) : 0.0f;
            yl *= norm;
            yr *= norm;
            settleUnison();
        }
        if (const FilterParams& f = patch_->filter; f.mode != FilterMode::Off) {
            const float octaves = ctl.cutoffOctaves + f.envOctaves * env +
                                  f.keyTrack * (pitch() - kKeyBreakpoint) / 12.0f + // follows a glide smoothly
                                  f.velOctaves * (velocity_ - 1.0f) + v[idx(ModDst::Cutoff)];
            const float res = both(ModDst::Resonance);
            // Octaves → Hz, then the SVF prewarps (tan). Two transcendentals per sample per voice: measured
            // in L2.6. The SVF clamps the result to [1 Hz, 0.49·fs].
            const auto mode = static_cast<dsp::SvfMode>(static_cast<int>(f.mode) - 1); // FilterMode = SvfMode + 1
            svf_.set(util::pow2(octaves), res != 0.0f ? ctl.q * util::pow2(res) : ctl.q); // pow2: the fast exp (L2.6)
            if (mono) {
                yl = yr = svf_.tick(yl, mode);
            } else {
                svfR_.setFrom(svf_); // same coefficients, its own state
                yl = svf_.tick(yl, mode);
                yr = svfR_.tick(yr, mode);
            }
        }
        if (const float amp = both(ModDst::Amp); amp != 0.0f) {
            const float a = util::pow2(amp * (1.0f / 6.0206f)); // dB → gain: 6.02 dB per doubling
            yl *= a;
            yr *= a;
        }
        if (fading_) {
            fadeGain_ -= fadeStep_;
            if (fadeGain_ <= 0.0f) {
                fadeGain_ = 0.0f;
                busy_ = false; // silent: free the slot
            }
            yl *= fadeGain_;
            yr *= fadeGain_;
        } else if (!fm_[0].isActive(*ctl.alg)) {
            busy_ = false; // release finished on its own (every copy shares copy 0's envelopes)
        }
        return {yl, yr};
    }

    /// How many unison copies are running (L4.4; includes copies still fading out).
    [[nodiscard]] int unisonCopies() const noexcept { return copies_; }

    [[nodiscard]] bool busy() const noexcept { return busy_; }
    [[nodiscard]] bool released() const noexcept { return released_; }
    [[nodiscard]] bool fading() const noexcept { return fading_; }
    [[nodiscard]] bool sustained() const noexcept { return sustained_; }
    [[nodiscard]] float fadeGain() const noexcept { return fadeGain_; }
    [[nodiscard]] std::uint8_t key() const noexcept { return fm_[0].key(); }
    [[nodiscard]] std::int32_t noteId() const noexcept { return noteId_; }
    [[nodiscard]] std::uint64_t age() const noexcept { return age_; }

private:
    struct UnisonCopy {
        dsp::OnePoleSmoother gain{}, detune{}, pan{}; // gain 0..1, detune in semitones, pan −1..1
        float lastPan = 2.0f, gl = 1.0f, gr = 1.0f;   // cached pan gains (recomputed only when the pan moves)
        float lastDetune = 0.0f;                       // L4.2: the detune ratio's cache key ...
        double detuneRatio = 1.0;                      // ... and 2^(detune/12)
    };

    /// Targets for every copy from the unison settings; `snap` = jump there (note start).
    void layoutUnison(const UnisonParams& u, bool snap) noexcept
    {
        const int n = target_;
        for (int k = 0; k < kMaxUnison; ++k) {
            UnisonCopy& c = copy_[static_cast<std::size_t>(k)];
            const float pos = unisonPosition(k, n);
            if (k < n) {
                c.gain.setTarget(1.0f);
                c.detune.setTarget(pos * u.detuneCents * 0.005f); // ½ × spread, cents → semitones
                c.pan.setTarget(pos * u.widthPct * 0.01f);
            } else {
                c.gain.setTarget(0.0f); // fade out; keep its position while it fades
            }
            if (snap) {
                c.gain.reset(c.gain.target());
                c.detune.reset(c.detune.target());
                c.pan.reset(c.pan.target());
            }
        }
    }

    /// Copy 0 alone, at full gain, no detune, centred: the mono voice.
    [[nodiscard]] bool unisonCentred() const noexcept
    {
        const UnisonCopy& c = copy_[0];
        return c.gain.value() == 1.0f && c.detune.value() == 0.0f && c.pan.value() == 0.0f &&
               c.gain.target() == 1.0f && c.detune.target() == 0.0f && c.pan.target() == 0.0f;
    }

    /// Snap smoothers that have arrived (within 1e-5) and stop running copies that have faded out, so a voice
    /// back at unison 1 returns to the exact mono path and an unused copy stops costing CPU.
    void settleUnison() noexcept
    {
        for (int k = 0; k < copies_; ++k) {
            UnisonCopy& c = copy_[static_cast<std::size_t>(k)];
            for (dsp::OnePoleSmoother* s : {&c.gain, &c.detune, &c.pan})
                if (s->value() != s->target() && std::abs(s->value() - s->target()) < 1e-5f)
                    s->reset(s->target());
        }
        while (copies_ > target_ && copy_[static_cast<std::size_t>(copies_ - 1)].gain.value() < 1e-4f)
            --copies_;
        if (copies_ == 1 && unisonCentred())
            stereo_ = false; // back to mono: the next sample takes the mono path (L and R were equal by then)
    }

    /// One 2x-rate pair through a decimator, plus one 2x sample of padding: the halfband alone delays by 15.5
    /// base samples, the padding makes it a whole 16 (the latency the Engine reports, dsp/halfband.h).
    [[nodiscard]] static float decimate(dsp::Halfband<63>& d, float& pad, float even, float odd) noexcept
    {
        const float y = d.down(pad, even);
        pad = odd;
        return y;
    }
    void resetDecimators() noexcept
    {
        downL_.reset();
        downR_.reset();
        padL_ = padR_ = 0.0f;
    }

    std::array<FmVoice, kMaxUnison> fm_{}; // unison copies (L4.4); copy 0 is the voice of before L4.4
    int fmFactor_ = 1;                      // L4.2: the FM core's rate, 1x or 2x
    dsp::Halfband<63> downL_, downR_;       // L4.2: 2x → 1x, one per channel
    float padL_ = 0.0f, padR_ = 0.0f;
    std::array<UnisonCopy, kMaxUnison> copy_{};
    int copies_ = 1, target_ = 1; // copies running (incl. fading out), and the count asked for
    bool stereo_ = false;         // last sample took the stereo path (svfR_ in use)
    dsp::Svf svfR_;               // the right channel's filter (L4.4)
    dsp::Adsr filterEnv_;
    dsp::Adsr modEnv_;    // L4.1: a matrix source only
    float random_ = 0.0f; // L4.1: this note's random value, −1..1
    ModSums host_{};      // L4.1: the host's per-note modulation (CLAP PARAM_MOD), destination units
    float lfoRateMod_ = 0.0f; // L4.1: the LFO 1 rate modulation currently applied (octaves)
    bool opModActive_ = false; // L4.1: the operators had matrix offsets on the previous sample
    Algorithm scaled_{};  // L4.1: this voice's own index-scaled matrix, when a per-voice slot scales the index
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
