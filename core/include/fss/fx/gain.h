// fss/fx/gain.h: a smoothed gain in dB, L5.1's other trivial effect.
//
// The chain uses it as its last stage: the patch's Output level (the parameter that used to be applied inside
// the Engine's voice loop). It satisfies StereoEffect like any other effect, so it could also sit in an FxSlot
// (the tests do exactly that, to check the slot's fades with the simplest possible effect).
//
// The arithmetic is kept identical to the pre-L5.1 Engine (`gain.tick() * sample`, gain smoothed linearly
// over 10 ms toward dbToGain(dB)), so moving it out of the Engine changed no output bit (l1.7 hash).
#pragma once

#include <fss/dsp/smoother.h>
#include <fss/fx/effect.h>
#include <fss/util/math.h>

namespace fss::fx {

class Gain {
public:
    static constexpr bool kSpillsTail = false;

    /// smoothingMs 0 = jump (the Engine's A/B "no smoothing" mode).
    void prepare(double sampleRate, int /*maxBlock*/, float smoothingMs = 10.0f) { gain_.prepare(sampleRate, smoothingMs); }
    void reset() noexcept { gain_.reset(gain_.target()); }
    void setDb(float db) noexcept { gain_.setTarget(util::dbToGain(db)); }
    void snapDb(float db) noexcept { gain_.reset(util::dbToGain(db)); }
    [[nodiscard]] int tailSamples() const noexcept { return 0; }

    void process(StereoBlock b) noexcept
    {
        for (int i = 0; i < b.n; ++i) {
            const float g = gain_.tick(); // one tick per sample, shared by both channels
            b.L[i] = g * b.L[i];
            b.R[i] = g * b.R[i];
        }
    }

private:
    dsp::OnePoleSmoother gain_{};
};

static_assert(StereoEffect<Gain>);

} // namespace fss::fx
