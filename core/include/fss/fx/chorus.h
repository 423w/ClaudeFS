// fss/fx/chorus.h: a multi-voice stereo chorus (L5.4). [DATTORRO] part 2, [REISS-FX] ch. 2.
//
// A chorus is a few copies of the sound, each delayed by 10–20 ms and each delay slowly swept by an LFO.
// A moving delay is a pitch shift: reading d(t) samples behind a write head that moves at 1 sample per sample,
// the read head moves at 1 − d'(t), so the copy's pitch is scaled by that factor. With a sine sweep of width W
// (peak to peak) at rate r, the peak shift is π·r·W: 0.6 Hz and 3 ms give ±0.57 % ≈ ±10 cents. A few copies a
// few cents sharp and flat, beating against the dry sound and each other: an "ensemble".
//
//   voice k:        delay = delayMs + depthMs · ½·(1 + sin(2π·(φ + offset_k))),  offset = 0, ½, ¼, ¾
//   right channel:  the same, a quarter cycle (90°) further on
// The offsets are FIXED per voice (not k/N): changing the voice count then only fades a voice in or out; with
// k/N, every existing voice's phase would jump, and a jump in a delay is a click. With 2 voices the four taps
// sit exactly 90° apart; with 4, every 45°. A mono synth comes out stereo.
//
// The read must interpolate (the delay is never a whole number of samples for long), and a MOVING fractional
// read is where interpolation quality shows: linear interpolation's gain depends on the fraction, so a moving
// fraction ripples the level of high frequencies (journal, prediction 2). Hence a template parameter, cubic by
// default; the l5.4 render compares the two.
#pragma once

#include <fss/dsp/delay_line.h>
#include <fss/dsp/phasor.h>
#include <fss/dsp/smoother.h>
#include <fss/fx/effect.h>
#include <fss/fx/fx_params.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace fss::fx {

/// sin(2π · cycles) of a fixed-point phase (dsp::Phase: one cycle = 2^32, wraps by itself).
[[nodiscard]] inline float lfoSine(dsp::Phase p) noexcept
{
    return static_cast<float>(std::sin(2.0 * std::numbers::pi * dsp::phaseToCycles(p)));
}

template <class Interp = dsp::CubicInterp>
class ChorusT {
public:
    static constexpr bool kSpillsTail = false; // a 40 ms tail isn't worth spilling: off fades back to dry
    static constexpr int kMaxVoices = 4;
    static constexpr double kMaxDelayMs = 30.0, kMaxDepthMs = 10.0;

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sampleRate_ = sampleRate;
        const auto maxSamples = static_cast<std::size_t>(std::ceil((kMaxDelayMs + kMaxDepthMs) * 0.001 * sampleRate)) + 4;
        lineL_.prepare(maxSamples);
        lineR_.prepare(maxSamples);
        lfo_.prepare(sampleRate);
        for (auto* s : {&depth_, &delay_, &dry_, &wet_})
            s->prepare(sampleRate, 20.0f);
        for (auto& g : voiceGain_)
            g.prepare(sampleRate, 20.0f); // a voice added or removed fades in/out instead of appearing
        setParams(params_);
        reset();
    }

    void reset() noexcept
    {
        lineL_.reset();
        lineR_.reset();
        lfo_.reset(0);
        for (auto* s : {&depth_, &delay_, &dry_, &wet_})
            s->reset(s->target());
        for (auto& g : voiceGain_)
            g.reset(g.target());
    }

    void setParams(const ChorusParams& p) noexcept
    {
        params_ = p;
        lfo_.setFrequency(std::clamp(static_cast<double>(p.rateHz), 0.01, 20.0));
        const double ms = 0.001 * sampleRate_;
        depth_.setTarget(static_cast<float>(std::clamp(static_cast<double>(p.depthMs), 0.0, kMaxDepthMs) * ms));
        delay_.setTarget(static_cast<float>(std::clamp(static_cast<double>(p.delayMs), 1.0, kMaxDelayMs) * ms));
        // Equal power, not linear (measured, L5.4 journal): a copy 8–20 ms late and moving is UNCORRELATED with
        // the dry sound at every frequency that matters (it's many periods away, and the comb that dry + wet
        // would make keeps sliding), so they add as powers. With the linear law the 50 % mix came out 5.5 dB
        // quieter than the dry sound in mono.
        const MixGains g = equalPowerMix(p.mixPct * 0.01f);
        dry_.setTarget(g.dry);
        wet_.setTarget(g.wet);
        const int n = std::clamp<int>(p.voices, 1, kMaxVoices);
        for (int k = 0; k < kMaxVoices; ++k)
            voiceGain_[static_cast<std::size_t>(k)].setTarget(k < n ? 1.0f : 0.0f);
    }

    [[nodiscard]] int tailSamples() const noexcept
    {
        return static_cast<int>(std::ceil((params_.delayMs + params_.depthMs) * 0.001 * sampleRate_)) + 4;
    }

    void process(StereoBlock b) noexcept
    {
        constexpr dsp::Phase kQuarter = 0x40000000u; // 90° in fixed-point phase
        constexpr std::array<dsp::Phase, kMaxVoices> kOffset = {0u, 2 * kQuarter, kQuarter, 3 * kQuarter};
        for (int i = 0; i < b.n; ++i) {
            const dsp::Phase phase = lfo_.tick();
            const float depth = depth_.tick(), base = delay_.tick();
            const float dry = dry_.tick(), wet = wet_.tick();
            lineL_.push(b.L[i]); // write first: delays are ≥ 1 ms, so the read never touches this sample
            lineR_.push(b.R[i]);
            float sumL = 0.0f, sumR = 0.0f, power = 0.0f;
            for (int k = 0; k < kMaxVoices; ++k) {
                const float g = voiceGain_[static_cast<std::size_t>(k)].tick();
                if (g <= 0.0f)
                    continue;
                const dsp::Phase pk = phase + kOffset[static_cast<std::size_t>(k)];
                const float dl = base + depth * 0.5f * (1.0f + lfoSine(pk));
                const float dr = base + depth * 0.5f * (1.0f + lfoSine(pk + kQuarter));
                sumL += g * lineL_.read(dl);
                sumR += g * lineR_.read(dr);
                power += g * g;
            }
            // The voices are uncorrelated with each other too (each moves differently), so they're normalized by
            // POWER, 1/√(Σg²): the wet path has the input's loudness whatever the voice count. (With depth 0 the
            // copies are identical and add coherently, √N louder: a chorus without depth is a corner case.)
            const float norm = power > 1e-6f ? 1.0f / std::sqrt(power) : 0.0f;
            b.L[i] = dry * b.L[i] + wet * sumL * norm;
            b.R[i] = dry * b.R[i] + wet * sumR * norm;
        }
    }

private:
    ChorusParams params_{};
    double sampleRate_ = 48000.0;
    dsp::DelayLine<Interp> lineL_, lineR_;
    dsp::Phasor lfo_;
    dsp::OnePoleSmoother depth_{}, delay_{}, dry_{}, wet_{}; // depth and delay in samples
    std::array<dsp::OnePoleSmoother, kMaxVoices> voiceGain_{};
};

using Chorus = ChorusT<>;
static_assert(StereoEffect<Chorus>);

} // namespace fss::fx
