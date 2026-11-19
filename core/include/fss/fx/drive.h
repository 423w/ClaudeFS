// fss/fx/drive.h: the chain's first effect: saturation and distortion, oversampled (L5.2). [ZAV] ch. 6,
// [REISS-FX] ch. 7, [PIRKLE-FX].
//
//                 ┌───────── dry, delayed by the latency (so the mix doesn't comb-filter) ──────────┐
//   in ──┬────────┘                                                                                  ├─► out
//        └─► pre-emphasis ─► × drive ─► [↑ shaper ↓] ─► (DC block) ─► de-emphasis ─► × auto gain ──┘
//            (+tone dB of highs)        1×, 2× or 4×    (Tube only)    (exact inverse)  × output trim
//
// TONE = EMPHASIS. A high shelf boosts the highs by `toneDb` before the shaper, and its exact inverse cuts them
// again after it. While the shaper is linear the two cancel (bit-for-bit up to rounding: tested), so tone changes
// nothing about the clean sound, only WHAT distorts: +12 dB pushes the highs into the curve first (fizzy, bright,
// "treble booster"), −12 dB the lows (fat, warm, the highs stay clean). Same trick as tape pre-emphasis.
//   shelf:   y = x + (A − 1)·HP_g(x)          A = 10^(tone/20), HP = a TPT one-pole high-pass at 1 kHz
//   inverse: y = x + (1/A − 1)·HP_{g/A}(x)    the analog inverse (1 + s/ω)/(1 + A·s/ω) is a shelf at ω/A,
//                                            and with the TPT's g = tan(πfc/fs) the corner ω/A is g/A exactly
//
// AUTO GAIN. Distortion makes quiet signals louder (the drive) and loud ones not (the ceiling). Louder always
// "sounds better" in an A/B, so an honest drive knob must not change the loudness. The auto gain MEASURES: two
// slow energy followers (τ = 200 ms, both channels together), one on the (latency-aligned) input, one on the
// shaped signal, and the wet path is scaled by √(input energy / output energy). So whatever the patch, the drive
// changes the tone and the loudness stays put (slow level changes are matched; the shaper's fast squashing, which
// IS the sound, isn't). The first version was a fixed law per drive setting that matched a −12 dBFS sine: on real
// patches it was 6 to 13 dB off, because a synth's signal inside the chain is hotter and denser than a sine
// (L5.2 journal).
//
// OVERSAMPLING AND LATENCY. The shaper runs at 1×, 2× or 4× (dsp/halfband.h); 2× and 4× delay the output by 31
// and 37 samples. A plugin's latency may only change while the host restarts it (CLAP: deactivate → activate),
// so the factor has two values: the one the patch asks for (params) and the one in use (committed). commitLatency()
// switches; the Engine calls it at prepare()/setPatch() (and, in hosts without delay compensation, on the change
// itself). The FxSlot around this effect delays its own dry path by latencySamples() too, so switching the Drive on
// and off crossfades between two time-aligned signals, and the host's delay compensation never has to change
// when the Drive is merely switched or automated.
#pragma once

#include <fss/dsp/halfband.h>
#include <fss/dsp/one_pole.h>
#include <fss/dsp/shapers.h>
#include <fss/dsp/smoother.h>
#include <fss/fx/effect.h>
#include <fss/fx/fx_params.h>
#include <fss/util/math.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace fss::fx {

[[nodiscard]] constexpr int driveFactorFromIndex(int index) noexcept { return index >= 2 ? 4 : index == 1 ? 2 : 1; }

class Drive {
public:
    static constexpr bool kSpillsTail = false;
    static constexpr float kToneCornerHz = 1000.0f;
    static constexpr float kDcBlockHz = 10.0f;

