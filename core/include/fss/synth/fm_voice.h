// fss/synth/fm_voice.h: four operators executing an Algorithm (L1.5).
//
// One sample:
//   for dst = A..D:
//       pm = Σ_src mod[src][dst] · (src < dst ? out[src] this sample : back-edge value of src)
//       out[dst] = op[dst].tick(pm)
//   y = Σ carrier[i] · out[i]
//
// Back edges (feedback) read the AVERAGE of the source's last two outputs, as the DX7 does
// ([SHIRRIFF-DX7]). Plain one-sample feedback tends to lock into an oscillation at fs/2 at high
// amounts (the output flips sign every sample and feeds that back); averaging two samples is a cheap
// low-pass that cancels exactly that fs/2 component, so strong feedback becomes noise-like instead of a
// harsh Nyquist buzz.
//
// Real-time safe: fixed-size arrays, no allocation; the matrix loop is 16 multiply-adds that skip
// zero entries (most presets have 1–3 edges).
#pragma once

#include <fss/synth/algorithm.h>
#include <fss/synth/operator.h>
#include <fss/synth/patch.h>
#include <fss/util/inline.h>
#include <fss/util/math.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace fss::synth {

// ---- note → operator settings (L1.6; formulas in patch.h) ---------------------------------------

/// The note's own frequency: tuning, key and pitch bend (bend −1..1 × bendRange semitones).
[[nodiscard]] inline double noteFrequencyHz(const Patch& patch, std::uint8_t key, float bend) noexcept
{
    const double semis = static_cast<double>(key) + static_cast<double>(bend) * patch.bendRange.value - 69.0;
    return patch.tuningA4.value * std::exp2(semis / 12.0);
}

[[nodiscard]] inline double operatorFrequencyHz(const Patch& patch, const OperatorParams& op, std::uint8_t key,
                                                float bend) noexcept
{
    if (op.fixed)
        return op.fixedHz.value;
    const util::Hz note{noteFrequencyHz(patch, key, bend)};
    return (note * op.ratio * util::toRatio(op.detune)).value;
}

/// Octaves from the key-scaling breakpoint (positive above middle C).
[[nodiscard]] inline float octavesFromBreakpoint(std::uint8_t key) noexcept
{
    return (static_cast<float>(key) - kKeyBreakpoint) / 12.0f;
}

[[nodiscard]] inline float operatorLevel(const OperatorParams& op, std::uint8_t key, float velocity) noexcept
{
    const float v = std::clamp(velocity, 0.0f, 1.0f);
    const float velDb = op.velSens * kVelocityRangeDb * (v - 1.0f);                       // ≤ 0 dB
    const float keyDb = -op.keyLevelDbPerOct * std::max(0.0f, octavesFromBreakpoint(key)); // only above C4
    return op.level * util::dbToGain(velDb + keyDb);
}

[[nodiscard]] inline dsp::AdsrParams scaledEnvelope(const OperatorParams& op, std::uint8_t key) noexcept
{
    // Rate scaling: every time is multiplied by 2^(−scale·octaves). Like a real string: higher notes die sooner.
    const float k = std::exp2(-op.keyRateScale * octavesFromBreakpoint(key));
    dsp::AdsrParams e = op.env;
    e.attackMs *= k;
    e.decayMs *= k;
    e.releaseMs *= k;
    return e;
}

class FmVoice {
public:
    /// Configure every operator for this note from the patch, then start it (gate on). The patch must
    /// outlive the note (the Engine owns it), because setBend() reads it again.
    void start(const NoteContext& note, const Patch& patch) noexcept
    {
        patch_ = &patch;
        key_ = note.key;
        velocity_ = note.velocity;
        bend_ = note.bend;
        pitchOffset_ = 0.0f;
        pitchRatio_ = 1.0;
        opPitch_.fill(0.0f);
        for (int i = 0; i < kNumOps; ++i) {
            ops_[static_cast<std::size_t>(i)].setLevelMod(1.0f);
            ops_[static_cast<std::size_t>(i)].setPitchRatio(1.0); // no leftover modulation from the slot's last note
            const OperatorParams& p = patch.ops[static_cast<std::size_t>(i)];
            Operator& o = ops_[static_cast<std::size_t>(i)];
            o.setFrequency(operatorFrequencyHz(patch, p, note.key, note.bend));
            o.setLevel(operatorLevel(p, note.key, note.velocity));
            o.setEnvelope(scaledEnvelope(p, note.key));
            o.setWave(static_cast<dsp::OpWave>(std::min<int>(p.wave, dsp::kNumOpWaves - 1)));
        }
        noteOn();
    }

