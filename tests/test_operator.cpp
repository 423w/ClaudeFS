// Tests for fss/synth/operator.h (L1.4): phase-modulation plumbing, and the FM spectrum itself checked
// against Bessel functions computed by the C++ standard library (std::cyl_bessel_j), independent of the
// Python sketch from L0.4.
#include <fss/synth/operator.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <algorithm>
#include <complex>
#include <cstdint>
#include <numbers>
#include <vector>

using namespace fss::synth;
using Catch::Matchers::WithinAbs;

namespace {

constexpr double kFs = 48000.0;
const fss::dsp::AdsrParams kHold{.attackMs = 1.0f, .decayMs = 1.0f, .sustain = 1.0f, .releaseMs = 100.0f};

Operator makeOp(double hz, float level)
{
    Operator op;
    op.prepare(kFs);
    op.setEnvelope(kHold);
    op.setFrequency(hz);
    op.setLevel(level);
    op.noteOn();
    for (int i = 0; i < 480; ++i) // 10 ms: let the 1 ms attack finish so the envelope is a steady 1.0
        (void)op.tick();
    return op;
}

/// Amplitude of the component at `hz` in x, by a single-bin DFT. Exact when hz sits on a bin and the
/// signal is periodic in the window (true for the frequencies used below), so no window function is needed.
double amplitudeAt(const std::vector<float>& x, double hz)
{
    std::complex<double> acc = 0.0;
    for (std::size_t n = 0; n < x.size(); ++n)
        acc += static_cast<double>(x[n]) * std::polar(1.0, -2.0 * std::numbers::pi * hz * static_cast<double>(n) / kFs);
    return 2.0 * std::abs(acc) / static_cast<double>(x.size());
}

} // namespace

TEST_CASE("radiansToPhase: pi is half a cycle, 2pi wraps to zero, big offsets are safe", "[operator]")
{
    CHECK(radiansToPhase(0.0f) == 0u);
    CHECK(std::abs(static_cast<std::int64_t>(radiansToPhase(std::numbers::pi_v<float>)) - 0x80000000ll) < 256);
    const auto twoPi = radiansToPhase(2.0f * std::numbers::pi_v<float>);
    CHECK((twoPi < 256u || twoPi > 0xFFFFFF00u)); // ≈ 0 mod one cycle (float π isn't exact)
    // −90° = 270°. Compare as a signed difference: float π is slightly *larger* than π, so the result
    // lands a few steps below 0xC0000000, and an unsigned "a − b < tol" check would wrap and fail.
    CHECK(std::abs(static_cast<std::int32_t>(radiansToPhase(-std::numbers::pi_v<float> / 2) - 0xC0000000u)) < 256);
    (void)radiansToPhase(1.0e6f); // far beyond any useful index: must not be UB
    SUCCEED();
}

TEST_CASE("Operator: phase modulation of pi radians inverts the output", "[operator]")
{
    Operator a = makeOp(440.0, 1.0f), b = makeOp(440.0, 1.0f);
    for (int i = 0; i < 1000; ++i) {
        const float plain = a.tick(0.0f);
        const float shifted = b.tick(std::numbers::pi_v<float>); // sin(x + π) = −sin(x)
        REQUIRE_THAT(shifted, WithinAbs(-plain, 1e-5));
    }
}

TEST_CASE("Operator: level scales the output; envelope gates it", "[operator]")
{
    Operator op = makeOp(1000.0, 0.25f);
    float peak = 0.0f;
    for (int i = 0; i < 4800; ++i)
        peak = std::max(peak, std::abs(op.tick()));
    CHECK_THAT(peak, WithinAbs(0.25, 1e-3));
    op.noteOff();
    for (int i = 0; i < 48000 && op.isActive(); ++i)
        (void)op.tick();
    CHECK_FALSE(op.isActive());
    CHECK(op.tick() == 0.0f);
}

TEST_CASE("2-op FM: sideband amplitudes equal |J_k(I)| (Bessel)", "[operator][fm]")
{
    // fc = 6 kHz, fm = 1 kHz: sidebands at 6000 ± 1000·k. For I ≤ 2 nothing significant reaches 0 Hz or
    // Nyquist, so every sideband is a clean, unfolded J_k(I). 4800 samples = 100 Hz bins, all on-bin.
    const double index = GENERATE(0.5, 1.0, 2.0);
    Operator mod = makeOp(1000.0, static_cast<float>(index));
    Operator car = makeOp(6000.0, 1.0f);
    // Re-align both phases to 0 at the same sample (makeOp ran them independently).
    mod.noteOn();
    car.noteOn();
    std::vector<float> y(4800);
    for (float& s : y)
        s = car.tick(mod.tick());

    INFO("index " << index);
    for (int k = -5; k <= 5; ++k) {
        const double expected = std::abs(std::cyl_bessel_j(std::abs(k), index)); // |J_−k| = |J_k|
        const double measured = amplitudeAt(y, 6000.0 + 1000.0 * k);
        INFO("k = " << k << ": expected " << expected << ", measured " << measured);
        CHECK_THAT(measured, WithinAbs(expected, 2e-4));
    }
}

TEST_CASE("2-op FM: carrier vanishes at the first zero of J0 (I = 2.4048)", "[operator][fm]")
{
    Operator mod = makeOp(1000.0, 2.404826f);
    Operator car = makeOp(6000.0, 1.0f);
    mod.noteOn();
    car.noteOn();
    std::vector<float> y(4800);
    for (float& s : y)
        s = car.tick(mod.tick());
    CHECK(amplitudeAt(y, 6000.0) < 1e-4);          // the carrier frequency is gone...
    CHECK(amplitudeAt(y, 7000.0) > 0.5);           // ...its energy moved to the sidebands (J1(2.405) = 0.519)
}
