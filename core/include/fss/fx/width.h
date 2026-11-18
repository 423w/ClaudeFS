// fss/fx/width.h: stereo width by mid/side scaling, L5.1's first trivial effect.
//
// Any stereo pair can be rewritten as what both channels share (MID) and how they differ (SIDE):
//     M = (L + R)/2        S = (L − R)/2        and back:  L = M + S,  R = M − S
// Scaling S scales the width: w = 0 leaves only M (mono, L = R), w = 1 changes nothing, w = 2 doubles the
// difference (wider; past ~150 % it starts to sound hollow, because whatever is panned centre gets
// relatively quieter and anything out of phase gets louder).
//
// Note what width CAN'T do: make a mono signal stereo. With L = R, S = 0, and 0 × anything is still 0. Until
// chorus (L5.4), delay ping-pong (L5.3) or unison (L4.4) put a difference between the channels, CFS's voices
// are mono and this effect is silent in its effect. It's last in the chain for that reason: it widens
// whatever the other effects made stereo.
#pragma once

#include <fss/dsp/smoother.h>
#include <fss/fx/effect.h>
#include <fss/fx/fx_params.h>

namespace fss::fx {

class StereoWidth {
public:
    static constexpr bool kSpillsTail = false; // no memory: off means back to the dry signal at once

    void prepare(double sampleRate, int /*maxBlock*/) { width_.prepare(sampleRate, 20.0f); }
    void reset() noexcept { width_.reset(width_.target()); }
    void setParams(const WidthParams& p) noexcept { width_.setTarget(p.widthPct * 0.01f); }
    [[nodiscard]] int tailSamples() const noexcept { return 0; }

    void process(StereoBlock b) noexcept
    {
        for (int i = 0; i < b.n; ++i) {
            const float w = width_.tick(); // smoothed per sample: a width sweep doesn't zipper
            const float m = 0.5f * (b.L[i] + b.R[i]);
            const float s = 0.5f * (b.L[i] - b.R[i]) * w;
            b.L[i] = m + s;
            b.R[i] = m - s;
        }
    }

private:
    dsp::OnePoleSmoother width_{};
};

static_assert(StereoEffect<StereoWidth>);

} // namespace fss::fx
