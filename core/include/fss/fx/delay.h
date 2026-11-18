// fss/fx/delay.h: a stereo, tempo-synced, ping-pong delay with damped feedback (L5.3). [JOS-PASP], [DATTORRO]
//
//        in ─┬──────────────────────────────────────────────── dry ──┐
//            │      ┌──────────────┐                                  ├─► out   (equal-power dry/wet)
//            └──(+)─► delay line L ├──┬─────────────────────── wet ──┘
//                ▲  └──────────────┘  │
//                │                    ▼
//                └── feedback × ◄── damping (one-pole low-pass)       (and the same for R)
//
// PING-PONG crossfeeds the two lines: the input (as mono) goes into the LEFT line only, the left line's echo
// feeds the right line, the right's feeds back into the left. So echo 1 is left, echo 2 right, echo 3 left...
// A smoothed 0..1 amount blends the two routings, so switching ping-pong while echoes are in flight doesn't
// write a step into the lines (which would come back out one delay time later as a click).
//
// THE DAMPING is inside the loop, after the output tap: the first echo is as bright as the input; every repeat
// passes the low-pass once more, so the tail darkens the way tape and analog delays do.
//
// CHANGING THE TIME (DelayTimeMode). The lesson's choice, and both are offered:
//   Clean  two read taps, the old time and the new one, crossfaded linearly over 50 ms. Reads at WHOLE samples
//          (NoInterp): a fixed delay doesn't need a fractional read, and a fractional linear read is a low-pass
//          that would darken every repeat a little more (journal: prediction 2). A new time that arrives during
//          a crossfade waits for it to finish, so a knob sweep becomes a series of clean 50 ms steps.
//   Tape   one tap whose delay glides toward the target (one-pole, ~150 ms), read with linear interpolation.
//          A shrinking delay reads the buffer faster than it's written: the echoes go up in pitch while it
//          moves (and down when it grows). The classic tape-echo "warble" when you turn the knob.
//
// TEMPO SYNC. delay = beats × 60 / BPM seconds. The Engine passes the host tempo (Event::Type::Tempo); without a
// host transport it stays at Engine::kDefaultTempoBpm (120). A tempo change is just a new target time.
//
// Max delay 4 s (a whole note at 60 BPM). Both lines are allocated in prepare().
#pragma once

#include <fss/dsp/delay_line.h>
#include <fss/dsp/smoother.h>
#include <fss/fx/effect.h>
#include <fss/fx/fx_params.h>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace fss::fx {

