// Tests for fss/dsp/waveforms.h (L1.8): the PolyBLEP / PolyBLAMP residuals, waveform ranges, and the
// point of the lesson: band-limited waveforms have far less alias energy than naive ones.
#include <fss/dsp/waveforms.h>
#include <fss/util/realtime_guard.h>

#include "alias_meter.h" // tools/render (on the include path via fss::wav)

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstddef>
#include <vector>

using namespace fss::dsp;
using Catch::Matchers::WithinAbs;

namespace {

constexpr double kFs = 48000.0;
constexpr std::size_t kN = 4096; // one exact period of every test tone (see alias_meter.h)

/// N samples of a waveform at f0 = k·fs/N, from a raw Phasor + a free function f(t, dt).
template <typename F>
std::vector<float> render(std::size_t k, F f)
{
    Phasor ph;
    ph.prepare(kFs);
    ph.setFrequency(static_cast<double>(k) * kFs / static_cast<double>(kN));
    const double dt = phaseToCycles(ph.increment());
    std::vector<float> x(kN);
    for (float& v : x)
        v = f(phaseToCycles(ph.tick()), dt);
    return x;
}

// Bin 427 = 5004 Hz: "below 5 kHz", where hearing is sharp and where aliases are easiest to hear.
constexpr std::size_t kLowBins = 427;

fss::render::AliasMeasurement measure(std::size_t k, auto f)
{
    return fss::render::measureAliasing(render(k, f), k, kLowBins);
}

} // namespace

TEST_CASE("polyBlep: zero away from the jump, cancels a jump of 2 at it", "[waveforms]")
{
    const double dt = 0.01;
    CHECK(polyBlep(0.5, dt) == 0.0);
    CHECK(polyBlep(dt, dt) == 0.0);       // exactly one sample after: x = 1, outside the residual
    CHECK(polyBlep(1.0 - dt, dt) == 0.0); // exactly one sample before: x = −1
    // Ends of the residual are continuous with 0 (no new discontinuity is created)...
    CHECK_THAT(polyBlep(dt * 0.999999, dt), WithinAbs(0.0, 1e-9));
    CHECK_THAT(polyBlep(1.0 - dt * 0.999999, dt), WithinAbs(0.0, 1e-9));
    // ...and at the jump it goes from +1 (just before) to −1 (just after): a step of −2 that cancels the
    // saw's +2 drop... so blepSaw is continuous across the wrap in the limit of a continuous phase.
    CHECK_THAT(polyBlep(1.0 - 1e-12, dt), WithinAbs(1.0, 1e-9));
    CHECK(polyBlep(0.0, dt) == -1.0);
    CHECK_THAT(static_cast<double>(blepSaw(1.0 - 1e-12, dt)), WithinAbs(0.0, 1e-6));
    CHECK_THAT(static_cast<double>(blepSaw(0.0, dt)), WithinAbs(0.0, 1e-6));
}

TEST_CASE("polyBlamp: continuous, 1/6 at the corner, and the integral of polyBlep/2", "[waveforms]")
{
    const double dt = 0.02;
    CHECK(polyBlamp(0.5, dt) == 0.0);
    CHECK_THAT(polyBlamp(0.0, dt), WithinAbs(1.0 / 6.0, 1e-12));
    CHECK_THAT(polyBlamp(1.0 - 1e-12, dt), WithinAbs(1.0 / 6.0, 1e-9));
    CHECK_THAT(polyBlamp(dt * 0.999999, dt), WithinAbs(0.0, 1e-12));
    CHECK_THAT(polyBlamp(1.0 - dt * 0.999999, dt), WithinAbs(0.0, 1e-12));

    // D(x) = ∫_{−1}^{x} residual(u) du, residual = polyBlep/2 (unit jump). Integrate numerically with the
    // midpoint rule over x in (−1, 1), i.e. phase from 1 − dt round the wrap to dt, and compare.
    const int steps = 20000;
    const double h = 2.0 / steps; // in samples
    double integral = 0.0, worst = 0.0;
    for (int i = 0; i < steps; ++i) {
        const double xm = -1.0 + (i + 0.5) * h; // midpoint, samples from the corner
        const double tm = xm < 0.0 ? 1.0 + xm * dt : xm * dt;
        integral += 0.5 * polyBlep(tm, dt) * h;
        const double xe = -1.0 + (i + 1) * h; // end of this step
        if (xe < 1.0 - 1e-9) {
            const double te = xe < 0.0 ? 1.0 + xe * dt : xe * dt;
            worst = std::max(worst, std::abs(integral - polyBlamp(te, dt)));
        }
    }
    CHECK(worst < 1e-6);
    CHECK_THAT(integral, WithinAbs(0.0, 1e-6)); // the residual has zero net area
}

