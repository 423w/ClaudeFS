// Tests for fss/dsp/sine.h (L1.2): accuracy of each sine against double-precision std::sin, plus
// symmetry properties. Benchmarks are hidden ([.]): run them in Release with
//     build\release\tests\fss_tests.exe "[benchmark]"
#include <fss/dsp/sine.h>

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstdint>
#include <numbers>

using namespace fss::dsp;
using Catch::Matchers::WithinAbs;

namespace {

// Max |error| in dB relative to a full-scale sine, over 2^22 evenly spaced phases plus the odd offset
// (so we don't only test the table's own grid points, where linear interpolation is exact).
template <typename F>
double maxErrorDb(F sine)
{
    double worst = 0.0;
    for (std::uint64_t k = 0; k < (1u << 22); ++k) {
        const auto p = static_cast<Phase>(k * 1024u + 333u);
        const double ref = std::sin(2.0 * std::numbers::pi * phaseToCycles(p));
        worst = std::max(worst, std::abs(static_cast<double>(sine(p)) - ref));
    }
    return 20.0 * std::log10(worst);
}

} // namespace

TEST_CASE("sine accuracy: std::sin reference is float-exact", "[sine][accuracy]")
{
    const double db = maxErrorDb(sineStd);
    INFO("sineStd max error " << db << " dB");
    CHECK(db < -140.0); // just float rounding (2^-25 ≈ −150 dB)
}

TEST_CASE("sine accuracy: table + linear interpolation beats -100 dB", "[sine][accuracy]")
{
    const double db = maxErrorDb(sineTable);
    INFO("sineTable max error " << db << " dB (theory (2pi/1024)^2/8 = -106.5 dB)");
    CHECK(db < -100.0); // exit criterion asks for < −90
    CHECK(db > -110.0); // and it should be close to theory, or the test isn't testing what we think
}

TEST_CASE("sine accuracy: degree-7 minimax polynomial beats -118 dB", "[sine][accuracy]")
{
    const double db = maxErrorDb(sinePoly);
    INFO("sinePoly max error " << db << " dB (fit: -124.6 dB in double)");
    CHECK(db < -118.0);
}

TEST_CASE("sine: exact values at the quarter points", "[sine]")
{
    for (auto f : {sineStd, sineTable, sinePoly}) {
        CHECK_THAT(f(0u), WithinAbs(0.0, 1e-7));
        CHECK_THAT(f(0x40000000u), WithinAbs(1.0, 1e-6));  // 90°
        CHECK_THAT(f(0x80000000u), WithinAbs(0.0, 1e-6));  // 180°
        CHECK_THAT(f(0xC0000000u), WithinAbs(-1.0, 1e-6)); // 270°
    }
}

TEST_CASE("sine: odd symmetry sin(-x) = -sin(x) holds exactly for table and polynomial", "[sine]")
{
    // Exact symmetry means no DC offset and no even harmonics from the approximation itself.
    for (std::uint32_t k = 1; k < 100000; ++k) {
        const Phase p = k * 42947u; // scattered phases
        const Phase neg = 0u - p;   // −p mod 2^32
        REQUIRE(sinePoly(neg) == -sinePoly(p));
    }
    // The table is symmetric on its grid; between grid points the 22-bit fraction differs for p and −p
    // (frac vs 1 − frac), so allow one float rounding.
    for (std::uint32_t k = 1; k < 100000; ++k) {
        const Phase p = k * 42947u;
        REQUIRE_THAT(sineTable(0u - p), WithinAbs(-sineTable(p), 1e-7));
    }
}

TEST_CASE("sine table: built at compile time with exact symmetry", "[sine]")
{
    using fss::dsp::detail::kSineTable;
    static_assert(kSineTable.size() == 1025);
    static_assert(kSineTable[0] == 0.0f && kSineTable[256] == 1.0f && kSineTable[1024] == 0.0f);
    static_assert(kSineTable[768] == -1.0f);
    static_assert(kSineTable[100] == kSineTable[412]);  // sin(π − x) = sin(x)
    static_assert(kSineTable[100] == -kSineTable[612]); // sin(π + x) = −sin(x)
    SUCCEED();
}

TEST_CASE("sine benchmarks", "[.][benchmark]")
{
    // A phasor-like sweep through 4096 phases per run; summing keeps the optimizer from deleting the work.
    constexpr Phase inc = 0x01234567u;
    auto run = [&](auto sine) {
        Phase p = 0;
        float acc = 0.0f;
        for (int i = 0; i < 4096; ++i, p += inc)
            acc += sine(p);
        return acc;
    };
    BENCHMARK("sineStd   x4096") { return run(sineStd); };
    BENCHMARK("sineTable x4096") { return run(sineTable); };
    BENCHMARK("sinePoly  x4096") { return run(sinePoly); };
}
