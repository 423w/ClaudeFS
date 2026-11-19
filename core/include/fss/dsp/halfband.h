// fss/dsp/halfband.h: 2× and 4× oversampling with linear-phase halfband FIR filters (L5.2; L6.3 goes deeper).
//
// WHY. A waveshaper makes harmonics far above the input's bandwidth; at 48 kHz every one above 24 kHz folds back
// to an inharmonic frequency (an alias). Running the shaper at 2× or 4× the sample rate gives those harmonics
// room: they now fold around 48 or 96 kHz, and what lands between 24 kHz and the new Nyquist is removed by a
// low-pass before we drop back to the base rate. Only harmonics high enough to fold all the way back into the
// audio band still alias, and a shaper's harmonics get weaker the higher they go.
//
//   x ─► ↑2 (zero-stuff) ─► low-pass ─► shaper ─► low-pass ─► ↓2 (drop every other) ─► y
//
// HALFBAND FILTERS. A low-pass with its cutoff at exactly a quarter of the (high) sample rate, symmetric about it:
// pass up to fp, stop from 0.5 − fp. Its impulse response is a windowed sinc(n/2), and sinc(n/2) is ZERO at
// every even n except the centre, which is ½. So half of the taps are zero, and a polyphase implementation
// skips them all:
//   upsampling   y[2n]   = 2·Σⱼ h[2j]·x[n − j]     (the non-zero taps: a real FIR)
//                y[2n+1] = x[n − m]               (only the centre tap: a pure delay!)
//   downsampling y[n]    = Σⱼ h[2j]·v[2n − 2j] + ½·v[2n − c]
// with N = 4m + 3 taps and the centre c = (N − 1)/2 = 2m + 1. (The 2 in the upsampler makes up for the zeros we
// stuffed in: half the samples are zero, so the average level halves.)
//
// LINEAR PHASE = A WHOLE-SAMPLE LATENCY. A symmetric FIR delays every frequency by the same (N − 1)/2 samples (at
// the rate it runs at), so the waveform's shape survives; the price is that delay. Up + down = 2c samples at 2×
// = c samples at the base rate: 31 for the 63-tap stage. The 4× path adds a 23-tap stage at the 2× rate (11 + 11
// samples at 4× = 5.5 base samples) and one 2×-rate sample of padding to make the total whole: 37. The plugin
// reports that latency to the host (clap.latency), which shifts CFS's output back into time.
// (The alternative, IIR allpass halfbands, has almost no latency but bends the phase: L5.2 journal.)
//
// DESIGN: Kaiser-windowed sinc [JOS-SASP "Kaiser window"], [PIRKLE-FX]. β ≈ 0.1102·(A − 8.7) for a
// stopband A dB down. Measured (tests/test_drive.cpp): stage 1 passes 0.4·fs within 0.01 dB and stops from 0.6·fs
// by > 94 dB (fs = the base rate); stage 2 is relaxed, because at 4× everything between 0.6·fs and 1.4·fs is
// removed by stage 1 on the way down anyway.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>

namespace fss::dsp {

/// The modified Bessel function I₀, by its power series Σ ((x/2)^k / k!)². Converges for every x; at x ≈ 10
/// (our β) it needs about 30 terms.
[[nodiscard]] inline double besselI0(double x) noexcept
{
    double sum = 1.0, term = 1.0;
    const double q = 0.25 * x * x;
    for (int k = 1; k < 200 && term > 1e-17 * sum; ++k) {
        term *= q / (static_cast<double>(k) * k);
        sum += term;
    }
    return sum;
}

/// One halfband FIR of N = 4m + 3 taps, either as a 2× upsampler or as a 2× downsampler (each instance keeps
/// one history; use two instances for the two directions).
template <int N>
class Halfband {
    static_assert(N >= 7 && (N - 3) % 4 == 0, "a halfband with an odd centre needs N = 4m + 3 taps");

public:
    static constexpr int kCenter = (N - 1) / 2;   // c = 2m + 1: the delay in high-rate samples
    static constexpr int kTaps = (N + 1) / 2;     // the non-zero even-index taps h[0], h[2], ..., h[N − 1]
    static constexpr int kM = (kCenter - 1) / 2;  // m: the upsampler's odd outputs are x delayed by m

    /// Compute the taps: a Kaiser-windowed sinc(n/2), with the off-centre taps scaled so the DC gain is exactly 1.
    void design(double beta) noexcept
    {
        const double i0Beta = besselI0(beta);
        std::array<double, kTaps> t{};
        double sum = 0.0;
        for (int j = 0; j < kTaps; ++j) {
            const int k = 2 * j - kCenter; // offset from the centre: always odd
            const double sinc = std::sin(std::numbers::pi * k / 2.0) / (std::numbers::pi * k);
            const double r = static_cast<double>(2 * j) / (N - 1) * 2.0 - 1.0; // −1..1 across the filter
            const double w = besselI0(beta * std::sqrt(std::max(0.0, 1.0 - r * r))) / i0Beta;
            t[static_cast<std::size_t>(j)] = sinc * w;
            sum += sinc * w;
        }
        // The centre tap is ½ and the others must add up to ½ too, so that H(0) = 1 exactly. The window shrinks
        // them slightly, so rescale (by ~1.0001): no DC gain error, no level change.
        for (int j = 0; j < kTaps; ++j)
            taps_[static_cast<std::size_t>(j)] = static_cast<float>(t[static_cast<std::size_t>(j)] * (0.5 / sum));
        reset();
    }

