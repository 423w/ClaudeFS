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
        for (int i = 0; i < kNumOps; ++i) {
            const OperatorParams& p = patch.ops[static_cast<std::size_t>(i)];
            Operator& o = ops_[static_cast<std::size_t>(i)];
            o.setFrequency(operatorFrequencyHz(patch, p, note.key, note.bend));
            o.setLevel(operatorLevel(p, note.key, note.velocity));
            o.setEnvelope(scaledEnvelope(p, note.key));
        }
        noteOn();
    }

    /// Pitch bend while the note sounds: frequencies change, phases don't (no clicks, L1.2).
    void setBend(float bend) noexcept
    {
        if (!patch_)
            return;
        for (int i = 0; i < kNumOps; ++i)
            ops_[static_cast<std::size_t>(i)].setFrequency(
                operatorFrequencyHz(*patch_, patch_->ops[static_cast<std::size_t>(i)], key_, bend));
    }

    [[nodiscard]] std::uint8_t key() const noexcept { return key_; }

    void prepare(double sampleRate) noexcept
    {
        for (Operator& op : ops_)
            op.prepare(sampleRate);
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
    [[nodiscard]] float tick(const Algorithm& alg) noexcept
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
    const Patch* patch_ = nullptr; // set by start(); read by setBend()
    std::uint8_t key_ = 0;
    std::array<Operator, kNumOps> ops_{};
    std::array<float, kNumOps> out_{}, prev_{}, prev2_{};
};

} // namespace fss::synth