    /// Pitch bend while the note sounds: frequencies change, phases don't (no clicks, L1.2).
    void setBend(float bend) noexcept
    {
        bend_ = bend;
        refreshFrequencies();
    }

    // ---- live parameter changes while the note sounds (L2.4). The patch was edited in place by the
    // Engine; these re-derive this note's operator settings from it. ----------------------------------
    /// Ratio, detune, fixed mode, tuning, bend range: new frequencies; phases continue (no clicks).
    void refreshFrequencies() noexcept
    {
        if (!patch_)
            return;
        for (int i = 0; i < kNumOps; ++i)
            ops_[static_cast<std::size_t>(i)].setFrequency(
                operatorFrequencyHz(*patch_, patch_->ops[static_cast<std::size_t>(i)], key_, bend_));
        applyPitchRatio(); // "fixed" may have changed: fixed-frequency operators ignore pitch modulation
    }

    /// Per-sample pitch modulation of the whole note, in semitones (vibrato, glide). Fixed-frequency
    /// operators ignore it, as they ignore the key and the bend. Cheap when nothing changes: the exp2 and the
    /// four increment updates only run when the offset actually moved.
    void setPitchOffset(float semitones) noexcept
    {
        if (semitones == pitchOffset_)
            return;
        pitchOffset_ = semitones;
        pitchRatio_ = util::pow2(static_cast<double>(semitones) / 12.0); // per sample: the fast exp (L2.6)
        applyPitchRatio();
    }
    /// The same, with the ratio 2^(semitones/12) already computed by the caller (L4.4 open item, fixed in L4.2:
    /// a unison voice computes its own pitch ratio once and multiplies each copy's cached detune ratio in, instead
    /// of a pow2 per copy per sample). `semitones` is only the cache key.
    void setPitchOffsetRatio(float semitones, double ratio) noexcept
    {
        if (semitones == pitchOffset_)
            return;
        pitchOffset_ = semitones;
        pitchRatio_ = ratio;
        applyPitchRatio();
    }
    [[nodiscard]] float pitchOffset() const noexcept { return pitchOffset_; }

    /// Mod matrix (L4.1): one operator's own pitch offset (semitones, on top of the voice's) and level
    /// multiplier. Cheap when nothing changes: the ratio is only recomputed when the offset moves.
    void setOpPitch(int i, float semitones) noexcept
    {
        float& o = opPitch_[static_cast<std::size_t>(i)];
        if (semitones == o)
            return;
        o = semitones;
        applyPitchRatio();
    }
    void setOpLevelMod(int i, float m) noexcept { ops_[static_cast<std::size_t>(i)].setLevelMod(m); }

    /// Unison (L4.4): give every operator a random start phase (seeded, so renders reproduce), and forget the
    /// feedback history, so a copy doesn't start in phase with the others.
    void randomizePhases(std::uint32_t seed) noexcept
    {
        std::uint32_t x = seed ? seed : 0x9E3779B9u;
        for (Operator& op : ops_) {
            x ^= x << 13; // xorshift32 (util/random.h's generator, inlined to keep this header light)
            x ^= x >> 17;
            x ^= x << 5;
            op.setPhase(x);
        }
        prev_ = prev2_ = {};
    }
    /// Level, velocity sensitivity, key level: the new level is a *target*, reached by the operator's smoother.
    void refreshLevels() noexcept
    {
        if (!patch_)
            return;
        for (int i = 0; i < kNumOps; ++i)
            ops_[static_cast<std::size_t>(i)].setLevelSmoothed(
                operatorLevel(patch_->ops[static_cast<std::size_t>(i)], key_, velocity_));
    }
    /// Waveforms (L4.2): switch at once (a waveform change is a timbre step, like a menu).
    void refreshWaves() noexcept
    {
        if (!patch_)
            return;
        for (int i = 0; i < kNumOps; ++i)
            ops_[static_cast<std::size_t>(i)].setWave(
                static_cast<dsp::OpWave>(std::min<int>(patch_->ops[static_cast<std::size_t>(i)].wave, dsp::kNumOpWaves - 1)));
    }
    /// Envelope times/sustain and rate scaling: take effect immediately (sustain changes glide inside Adsr).
    void refreshEnvelopes() noexcept
    {
        if (!patch_)
            return;
        for (int i = 0; i < kNumOps; ++i)
            ops_[static_cast<std::size_t>(i)].setEnvelope(scaledEnvelope(patch_->ops[static_cast<std::size_t>(i)], key_));
    }

