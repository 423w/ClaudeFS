// fss/dsp/delay_line.h: the delay line, the most reused structure in effects (L5.3). [JOS-PASP] "Delay lines",
// [DATTORRO] part 2 (interpolation and modulated delays).
//
// A circular buffer and a write position. Every sample: write the input at `pos`, advance. Reading "d samples
// ago" is reading at pos − d. With a power-of-two size N the wrap-around is a bit mask, (pos − d) & (N − 1),
// instead of a branch or a modulo (and it's right for negative values too, in unsigned arithmetic).
//
// FRACTIONAL DELAYS. A delay of 10.25 samples falls between two stored samples, so the read INTERPOLATES. The
// policy is a template parameter (zero cost, chosen per use):
//   NoInterp     rounds to the nearest sample. Exact and transparent for a fixed delay; changes in whole steps.
//   LinearInterp (1 − f)·x[n] + f·x[n+1]. Cheap. But it's a low-pass filter whose strength depends on f: at
//                f = 0.5 it averages two neighbours (a zero at Nyquist, −3 dB near fs/4). Fine for one pass,
//                audible in a feedback loop, and a moving f (modulation) makes that filter wobble.
//   CubicInterp  4-point, 3rd-order Lagrange: flat to much higher frequencies, a little more work. The one for
//                modulated delays (chorus, L5.4).
// (Allpass interpolation, the fourth classic, has a perfectly flat magnitude but keeps state, which makes it
// bad when the delay moves; not implemented here. [DATTORRO] part 2 discusses it.)
//
// Allocation happens only in prepare(). Everything else is real-time safe.
#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <type_traits>
#include <vector>

namespace fss::dsp {

/// Read policies. Each reads `delay` samples before the most recent write (delay ≥ 0 for NoInterp/Linear,
/// ≥ 1 for Cubic, which also needs the sample after). `buf` has power-of-two size; `newest` is the index of
/// the most recently written sample.
struct NoInterp {
    static constexpr int kExtraSamples = 1;
    [[nodiscard]] static float read(std::span<const float> buf, std::size_t mask, std::size_t newest, double delay) noexcept
    {
        const auto d = static_cast<std::size_t>(delay + 0.5);
        return buf[(newest - d) & mask];
    }
};

struct LinearInterp {
    static constexpr int kExtraSamples = 2;
    [[nodiscard]] static float read(std::span<const float> buf, std::size_t mask, std::size_t newest, double delay) noexcept
    {
        const auto whole = static_cast<std::size_t>(delay);
        const auto f = static_cast<float>(delay - static_cast<double>(whole));
        const float a = buf[(newest - whole) & mask];     // d samples ago
        const float b = buf[(newest - whole - 1) & mask]; // d + 1 samples ago
        return a + f * (b - a);
    }
};

struct CubicInterp {
    static constexpr int kExtraSamples = 3;
    [[nodiscard]] static float read(std::span<const float> buf, std::size_t mask, std::size_t newest, double delay) noexcept
    {
        // Lagrange through the four samples at delays w−1, w, w+1, w+2 (w = whole part), evaluated at w + f.
        const auto whole = static_cast<std::size_t>(delay);
        const auto f = static_cast<float>(delay - static_cast<double>(whole));
        const float xm1 = buf[(newest - whole + 1) & mask];
        const float x0 = buf[(newest - whole) & mask];
        const float x1 = buf[(newest - whole - 1) & mask];
        const float x2 = buf[(newest - whole - 2) & mask];
        // The four Lagrange basis polynomials at points −1, 0, 1, 2:
        const float cm1 = -f * (f - 1.0f) * (f - 2.0f) / 6.0f;
        const float c0 = (f + 1.0f) * (f - 1.0f) * (f - 2.0f) / 2.0f;
        const float c1 = -(f + 1.0f) * f * (f - 2.0f) / 2.0f;
        const float c2 = (f + 1.0f) * f * (f - 1.0f) / 6.0f;
        return cm1 * xm1 + c0 * x0 + c1 * x1 + c2 * x2;
    }
};

template <class Interp = LinearInterp>
class DelayLine {
public:
    /// Allocate room for delays up to maxDelaySamples (plus what the interpolation needs). Not real-time safe.
    void prepare(std::size_t maxDelaySamples)
    {
        const std::size_t size = std::bit_ceil(maxDelaySamples + static_cast<std::size_t>(Interp::kExtraSamples) + 1);
        buf_.assign(size, 0.0f);
        mask_ = size - 1;
        maxDelay_ = static_cast<double>(maxDelaySamples);
        newest_ = 0;
    }
    void reset() noexcept
    {
        std::fill(buf_.begin(), buf_.end(), 0.0f);
        newest_ = 0;
    }

    /// Write the next input sample (it becomes "0 samples ago").
    void push(float x) noexcept
    {
        newest_ = (newest_ + 1) & mask_;
        buf_[newest_] = x;
    }

    /// Read `delaySamples` before the newest sample, clamped to [minimum, maxDelay]. A read-before-push loop
    /// (read, then push input + feedback) therefore delays by delaySamples + 1: callers subtract the 1.
    /// `I` picks another interpolation for this read (it must need no more extra samples than `Interp`).
    template <class I = Interp>
    [[nodiscard]] float read(double delaySamples) const noexcept
    {
        static_assert(I::kExtraSamples <= Interp::kExtraSamples, "the buffer has no room for this interpolation");
        constexpr double minDelay = std::is_same_v<I, CubicInterp> ? 1.0 : 0.0;
        const double d = std::fmin(std::fmax(delaySamples, minDelay), maxDelay_);
        return I::read(buf_, mask_, newest_, d);
    }

    [[nodiscard]] double maxDelay() const noexcept { return maxDelay_; }
    [[nodiscard]] std::span<const float> buffer() const noexcept { return buf_; }

private:
    std::vector<float> buf_ = std::vector<float>(1, 0.0f); // usable (silent) before prepare()
    std::size_t mask_ = 0;
    std::size_t newest_ = 0;
    double maxDelay_ = 0.0;
};

} // namespace fss::dsp