class StereoDelay {
public:
    static constexpr bool kSpillsTail = true; // switched off, the echoes already in flight ring out
    static constexpr double kMaxDelayMs = 4000.0;
    static constexpr double kMinDelayMs = 1.0;
    static constexpr float kCrossfadeMs = 50.0f;
    static constexpr float kTapeGlideMs = 150.0f;

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sampleRate_ = sampleRate;
        const auto maxSamples = static_cast<std::size_t>(std::ceil(kMaxDelayMs * 0.001 * sampleRate)) + 1;
        lineL_.prepare(maxSamples);
        lineR_.prepare(maxSamples);
        xfadeStep_ = 1.0 / (static_cast<double>(kCrossfadeMs) * 0.001 * sampleRate);
        tapeK_ = 1.0 - std::exp(-1.0 / (static_cast<double>(kTapeGlideMs) * 0.001 * sampleRate));
        feedback_.prepare(sampleRate, 20.0f);
        dry_.prepare(sampleRate, 20.0f);
        wet_.prepare(sampleRate, 20.0f);
        damping_.prepare(sampleRate, 20.0f);
        pingPong_.prepare(sampleRate, 20.0f);
        setParams(params_);
        reset();
    }

    /// Silence: empty lines, the taps at the target time, smoothers at their targets.
    void reset() noexcept
    {
        lineL_.reset();
        lineR_.reset();
        lpL_ = lpR_ = 0.0f;
        from_ = to_ = target_;
        xfade_ = 1.0;
        tape_ = target_;
        feedback_.reset(feedback_.target());
        dry_.reset(dry_.target());
        wet_.reset(wet_.target());
        damping_.reset(damping_.target());
        pingPong_.reset(pingPong_.target());
    }

    void setParams(const DelayParams& p) noexcept
    {
        const bool modeChanged = p.timeMode != params_.timeMode;
        params_ = p;
        feedback_.setTarget(std::clamp(p.feedbackPct, 0.0f, 99.0f) * 0.01f);
        const MixGains g = equalPowerMix(p.mixPct * 0.01f);
        dry_.setTarget(g.dry);
        wet_.setTarget(g.wet);
        // One-pole coefficient a = 1 − e^(−2π·fc/fs) (L1.9's impulse-invariant form), smoothed as a coefficient.
        const double fc = std::clamp(static_cast<double>(p.dampingHz), 20.0, 0.45 * sampleRate_);
        damping_.setTarget(static_cast<float>(1.0 - std::exp(-2.0 * std::numbers::pi * fc / sampleRate_)));
        pingPong_.setTarget(p.pingPong ? 1.0f : 0.0f);
        retarget();
        if (modeChanged) { // hand the current position over to the other mode's state, without a jump
            if (p.timeMode == DelayTimeMode::Tape) {
                tape_ = xfade_ < 1.0 ? to_ : from_;
            } else {
                from_ = to_ = std::round(tape_);
                xfade_ = 1.0;
            }
        }
    }

    void setTempo(double bpm) noexcept
    {
        bpm_ = std::clamp(bpm, 20.0, 999.0);
        retarget();
    }

    /// Echoes fall by the feedback gain per repeat: 90 dB takes log(10^−4.5)/log(fb) repeats (prediction 5).
    [[nodiscard]] int tailSamples() const noexcept
    {
        const double d = std::max({from_, to_, target_, tape_}) + 1.0;
        const double fb = std::clamp(static_cast<double>(params_.feedbackPct) * 0.01, 0.0, 0.99);
        const double repeats = fb < 1e-3 ? 1.0 : 1.0 + std::ceil(-4.5 / std::log10(fb));
        return static_cast<int>(std::min(d * repeats, 1.0e9));
    }

    /// The current delay in samples (where the taps are heading): for tests and the editor.
    [[nodiscard]] double targetSamples() const noexcept { return target_; }

    void process(StereoBlock b) noexcept
    {
        for (int i = 0; i < b.n; ++i) {
            const float fb = feedback_.tick();
            const float a = damping_.tick();
            const float pp = pingPong_.tick();
            const float dry = dry_.tick(), wet = wet_.tick();

            // 1. Read the echoes (before this sample's write, hence the −1: see DelayLine::read).
            float yL = 0.0f, yR = 0.0f;
            if (params_.timeMode == DelayTimeMode::Tape) {
                tape_ += (target_ - tape_) * tapeK_;
                yL = lineL_.read(tape_ - 1.0);
                yR = lineR_.read(tape_ - 1.0);
            } else {
                if (xfade_ >= 1.0 && target_ != to_) { // start a new crossfade toward the latest target
                    from_ = to_;
                    to_ = target_;
                    xfade_ = 0.0;
                }
                if (xfade_ < 1.0) {
                    xfade_ = std::min(1.0, xfade_ + xfadeStep_);
                    const auto x = static_cast<float>(xfade_);
                    yL = (1.0f - x) * lineL_.read<dsp::NoInterp>(from_ - 1.0) + x * lineL_.read<dsp::NoInterp>(to_ - 1.0);
                    yR = (1.0f - x) * lineR_.read<dsp::NoInterp>(from_ - 1.0) + x * lineR_.read<dsp::NoInterp>(to_ - 1.0);
                    if (xfade_ >= 1.0)
                        from_ = to_;
                } else {
                    yL = lineL_.read<dsp::NoInterp>(to_ - 1.0);
                    yR = lineR_.read<dsp::NoInterp>(to_ - 1.0);
                }
            }

            // 2. Damp the feedback, then write: straight (L→L, R→R) blended with ping-pong (mono in → L → R → L).
            lpL_ += a * (yL - lpL_);
            lpR_ += a * (yR - lpR_);
            const float inL = b.L[i], inR = b.R[i];
            const float mono = 0.5f * (inL + inR);
            lineL_.push((1.0f - pp) * (inL + fb * lpL_) + pp * (mono + fb * lpR_));
            lineR_.push((1.0f - pp) * (inR + fb * lpR_) + pp * (fb * lpL_));

            // 3. Mix.
            b.L[i] = dry * inL + wet * yL;
            b.R[i] = dry * inR + wet * yR;
        }
    }

private:
    void retarget() noexcept
    {
        const double seconds = params_.sync
                                   ? kDelayDivisions[std::min<std::size_t>(params_.division, kNumDelayDivisions - 1)].beats * 60.0 / bpm_
                                   : static_cast<double>(params_.timeMs) * 0.001;
        const double ms = std::clamp(seconds * 1000.0, kMinDelayMs, kMaxDelayMs);
        target_ = ms * 0.001 * sampleRate_;
        if (params_.timeMode == DelayTimeMode::Clean)
            target_ = std::round(target_); // whole samples: see "Clean" above
    }

    DelayParams params_{};
    double sampleRate_ = 48000.0;
    double bpm_ = 120.0;
    dsp::DelayLine<dsp::LinearInterp> lineL_, lineR_;
    double target_ = 18000.0;           // where the time is heading, in samples
    double from_ = 18000.0, to_ = 18000.0, xfade_ = 1.0, xfadeStep_ = 0.0; // Clean: two taps
    double tape_ = 18000.0, tapeK_ = 0.0;                                   // Tape: one gliding tap
    float lpL_ = 0.0f, lpR_ = 0.0f;     // damping filters' state
    dsp::OnePoleSmoother feedback_{}, dry_{}, wet_{}, damping_{}, pingPong_{};
};

static_assert(StereoEffect<StereoDelay>);

} // namespace fss::fx
