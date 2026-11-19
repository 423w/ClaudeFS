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
//
// LAZY CLEARING (L5.5, an L5.3 open item). reset() used to fill the whole buffer with zeros: 4 s of delay at
// 192 kHz is 1 MB, 210 µs on the audio thread every time an FX slot put its delay to sleep. Now reset() only
// remembers that nothing has been written yet (written_ = 0), and a read that reaches back further than what was
// written since then reads zeros instead. The result is bit-identical to clearing the buffer, at O(1) cost.
#pragma once

#include <algorithm>
#include <array>
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
        written_ = 0;
    }
    /// Silence, in O(1): the old contents stay in memory but can never be read again (see LAZY CLEARING).
    void reset() noexcept
    {
        newest_ = 0;
        written_ = 0;
    }

    /// Write the next input sample (it becomes "0 samples ago").
    void push(float x) noexcept
    {
        newest_ = (newest_ + 1) & mask_;
        buf_[newest_] = x;
        if (written_ <= mask_)
            ++written_; // saturates at the buffer size: from then on every slot holds a real sample
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
        // Fast path: every sample the interpolation touches (up to whole + 2 ago) was written since the reset.
        const auto whole = static_cast<std::size_t>(d);
        if (whole + 3 <= written_)
            return I::read(buf_, mask_, newest_, d);
        // Shortly after a reset: copy the (up to) four samples around the read into a tiny buffer, with zeros
        // for the ones written before the reset, and read that with the same policy. Index 7 plays "newest";
        // the policy then finds sample (whole + k) ago at 7 − 2 − k, for k = −1..2 (delay shifted to 2 + fraction).
        std::array<float, 8> local{};
        for (int k = -1; k <= 2; ++k) {
            const auto ago = static_cast<std::ptrdiff_t>(whole) + k;
            if (ago >= 0 && static_cast<std::size_t>(ago) < written_)
                local[static_cast<std::size_t>(5 - k)] = buf_[(newest_ - static_cast<std::size_t>(ago)) & mask_];
        }
        return I::read(local, 7, 7, 2.0 + (d - static_cast<double>(whole)));
    }

    [[nodiscard]] double maxDelay() const noexcept { return maxDelay_; }
    [[nodiscard]] std::span<const float> buffer() const noexcept { return buf_; }

private:
    std::vector<float> buf_ = std::vector<float>(1, 0.0f); // usable (silent) before prepare()
    std::size_t mask_ = 0;
    std::size_t newest_ = 0;
    std::size_t written_ = 0; // samples pushed since the last reset, saturating at the buffer size
    double maxDelay_ = 0.0;
};

} // namespace fss::dsp