TEST_CASE("waveforms: naive and band-limited outputs stay within -1..1", "[waveforms]")
{
    for (double hz : {55.0, 440.0, 3000.0, 11000.0, 23999.0}) {
        BlepOscillator osc;
        osc.prepare(kFs);
        osc.setFrequency(hz);
        const double dt = osc.phaseIncrementCycles();
        float lo = 0.0f, hi = 0.0f;
        for (int i = 0; i < 4800; ++i) {
            for (Waveform w : {Waveform::Sine, Waveform::Saw, Waveform::Square, Waveform::Triangle}) {
                const float v = osc.tick(w);
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
            const double t = static_cast<double>(i) / 4800.0;
            for (float v : {naiveSaw(t), naiveSquare(t), naiveTriangle(t), blepSaw(t, dt), blepSquare(t, dt),
                            blepTriangle(t, dt)}) {
                lo = std::min(lo, v);
                hi = std::max(hi, v);
            }
        }
        INFO(hz << " Hz: min " << lo << ", max " << hi);
        CHECK(lo >= -1.0f);
        CHECK(hi <= 1.0f);
    }
}

TEST_CASE("BlepOscillator: template and runtime tick agree bit for bit; frequency is clamped", "[waveforms]")
{
    for (Waveform w : {Waveform::Sine, Waveform::Saw, Waveform::Square, Waveform::Triangle}) {
        BlepOscillator a, b;
        a.prepare(kFs);
        b.prepare(kFs);
        a.setFrequency(1234.5);
        b.setFrequency(1234.5);
        bool same = true;
        for (int i = 0; i < 1000; ++i) {
            float ta = 0.0f;
            switch (w) {
            case Waveform::Sine: ta = a.tick<Waveform::Sine>(); break;
            case Waveform::Saw: ta = a.tick<Waveform::Saw>(); break;
            case Waveform::Square: ta = a.tick<Waveform::Square>(); break;
            case Waveform::Triangle: ta = a.tick<Waveform::Triangle>(); break;
            }
            same = same && (ta == b.tick(w));
        }
        CHECK(same);
    }
    BlepOscillator o;
    o.prepare(kFs);
    o.setFrequency(-100.0);
    CHECK(o.phaseIncrementCycles() == 0.0);
    o.setFrequency(std::nan(""));
    CHECK(o.phaseIncrementCycles() == 0.0);
    o.setFrequency(1.0e6);
    CHECK(o.phaseIncrementCycles() == 0.5);
}

TEST_CASE("BlepOscillator: tick does not allocate", "[waveforms][realtime]")
{
    BlepOscillator o;
    o.prepare(kFs);
    o.setFrequency(440.0);
    float sum = 0.0f;
    {
        fss::util::RealtimeGuard guard;
        for (int i = 0; i < 256; ++i)
            sum += o.tick(Waveform::Triangle) + o.tick<Waveform::Saw>();
    }
    CHECK(std::isfinite(sum));
}

// The lesson's measurement as a test. f0 = k·fs/N with k odd (alias_meter.h explains why odd).
// Measured (dB of alias power re total; numpy agrees to 0.1 dB):
//                       all aliases (naive -> PolyBLEP)       aliases below 5 kHz (naive -> PolyBLEP)
//   k = 19 (222.7 Hz):  saw  -22.5 -> -38.5   (16.0 better)    -30.1 ->  -78.2   (48.1 better)
//                       sq   -24.3 -> -40.4   (16.1)           -31.9 ->  -80.5   (48.6)
//                       tri  -68.8 -> -81.2   (12.4)           -79.4 -> -126.0   (46.6)
//   k = 85 (996.1 Hz):  saw  -16.1 -> -32.3   (16.3)           -23.7 ->  -72.2   (48.5)
//                       sq   -17.7 -> -33.6   (15.9)           -25.5 ->  -74.8   (49.3)
//                       tri  -49.3 -> -61.5   (12.3)           -60.2 -> -108.1   (47.9)
// Only ~12-16 dB better overall: most of the remaining alias energy sits just below Nyquist (folded from
// just above it), where PolyBLEP's 2-sample kernel barely attenuates. Below 5 kHz it's ~48 dB better.
// Bounds have a few dB of slack: they catch a sign error or a broken residual (with the BLAMP sign flipped,
// the triangle gets 5 dB WORSE than naive) without being brittle.
TEST_CASE("aliasing: PolyBLEP saw, square and triangle alias far less than naive", "[waveforms][alias]")
{
    const auto nSaw = [](double t, double) { return naiveSaw(t); };
    const auto nSq = [](double t, double) { return naiveSquare(t); };
    const auto nTri = [](double t, double) { return naiveTriangle(t); };
    const auto bSaw = [](double t, double dt) { return blepSaw(t, dt); };
    const auto bSq = [](double t, double dt) { return blepSquare(t, dt); };
    const auto bTri = [](double t, double dt) { return blepTriangle(t, dt); };

    for (std::size_t k : {19u, 85u}) { // 222.7 Hz and 996.1 Hz
        const auto check = [k](const char* name, auto naive, auto blep) {
            const auto n = measure(k, naive), b = measure(k, blep);
            INFO("k = " << k << " " << name << ": all " << n.aliasDb() << " -> " << b.aliasDb() << " dB, below 5 kHz "
                        << n.lowAliasDb() << " -> " << b.lowAliasDb() << " dB");
            CHECK(b.aliasDb() < n.aliasDb() - 10.0);
            CHECK(b.lowAliasDb() < n.lowAliasDb() - 40.0);
        };
        check("saw", nSaw, bSaw);
        check("square", nSq, bSq);
        check("triangle", nTri, bTri);
    }
}
