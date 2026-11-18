// Tests for fss/util/denormal_guard.h and the L2.6 speed-ups (fast pow2, cast rounding in the Phasor).
#include <fss/dsp/phasor.h>
#include <fss/engine.h>
#include <fss/util/denormal_guard.h>
#include <fss/util/math.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <limits>
#include <random>

using namespace fss;

namespace {
// volatile so the compiler can't fold the multiply at compile time (constant folding ignores MXCSR).
volatile float gTiny = 1e-30f;
volatile float gScale = 1e-10f;
} // namespace

TEST_CASE("DenormalGuard: flushes denormal results inside the scope and restores the mode after", "[denormal]")
{
    if (!util::DenormalGuard::supported())
        SKIP("no MXCSR on this architecture");
    const float before = gTiny * gScale; // 1e-40: a denormal
    CHECK(before != 0.0f);
    CHECK(std::fpclassify(before) == FP_SUBNORMAL);
    {
        const util::DenormalGuard ftz;
        CHECK(gTiny * gScale == 0.0f); // flushed to zero
        volatile float denormalIn = before;
        CHECK(denormalIn * 1.0f == 0.0f); // DAZ: a denormal input reads as 0
    }
    CHECK(gTiny * gScale == before); // the previous mode is back
}

TEST_CASE("Engine::process leaves the caller's floating-point mode as it found it", "[denormal][engine]")
{
    if (!util::DenormalGuard::supported())
        SKIP("no MXCSR on this architecture");
    Engine eng;
    eng.prepare(48000.0, 64);
    float l[64], r[64];
    const Event on[] = {Event::noteOn(0, 60, 1.0f)};
    eng.process(on, l, r, 64);
    CHECK(gTiny * gScale != 0.0f); // still producing denormals out here: the guard was undone
}

TEST_CASE("pow2 matches exp2 to ~1e-15 relative over the pitch and cutoff ranges", "[denormal][math]")
{
    double worst = 0.0;
    for (double x = -20.0; x <= 20.0; x += 0.0137)
        worst = std::fmax(worst, std::abs(util::pow2(x) / std::exp2(x) - 1.0));
    CHECK(worst < 1e-14);
    float worstF = 0.0f;
    for (float x = -10.0f; x <= 15.0f; x += 0.0137f)
        worstF = std::fmax(worstF, std::abs(util::pow2(x) / std::exp2(x) - 1.0f));
    CHECK(worstF < 4e-6f); // float: a few ulp (the float multiply x*ln2 is rounded)
}

TEST_CASE("Phasor: the cast rounding gives exactly llround's increments", "[denormal][phasor]")
{
    std::mt19937 rng(9);
    std::uniform_real_distribution<double> hz(-30000.0, 30000.0);
    dsp::Phasor a;
    a.prepare(48000.0);
    for (int i = 0; i < 200000; ++i) {
        const double f = hz(rng);
        a.setFrequency(f);
        const auto expected = static_cast<dsp::Phase>(std::llround(f * dsp::kPhaseOneCycle / 48000.0));
        REQUIRE(a.increment() == expected);
    }
}
