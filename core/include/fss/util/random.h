// fss/util/random.h: a tiny random number generator that is safe on the audio thread (L1.11).
//
// Why not rand()? It's allowed to use hidden global state and locks (implementation-defined), its quality
// is often poor, and two threads calling it share one sequence. Why not std::mt19937? It's fine in RT code
// (no allocation, no locks) but it carries 2.5 KB of state per generator and is overkill for an LFO.
// Marsaglia's xorshift32 [MARSAGLIA-XORSHIFT] is three shifts and three XORs on 4 bytes of state: fast,
// deterministic (same seed → same sequence on every machine, which golden files and block-size invariance
// need), and plenty random for sample-and-hold. It must never be seeded with 0 (0 maps to 0 forever).
#pragma once

#include <cstdint>

namespace fss::util {

class XorShift32 {
public:
    explicit constexpr XorShift32(std::uint32_t seed = 0x9E3779B9u) noexcept : state_(seed ? seed : 0x9E3779B9u) {}

    constexpr void seed(std::uint32_t s) noexcept { state_ = s ? s : 0x9E3779B9u; }

    /// Next 32 random bits.
    constexpr std::uint32_t next() noexcept
    {
        std::uint32_t x = state_;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        return state_ = x;
    }

    /// Uniform in [−1, 1). The top 24 bits fill a float's mantissa exactly (no rounding bias).
    constexpr float bipolar() noexcept
    {
        return static_cast<float>(next() >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }

private:
    std::uint32_t state_;
};

} // namespace fss::util