    [[nodiscard]] std::uint8_t key() const noexcept { return key_; }
    [[nodiscard]] float velocity() const noexcept { return velocity_; }

    /// Legato (L2.5): a new key for the sounding note. Frequencies follow (phases continue); levels,
    /// velocity and envelope scaling keep the values the note started with (it's still the same "strike").
    void setKey(std::uint8_t key) noexcept
    {
        key_ = key;
        refreshFrequencies();
    }

    void prepare(double sampleRate, float levelSmoothingMs = 5.0f) noexcept
    {
        for (Operator& op : ops_)
            op.prepare(sampleRate, levelSmoothingMs);
    }

    void reset() noexcept
    {
        for (Operator& op : ops_)
            op.reset();
        out_ = prev_ = prev2_ = {};
    }

    [[nodiscard]] Operator& op(int i) noexcept { return ops_[static_cast<std::size_t>(i)]; }

    void noteOn() noexcept
    {
        for (Operator& op : ops_)
            op.noteOn();
        // Clear feedback history too, so a retriggered note starts the same way every time.
        prev_ = prev2_ = {};
    }

    void noteOff() noexcept
    {
        for (Operator& op : ops_)
            op.noteOff();
    }

    /// One sample of the voice under algorithm `alg`.
    [[nodiscard]] FSS_FORCEINLINE float tick(const Algorithm& alg) noexcept // inlined: see util/inline.h
    {
        for (int dst = 0; dst < kNumOps; ++dst) {
            float pm = 0.0f;
            for (int src = 0; src < kNumOps; ++src) {
                const float amount = alg.mod[static_cast<std::size_t>(src)][static_cast<std::size_t>(dst)];
                if (amount == 0.0f)
                    continue;
                const auto s = static_cast<std::size_t>(src);
                const float in = (src < dst) ? out_[s]                          // already computed this sample
                                             : 0.5f * (prev_[s] + prev2_[s]);   // back edge: last two samples
                pm += amount * in;
            }
            out_[static_cast<std::size_t>(dst)] = ops_[static_cast<std::size_t>(dst)].tick(pm);
        }

        float y = 0.0f;
        for (std::size_t i = 0; i < kNumOps; ++i)
            y += alg.carrier[i] * out_[i];
        prev2_ = prev_;
        prev_ = out_;
        return y;
    }

    /// True while any *carrier* is still sounding. Modulators alone are inaudible, so a voice whose
    /// carriers have all finished their release can be freed even if a modulator envelope is still going.
    [[nodiscard]] bool isActive(const Algorithm& alg) const noexcept
    {
        for (std::size_t i = 0; i < kNumOps; ++i)
            if (alg.carrier[i] != 0.0f && ops_[i].isActive())
                return true;
        return false;
    }

private:
    void applyPitchRatio() noexcept
    {
        if (!patch_)
            return;
        const double r = pitchRatio_; // 2^(pitchOffset/12), computed where the offset was set
        for (std::size_t i = 0; i < kNumOps; ++i) {
            // An operator's own matrix pitch (L4.1) multiplies in only when it's set: r · 1 would be exact too,
            // but skipping the pow2 keeps the unmodulated cost where it was.
            const double own = opPitch_[i] != 0.0f ? util::pow2(static_cast<double>(opPitch_[i]) / 12.0) : 1.0;
            ops_[i].setPitchRatio(patch_->ops[i].fixed ? 1.0 : (opPitch_[i] != 0.0f ? r * own : r));
        }
    }

    const Patch* patch_ = nullptr; // set by start(); read again by the refresh functions
    float pitchOffset_ = 0.0f;     // semitones, from setPitchOffset()
    double pitchRatio_ = 1.0;      // 2^(pitchOffset_/12)
    std::array<float, kNumOps> opPitch_{}; // per-operator semitones from the mod matrix (L4.1)
    std::uint8_t key_ = 0;
    float velocity_ = 1.0f, bend_ = 0.0f;
    std::array<Operator, kNumOps> ops_{};
    std::array<float, kNumOps> out_{}, prev_{}, prev2_{};
};

} // namespace fss::synth