    void prepare(double sampleRate, int /*maxBlock*/)
    {
        sampleRate_ = sampleRate;
        toneG_ = static_cast<float>(std::tan(std::numbers::pi * std::min(static_cast<double>(kToneCornerHz), 0.45 * sampleRate) / sampleRate));
        for (Channel& c : ch_) {
            c.dc.prepare(sampleRate);
            c.dc.setCutoff(kDcBlockHz);
        }
        for (auto* s : {&drive_, &post_, &tone_, &dry_, &wet_})
            s->prepare(sampleRate, 20.0f);
        autoMix_.prepare(sampleRate, 20.0f);
        envK_ = static_cast<float>(1.0 - std::exp(-1.0 / (kAutoGainMs * 0.001 * sampleRate)));
        setParams(params_);
        reset();
        inEnv_ = outEnv_ = 0.0f;
        ratio_ = 1.0f;
    }

    void reset() noexcept
    {
        for (Channel& c : ch_) {
            c.os.reset();
            c.dc.reset();
            c.pre = c.post = 0.0f;
            c.ring.fill(0.0f);
        }
        w_ = 0;
        for (auto* s : {&drive_, &post_, &tone_, &dry_, &wet_, &autoMix_})
            s->reset(s->target());
        // Not the auto gain's measurement: it HOLDS through silence (and the slot resets a sleeping effect), so a note after a
        // pause starts at the level the last one ended at.
    }

    void setParams(const DriveParams& p) noexcept
    {
        params_ = p;
        type_ = static_cast<dsp::ShaperType>(std::min<int>(p.type, dsp::kNumShaperTypes - 1));
        requestedFactor_ = driveFactorFromIndex(p.oversampling);
        const float g = util::dbToGain(std::clamp(p.driveDb, 0.0f, 48.0f));
        drive_.setTarget(g);
        autoMix_.setTarget(p.autoGain ? 1.0f : 0.0f); // switching it fades (the two gains can be 20 dB apart)
        post_.setTarget(util::dbToGain(std::clamp(p.outputDb, -48.0f, 24.0f)));
        tone_.setTarget(util::dbToGain(std::clamp(p.toneDb, -24.0f, 24.0f)));
        const MixGains m = linearMix(p.mixPct * 0.01f); // aligned and in phase with the dry: they add as amplitudes
        dry_.setTarget(m.dry);
        wet_.setTarget(m.wet);
    }

    // ---- latency (see the header comment) ----
    [[nodiscard]] int latencySamples() const noexcept { return dsp::Oversampler::latencyFor(factor_); }
    [[nodiscard]] bool latencyPending() const noexcept { return requestedFactor_ != factor_; }
    [[nodiscard]] int requestedLatencySamples() const noexcept { return dsp::Oversampler::latencyFor(requestedFactor_); }
    void commitLatency() noexcept
    {
        factor_ = requestedFactor_;
        for (Channel& c : ch_)
            c.os.setFactor(factor_);
        reset();
    }
    [[nodiscard]] int factor() const noexcept { return factor_; }

    /// After the input stops: the latency, the shelves' ring (τ = 0.16 ms: 128 samples is −140 dB), and for Tube
    /// the DC blocker's slow decay (τ = 16 ms; 0.2 s is ~12 τ, −100 dB), so sleeping never cuts off a tail.
    [[nodiscard]] int tailSamples() const noexcept
    {
        const int dc = type_ == dsp::ShaperType::Tube ? static_cast<int>(std::ceil(0.2 * sampleRate_)) : 0;
        return latencySamples() + 128 + dc;
    }

