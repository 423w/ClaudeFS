// fss/fx/flanger.h: a stereo feedback flanger (L5.4). [JOS-PASP] "comb filters", [REISS-FX] ch. 2.
//
// Mix a signal with itself delayed by τ and every frequency whose period fits a half-integer number of times
// into τ cancels: notches at (k + ½)/τ, peaks at k/τ. A COMB filter, its teeth evenly spaced in Hz. Sweep τ
// (0.5 to a few ms) with an LFO and the comb slides up and down the spectrum: the jet-plane "whoosh".
// Feedback (the delayed signal fed back into the delay) turns the comb's gentle peaks into sharp resonances
// (at k/τ) and deepens the notches. Negative feedback puts the resonances at (k + ½)/τ instead: on the wet path
// alone peaks and notches swap; in a 50 % mix they partly cancel, giving a flatter, hollower, "nasal" sound.
// (Measured in tests/test_modfx.cpp: at fb = ±0.5 the 1 ms comb's 1 kHz/500 Hz gains are 1.5/0.17 and 0.83/0.5.)
//
//   in ──┬─────────────────────────────── dry ──┐
//        └─(+)─► delay τ(t) ──┬──────────── wet ─┴─► out     τ(t) = delayMs + depthMs·½(1 + sin(2π·φ))
//           ▲                 │                              right channel: φ + 90°
//           └── feedback × ◄──┘
//
// Unlike the chorus, the delay is so short that the effect is heard as TIMBRE (filtering), not as copies.
// Through-zero flanging (τ passing through 0, needing the dry path delayed too) is a later experiment.
#pragma once

#include <fss/dsp/delay_line.h>
#include <fss/dsp/phasor.h>
#include <fss/dsp/smoother.h>
#include <fss/fx/chorus.h> // lfoSine
#include <fss/fx/effect.h>
#include <fss/fx/fx_params.h>

#include <algorithm>
#include <cmath>

namespace fss::fx {

class Flanger {
public:
    static constexpr bool kSpillsTail = false;
    static constexpr double kMaxDelayMs = 10.0, kMaxDepthMs = 10.0;

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sampleRate_ = sampleRate;
        const auto maxSamples = static_cast<std::size_t>(std::ceil((kMaxDelayMs + kMaxDepthMs) * 0.001 * sampleRate)) + 4;
        lineL_.prepare(maxSamples);
        lineR_.prepare(maxSamples);
        lfo_.prepare(sampleRate);
        for (auto* s : {&depth_, &delay_, &feedback_, &dry_, &wet_})
            s->prepare(sampleRate, 20.0f);
        setParams(params_);
        reset();
    }

    void reset() noexcept
    {
        lineL_.reset();
        lineR_.reset();
        lfo_.reset(0);
            for (auto* s : {&depth_, &delay_, &feedback_, &dry_, &wet_})
            s->reset(s->target());
    }

    void setParams(const FlangerParams& p) noexcept
    {
        params_ = p;
        lfo_.setFrequency(std::clamp(static_cast<double>(p.rateHz), 0.01, 20.0));
        const double ms = 0.001 * sampleRate_;
        depth_.setTarget(static_cast<float>(std::clamp(static_cast<double>(p.depthMs), 0.0, kMaxDepthMs) * ms));
        // At least 2 samples: the cubic read before the write needs one sample of room on each side.
        delay_.setTarget(static_cast<float>(std::max(2.0, std::clamp(static_cast<double>(p.delayMs), 0.0, kMaxDelayMs) * ms)));
        feedback_.setTarget(std::clamp(p.feedbackPct, -95.0f, 95.0f) * 0.01f);
        const MixGains g = linearMix(p.mixPct * 0.01f); // 50 %: dry and wet equal, the notches as deep as possible
        dry_.setTarget(g.dry);
        wet_.setTarget(g.wet);
    }

    /// The comb rings for (longest delay) × (repeats until the feedback has decayed by 90 dB).
    [[nodiscard]] int tailSamples() const noexcept
    {
        const double d = (params_.delayMs + params_.depthMs) * 0.001 * sampleRate_ + 2.0;
        const double fb = std::clamp(std::abs(static_cast<double>(params_.feedbackPct)) * 0.01, 0.0, 0.95);
        const double repeats = fb < 1e-3 ? 1.0 : 1.0 + std::ceil(-4.5 / std::log10(fb));
        return static_cast<int>(std::ceil(d * repeats));
    }

    void process(StereoBlock b) noexcept
    {
        constexpr dsp::Phase kQuarter = 0x40000000u;
        for (int i = 0; i < b.n; ++i) {
            const dsp::Phase phase = lfo_.tick();
            const float depth = depth_.tick(), base = delay_.tick(), fb = feedback_.tick();
            const float dry = dry_.tick(), wet = wet_.tick();
            // Read before write (the feedback needs this sample's delayed value first): delay − 1, see DelayLine.
            const float yL = lineL_.read(base + depth * 0.5f * (1.0f + lfoSine(phase)) - 1.0f);
            const float yR = lineR_.read(base + depth * 0.5f * (1.0f + lfoSine(phase + kQuarter)) - 1.0f);
            lineL_.push(b.L[i] + fb * yL);
            lineR_.push(b.R[i] + fb * yR);
            b.L[i] = dry * b.L[i] + wet * yL;
            b.R[i] = dry * b.R[i] + wet * yR;
        }
    }

private:
    FlangerParams params_{};
    double sampleRate_ = 48000.0;
    dsp::DelayLine<dsp::CubicInterp> lineL_, lineR_;
    dsp::Phasor lfo_;
    dsp::OnePoleSmoother depth_{}, delay_{}, feedback_{}, dry_{}, wet_{};
};

static_assert(StereoEffect<Flanger>);

} // namespace fss::fx
