// Tests for fss/dsp/phasor.h (L1.2).
#include <fss/dsp/phasor.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstdint>
#include <limits>

using namespace fss::dsp;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE("Phase conversions: cycles <-> Phase, with wrapping", "[phasor]")
{
    CHECK(phaseFromCycles(0.0) == 0u);
    CHECK(phaseFromCycles(0.25) == 0x40000000u);
    CHECK(phaseFromCycles(0.5) == 0x80000000u);
    CHECK(phaseFromCycles(1.25) == 0x40000000u);  // wraps
    CHECK(phaseFromCycles(-0.25) == 0xC0000000u); // negative wraps to 3/4
    CHECK(phaseFromCycles(1.0 - 1e-12) == 0u);    // rounds up to a full cycle, which is 0
    CHECK(phaseFromCycles(std::numeric_limits<double>::quiet_NaN()) == 0u);
    CHECK(phaseToCycles(0xC0000000u) == 0.75);
}

TEST_CASE("Phasor: tick returns the current phase, then advances", "[phasor]")
{
    Phasor ph;
    ph.prepare(48000.0);
    ph.setFrequency(750.0); // 48000/750 = 64 samples per cycle → inc = 2^32/64 = 2^26, exact
    CHECK(ph.increment() == (1u << 26));
    CHECK(ph.tick() == 0u);
    CHECK(ph.tick() == (1u << 26));
    CHECK(ph.phase() == (2u << 26));
}

TEST_CASE("Phasor: exactly periodic when fs/f is a power of two", "[phasor]")
{
    Phasor ph;
    ph.prepare(48000.0);
    ph.setFrequency(750.0);
    for (int cycle = 0; cycle < 1000; ++cycle)
        for (int i = 0; i < 64; ++i)
            (void)ph.tick();
    CHECK(ph.phase() == 0u); // after 64 000 samples: back at exactly 0, no drift at all
}

TEST_CASE("Phasor: phase after n ticks is exactly start + n*inc mod 2^32", "[phasor][determinism]")
{
    // The D5 promise: no accumulated rounding, ever. Checked against 64-bit integer arithmetic.
    Phasor ph;
    ph.prepare(44100.0);
    ph.setFrequency(440.0);
    const Phase start = 0x12345678u;
    ph.reset(start);
    constexpr std::uint64_t n = 10'000'000; // ~3.8 minutes at 44.1 kHz
    for (std::uint64_t i = 0; i < n; ++i)
        (void)ph.tick();
    const auto expected = static_cast<Phase>((start + n * ph.increment()) & 0xFFFFFFFFull);
    CHECK(ph.phase() == expected);
}

TEST_CASE("Phasor: frequency quantization is below 0.00002 Hz", "[phasor]")
{
    const double hz = GENERATE(0.01, 0.1, 1.0, 27.5, 440.0, 4186.0, 20000.0);
    Phasor ph;
    ph.prepare(48000.0);
    ph.setFrequency(hz);
    // One step = 48000 / 2^32 ≈ 1.1e-5 Hz; rounding to the nearest step is off by at most half of that.
    CHECK_THAT(ph.frequencyHz(), WithinAbs(hz, 0.5 * 48000.0 / kPhaseOneCycle + 1e-12));
}

TEST_CASE("Phasor: changing frequency never moves the phase", "[phasor]")
{
    // This is why glides and FM don't click: frequency changes the *slope*, not the position.
    Phasor ph;
    ph.prepare(48000.0);
    ph.setFrequency(440.0);
    for (int i = 0; i < 1234; ++i)
        (void)ph.tick();
    const Phase before = ph.phase();
    ph.setFrequency(660.0);
    CHECK(ph.phase() == before);
    CHECK(ph.tick() == before);
}

TEST_CASE("Phasor: negative and above-Nyquist frequencies alias correctly", "[phasor]")
{
    Phasor a, b, c;
    for (Phasor* p : {&a, &b, &c})
        p->prepare(48000.0);
    a.setFrequency(-100.0);
    b.setFrequency(48000.0 - 100.0); // fs − 100 Hz looks exactly like −100 Hz once sampled
    c.setFrequency(100.0);
    CHECK(a.increment() == b.increment());
    CHECK(a.increment() == static_cast<Phase>(0u - c.increment())); // backwards = two's-complement negative
    CHECK_THAT(b.frequencyHz(), WithinAbs(-100.0, 1e-4));

    (void)a.tick();
    CHECK(a.phase() > 0x80000000u); // stepped backwards from 0, wrapped to just under a full cycle
}

TEST_CASE("Phasor: prepare() at a new sample rate keeps the same frequency", "[phasor]")
{
    Phasor ph;
    ph.prepare(48000.0);
    ph.setFrequency(440.0);
    ph.prepare(96000.0);
    CHECK_THAT(ph.frequencyHz(), WithinRel(440.0, 1e-7));
    CHECK(ph.increment() == static_cast<Phase>(std::llround(440.0 / 96000.0 * kPhaseOneCycle)));
}

TEST_CASE("Phasor: NaN and absurd frequencies are safe", "[phasor][robustness]")
{
    Phasor ph;
    ph.prepare(48000.0);
    ph.setFrequency(std::numeric_limits<double>::quiet_NaN());
    CHECK(ph.increment() == 0u);
    ph.setFrequency(1e300);                 // must not be undefined behaviour; any increment is acceptable
    ph.setFrequency(-std::numeric_limits<double>::infinity());
    (void)ph.tick();
    SUCCEED();
}
