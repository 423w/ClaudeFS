// fss/dsp/sine.h: three ways to turn a Phase into sin(2π·phase) (L1.2).
//
//   sineStd(p)    std::sin in double. The reference: exact to float precision, and the slowest.
//   sineTable(p)  1024-entry table + linear interpolation. Max error ≈ (2π/N)²/8 = −106.5 dB.
//   sinePoly(p)   fold to a quarter wave, then a degree-7 odd polynomial (minimax fit). ≈ −123 dB.
//
// Accuracy is measured in tests/test_sine.cpp and speed with Catch2 benchmarks (`fss_tests "[benchmark]"`);
// the L1.2 journal has the numbers and says which one operators use.
//
// Why −90 dB is the bar (exit criterion): an error 90 dB below the signal sits under 16-bit
// quantization noise (−98 dB for a full-scale sine) and far below anything audible in a synth.
// But in FM, a sine's errors are *modulated* too, so a cleaner sine keeps the sidebands cleaner.
#pragma once

#include "fss/dsp/phasor.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace fss::dsp {

// ---- reference --------------------------------------------------------------------------------

[[nodiscard]] inline float sineStd(Phase p) noexcept
{
    return static_cast<float>(std::sin(2.0 * std::numbers::pi * phaseToCycles(p)));
}

// ---- table ------------------------------------------------------------------------------------

namespace detail {

// sin(x) for x in [−π/2, π/2] by Taylor series, usable at compile time. (std::sin isn't constexpr in
// C++20.) 13 terms reach x^25. At x = π/2 the next term is ~1e-23, far below double precision.
constexpr double constexprSinQuarter(double x)
{
    double term = x, sum = x;
    const double x2 = x * x;
    for (int k = 1; k <= 12; ++k) {
        term *= -x2 / static_cast<double>((2 * k) * (2 * k + 1));
        sum += term;
    }
    return sum;
}

inline constexpr int kSineTableBits = 10;                  // 1024 points per cycle: 4 KB of float, fits in L1 cache
inline constexpr int kSineTableSize = 1 << kSineTableBits;

// N+1 entries: entry N repeats entry 0 (the "guard point"), so interpolation between the last point and
// the first never needs a wrap-around check.
// Only the first quarter is computed; the rest comes from sine's symmetries, which makes the table
// *exactly* symmetric (sin(π − x) = sin(x), sin(π + x) = −sin(x)) and keeps compile time low.
constexpr std::array<float, kSineTableSize + 1> makeSineTable()
{
    std::array<float, kSineTableSize + 1> t{};
    constexpr int quarter = kSineTableSize / 4, half = kSineTableSize / 2;
    for (int i = 0; i <= quarter; ++i)
        t[i] = static_cast<float>(constexprSinQuarter(2.0 * std::numbers::pi * i / kSineTableSize));
    for (int i = quarter + 1; i <= half; ++i)
        t[i] = t[half - i];
    for (int i = half + 1; i <= kSineTableSize; ++i)
        t[i] = -t[i - half];
    return t;
}

// Built by the compiler: no start-up cost, no static-initialization order problems, no hidden lock (a
// function-local `static` table would check a guard variable on every call). Lives in read-only data.
inline constexpr auto kSineTable = makeSineTable();

} // namespace detail

[[nodiscard]] inline float sineTable(Phase p) noexcept
{
    constexpr int fracBits = 32 - detail::kSineTableBits;      // 22 bits left over for interpolation
    constexpr std::uint32_t fracMask = (1u << fracBits) - 1;
    constexpr float fracScale = 1.0f / static_cast<float>(1u << fracBits);

    const std::uint32_t i = p >> fracBits;                     // top 10 bits: which table segment
    const float frac = static_cast<float>(p & fracMask) * fracScale; // position within it, [0, 1). Exact:
                                                               // 22 bits fit float's 24-bit mantissa
    const float a = detail::kSineTable[i];
    const float b = detail::kSineTable[i + 1];                 // guard point makes i + 1 = 1024 safe
    return a + frac * (b - a);
}

// ---- polynomial -------------------------------------------------------------------------------

[[nodiscard]] inline float sinePoly(Phase p) noexcept
{
    // 1. Centre the cycle on 0: reinterpret as signed, so v covers [−½, ½) cycle.
    //    (uint32 → int32 is defined as modulo 2^32 since C++20.)
    std::int64_t v = static_cast<std::int32_t>(p);
    // 2. Fold into [−¼, ¼] cycle using sin(½ − s) = sin(s) and sin(−½ − s) = sin(s). Exact, in integers.
    constexpr std::int64_t kQuarter = std::int64_t{1} << 30, kHalf = std::int64_t{1} << 31;
    if (v > kQuarter)
        v = kHalf - v;
    else if (v < -kQuarter)
        v = -kHalf - v;
    // 3. s in cycles, |s| ≤ ¼. Odd polynomial in s ≈ sin(2πs). Coefficients: near-minimax fit (Lawson
    //    iteration on Chebyshev nodes, numpy; see the L1.2 journal). Max error 5.9e-7 in double
    //    (−124.6 dB), −122.8 dB measured in float. A Taylor series of the same degree is ~270× worse
    //    (1.6e-4 at |s| = ¼), because minimax spreads the error evenly instead of piling it up at the edge.
    const float s = static_cast<float>(v) * (1.0f / 4294967296.0f);
    const float s2 = s * s;
    return s * (6.28316404f + s2 * (-41.3371423f + s2 * (81.3407664f + s2 * -70.9934068f)));
}

} // namespace fss::dsp