    void process(StereoBlock b) noexcept
    {
        const int lat = latencySamples();
        const bool tube = type_ == dsp::ShaperType::Tube;
        const dsp::ShaperType t = type_;
        auto shaper = [t](float x) noexcept { return dsp::shape(t, x); };
        for (int i = 0; i < b.n; ++i) {
            const float autoMix = autoMix_.tick();
            const float drive = drive_.tick(), post = post_.tick(), a = tone_.tick();
            const float dryG = dry_.tick(), wetG = wet_.tick();
            // The two shelves' one-pole coefficients, G = g/(1 + g): the pre-emphasis at the fixed corner, the
            // de-emphasis at g/A (the inverse's corner), so the pair cancels exactly for any A.
            const float gPre = toneG_, gPost = toneG_ / a;
            const float kPre = gPre / (1.0f + gPre), kPost = gPost / (1.0f + gPost);
            const int r = w_;
            float dry[2], wet[2];
            float inE = 0.0f, outE = 0.0f;
            for (int c = 0; c < 2; ++c) {
                Channel& s = ch_[static_cast<std::size_t>(c)];
                const float x = (c == 0 ? b.L : b.R)[i];
                s.ring[static_cast<std::size_t>(r)] = x;
                dry[c] = s.ring[static_cast<std::size_t>((r - lat) & kRingMask)];
                float y = drive * (x + (a - 1.0f) * hp(s.pre, kPre, x));
                y = s.os.process(y, shaper);
                if (tube)
                    y = s.dc.tickHighPass(y);
                wet[c] = y + (1.0f / a - 1.0f) * hp(s.post, kPost, y);
                inE += dry[c] * dry[c]; // the input as it was when this output's input went in: aligned, like the mix
                outE += wet[c] * wet[c];
            }
            w_ = (r + 1) & kRingMask;
            // Auto gain: follow both energies; the compensation is the square root of their ratio, clamped to
            // ±48 dB. In silence both followers decay at the same rate, so their ratio (and the gain) stays where it
            // was until they underflow; then it holds (0/0 says nothing).
            inEnv_ += envK_ * (inE - inEnv_);
            outEnv_ += envK_ * (outE - outEnv_);
            if (inEnv_ > 1e-30f && outEnv_ > 1e-30f)
                ratio_ = std::clamp(std::sqrt(inEnv_ / outEnv_), 1.0f / 256.0f, 256.0f);
            const float gain = post * (1.0f + autoMix * (ratio_ - 1.0f));
            b.L[i] = dryG * dry[0] + wetG * (gain * wet[0]);
            b.R[i] = dryG * dry[1] + wetG * (gain * wet[1]);
        }
    }

private:
    static constexpr int kRingSize = 64; // > kMaxLatency: holds the dry signal until the wet one catches up
    static constexpr int kRingMask = kRingSize - 1;
    static_assert(kRingSize > dsp::Oversampler::kMaxLatency);

    /// TPT (trapezoidal) one-pole high-pass, state `s`, coefficient k = g/(1 + g). [ZAV] ch. 3.
    [[nodiscard]] static float hp(float& s, float k, float x) noexcept
    {
        const float v = (x - s) * k;
        const float lp = v + s;
        s = lp + v;
        return x - lp;
    }

    struct Channel {
        dsp::Oversampler os;
        dsp::OnePole dc;
        float pre = 0.0f, post = 0.0f; // the two shelves' states
        std::array<float, kRingSize> ring{};
    };
    std::array<Channel, 2> ch_{};
    DriveParams params_{};
    dsp::ShaperType type_ = dsp::ShaperType::Soft;
    int requestedFactor_ = 1, factor_ = 1;
    static constexpr double kAutoGainMs = 200.0;
    float inEnv_ = 0.0f, outEnv_ = 0.0f, envK_ = 1e-4f; // the auto gain's energy followers
    float ratio_ = 1.0f;                                 // √(in/out): the compensation, held in silence
    dsp::OnePoleSmoother autoMix_{};                     // 0 = auto gain off, 1 = on (fades between them)
    float toneG_ = 0.07f;
    double sampleRate_ = 48000.0;
    int w_ = 0;
    dsp::OnePoleSmoother drive_{}, post_{}, tone_{}, dry_{}, wet_{};
};

static_assert(StereoEffect<Drive>);

} // namespace fss::fx
