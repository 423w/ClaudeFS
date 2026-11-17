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

#include <array>

namespace fss::synth {

class FmVoice {
public:
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
    std::array<Operator, kNumOps> ops_{};
    std::array<float, kNumOps> out_{}, prev_{}, prev2_{};
};

} // namespace fss::synth