    void reset() noexcept
    {
        hist_.fill(0.0f);
        histOdd_.fill(0.0f);
        pos_ = 0;
    }

    /// Upsample one base-rate sample into two high-rate samples (out[0] first in time).
    void up(float x, float* out) noexcept
    {
        push(hist_, x);
        out[0] = 2.0f * dot(hist_);
        out[1] = hist_[static_cast<std::size_t>(pos_ + kM)]; // the centre tap alone: x[n − m]
        advance();
    }

    /// Downsample two high-rate samples (even = earlier, odd = later) into one.
    [[nodiscard]] float down(float even, float odd) noexcept
    {
        push(hist_, even);
        push(histOdd_, odd);
        // v[2n − c] with c = 2m + 1 is the odd sample of pair n − m − 1.
        const float y = dot(hist_) + 0.5f * histOdd_[static_cast<std::size_t>(pos_ + kM + 1)];
        advance();
        return y;
    }

    [[nodiscard]] const std::array<float, kTaps>& taps() const noexcept { return taps_; }

private:
    // Each history is stored twice in a row, so the newest kTaps samples are always contiguous at pos_..pos_+kTaps−1
    // (newest first): the dot product never wraps. (The "double buffer" trick: two writes per sample, no modulo.)
    using History = std::array<float, 2 * kTaps>;
    void push(History& h, float x) noexcept
    {
        h[static_cast<std::size_t>(pos_)] = x;
        h[static_cast<std::size_t>(pos_ + kTaps)] = x;
    }
    void advance() noexcept { pos_ = pos_ == 0 ? kTaps - 1 : pos_ - 1; }
    [[nodiscard]] float dot(const History& h) const noexcept
    {
        float acc = 0.0f;
        const float* x = h.data() + pos_;
        for (int j = 0; j < kTaps; ++j)
            acc += taps_[static_cast<std::size_t>(j)] * x[j];
        return acc;
    }

    std::array<float, kTaps> taps_{};
    History hist_{}, histOdd_{};
    int pos_ = 0; // where the next sample goes (moves backwards, so newer samples sit at lower indices)
};

/// Oversampling around a memoryless function, per channel: factor 1 (straight through), 2 or 4.
class Oversampler {
public:
    using Stage1 = Halfband<63>; // base ↔ 2×: the demanding one (transition 0.4·fs .. 0.6·fs)
    using Stage2 = Halfband<23>; // 2× ↔ 4×: relaxed (transition 0.4·fs .. 1.6·fs, in base-rate terms)
    static constexpr int kMaxLatency = Stage1::kCenter + (Stage2::kCenter + 1) / 2; // 37

    Oversampler()
    {
        up1_.design(kBeta1);
        down1_.design(kBeta1);
        up2_.design(kBeta2);
        down2_.design(kBeta2);
    }

    /// 1, 2 or 4 (anything else is treated as 1). Clears the filters' memory.
    void setFactor(int factor) noexcept
    {
        factor_ = (factor == 2 || factor == 4) ? factor : 1;
        reset();
    }
    [[nodiscard]] int factor() const noexcept { return factor_; }

    void reset() noexcept
    {
        up1_.reset();
        down1_.reset();
        up2_.reset();
        down2_.reset();
        pad_ = 0.0f;
    }

    /// The delay this factor adds, in base-rate samples.
    [[nodiscard]] static constexpr int latencyFor(int factor) noexcept
    {
        return factor == 4 ? kMaxLatency : factor == 2 ? Stage1::kCenter : 0;
    }
    [[nodiscard]] int latency() const noexcept { return latencyFor(factor_); }

    /// One base-rate sample through f at the oversampled rate.
    template <class F>
    [[nodiscard]] float process(float x, F&& f) noexcept
    {
        if (factor_ == 1)
            return f(x);
        float a[2];
        up1_.up(x, a);
        if (factor_ == 2)
            return down1_.down(f(a[0]), f(a[1]));
        float b[2], c[2];
        up2_.up(a[0], b);
        up2_.up(a[1], c);
        const float e = down2_.down(f(b[0]), f(b[1]));
        const float o = down2_.down(f(c[0]), f(c[1]));
        // Stage 2 delays the 2× stream by 11 samples: half a base sample too many to be whole. One more 2× sample
        // (the previous pair's second half) makes it 12 = 6 base samples, so the total latency is an integer.
        const float y = down1_.down(pad_, e);
        pad_ = o;
        return y;
    }

private:
    static constexpr double kBeta1 = 9.6;  // ≈ 95 dB stopband for 63 taps (measured: −94.7 dB)
    static constexpr double kBeta2 = 10.0; // ≈ 95 dB for 23 taps over its wide transition
    Stage1 up1_, down1_;
    Stage2 up2_, down2_;
    float pad_ = 0.0f;
    int factor_ = 1;
};

} // namespace fss::dsp
