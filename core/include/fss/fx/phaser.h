// fss/fx/phaser.h: a stereo allpass phaser (L5.4). [ZAV] ch. 3 (the TPT one-pole), [JOS-FILTERS] "allpass".
//
// A PHASER IS NOT A DELAY. It's a chain of first-order ALLPASS filters: each passes every frequency at full
// level (|H| = 1, L1.9's flat-magnitude case) but shifts its phase, from 0 at DC through −90° at the break
// frequency fc to −180° at Nyquist. N stages shift up to −N·180°. Mixed 50/50 with the dry signal, a frequency
// shifted by an odd multiple of −180° cancels: a notch. For N stages there are N/2 notches, at the frequencies
// where N·2·atan(f/fc) = (2k+1)·π, i.e. f = fc·tan((2k+1)·π/(2N)). For 6 stages: 0.27·fc, fc, 3.7·fc.
//
// So, compared with a flanger (a delay: phase −2π·f·τ, a straight line in f, endless evenly spaced notches):
//   - a phaser has FEW notches (N/2), and
//   - they're spaced by RATIO, not by Hz: the sound has no pitch of its own, where a flanger's comb has one
//     (the teeth at k/τ are a harmonic series, which is why a flanger with feedback "sings" a note).
// The LFO sweeps fc exponentially (octaves), so the notches move evenly as heard. Feedback (the last stage's
// output back into the first) sharpens the peaks between the notches.
//
// Each allpass is the TPT one-pole from L1.10 (zero-delay feedback, stable however fast fc moves):
//     v = (x − s)·G,  lp = v + s,  s = lp + v,  ap = 2·lp − x,     G = g/(1 + g),  g = tan(π·fc/fs)
//
// Changing the number of stages switches between two taps of the same chain, crossfaded over 20 ms (all 12
// stages always run, so every tap is always up to date).
#pragma once

#include <fss/dsp/phasor.h>
#include <fss/dsp/smoother.h>
#include <fss/fx/chorus.h> // lfoSine
#include <fss/fx/effect.h>
#include <fss/fx/fx_params.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace fss::fx {

class Phaser {
public:
    static constexpr bool kSpillsTail = false;
    static constexpr int kMaxStages = 12;

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sampleRate_ = sampleRate;
        lfo_.prepare(sampleRate);
        for (auto* s : {&centerOct_, &depth_, &feedback_, &dry_, &wet_})
            s->prepare(sampleRate, 20.0f);
        xfadeStep_ = static_cast<float>(1.0 / (0.020 * sampleRate));
        setParams(params_);
        reset();
    }

    void reset() noexcept
    {
        for (auto& ch : state_)
            ch.fill(0.0f);
        last_.fill(0.0f);
        lfo_.reset(0);
        for (auto* s : {&centerOct_, &depth_, &feedback_, &dry_, &wet_})
            s->reset(s->target());
        from_ = to_ = target_;
        xfade_ = 1.0f;
    }

    void setParams(const PhaserParams& p) noexcept
    {
        params_ = p;
        lfo_.setFrequency(std::clamp(static_cast<double>(p.rateHz), 0.01, 20.0));
        centerOct_.setTarget(std::log2(std::clamp(p.centerHz, 20.0f, 20000.0f))); // smoothed in octaves, like the cutoff
        depth_.setTarget(std::clamp(p.depthOct, 0.0f, 6.0f));
        feedback_.setTarget(std::clamp(p.feedbackPct, 0.0f, 90.0f) * 0.01f);
        const MixGains g = linearMix(p.mixPct * 0.01f);
        dry_.setTarget(g.dry);
        wet_.setTarget(g.wet);
        target_ = std::clamp<int>(p.stages, 1, kMaxStages);
    }

    /// Allpasses ring only briefly; with feedback a little longer. 100 ms covers 90 % feedback at low fc.
    [[nodiscard]] int tailSamples() const noexcept { return static_cast<int>(0.1 * sampleRate_); }

    void process(StereoBlock b) noexcept
    {
        constexpr dsp::Phase kQuarter = 0x40000000u;
        const double nyquistGuard = 0.45 * sampleRate_;
        for (int i = 0; i < b.n; ++i) {
            const dsp::Phase phase = lfo_.tick();
            const float center = centerOct_.tick(), depth = depth_.tick(), fb = feedback_.tick();
            const float dry = dry_.tick(), wet = wet_.tick();
            if (xfade_ >= 1.0f && target_ != to_) { // a new stage count: crossfade between the two taps
                from_ = to_;
                to_ = target_;
                xfade_ = 0.0f;
            }
            if (xfade_ < 1.0f)
                xfade_ = std::min(1.0f, xfade_ + xfadeStep_);
            const float lfo[2] = {lfoSine(phase), lfoSine(phase + kQuarter)};
            float* io[2] = {&b.L[i], &b.R[i]};
            for (std::size_t c = 0; c < 2; ++c) {
                const double fc = std::min(nyquistGuard, std::exp2(static_cast<double>(center + depth * lfo[c])));
                const auto g = static_cast<float>(std::tan(std::numbers::pi * fc / sampleRate_));
                const float G = g / (1.0f + g);
                float x = *io[c] + fb * last_[c];
                float tapFrom = 0.0f, tapTo = 0.0f;
                for (int k = 0; k < kMaxStages; ++k) {
                    float& s = state_[c][static_cast<std::size_t>(k)];
                    const float v = (x - s) * G;
                    const float lp = v + s;
                    s = lp + v;
                    x = 2.0f * lp - x;
                    if (k + 1 == from_)
                        tapFrom = x;
                    if (k + 1 == to_)
                        tapTo = x;
                }
                const float y = xfade_ < 1.0f ? (1.0f - xfade_) * tapFrom + xfade_ * tapTo : tapTo;
                last_[c] = y;
                *io[c] = dry * *io[c] + wet * y;
            }
        }
    }

private:
    PhaserParams params_{};
    double sampleRate_ = 48000.0;
    dsp::Phasor lfo_;
    std::array<std::array<float, kMaxStages>, 2> state_{};
    std::array<float, 2> last_{}; // each channel's previous output, for the feedback
    dsp::OnePoleSmoother centerOct_{}, depth_{}, feedback_{}, dry_{}, wet_{};
    int from_ = 6, to_ = 6, target_ = 6;
    float xfade_ = 1.0f, xfadeStep_ = 0.0f;
};

static_assert(StereoEffect<Phaser>);

} // namespace fss::fx
