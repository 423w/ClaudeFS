// Tests for fss/dsp/svf.h (L1.10): the TPT SVF has the RBJ biquad's response at a static cutoff, and stays
// finite and bounded under any modulation we can throw at it.
#include <fss/dsp/biquad.h>
#include <fss/dsp/svf.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <complex>
#include <numbers>
#include <random>
#include <vector>

using namespace fss::dsp;
using Catch::Matchers::WithinAbs;

namespace {

constexpr double kFs = 48000.0;

double db(std::complex<double> h) { return 20.0 * std::log10(std::abs(h) + 1e-300); }

std::complex<double> dftAt(const std::vector<float>& h, double hz)
{
    const std::complex<double> step = std::polar(1.0, -2.0 * std::numbers::pi * hz / kFs);
    std::complex<double> rot = 1.0, sum = 0.0;
    for (float v : h) {
        sum += static_cast<double>(v) * rot;
        rot *= step;
    }
    return sum;
}

BiquadType biquadTypeFor(SvfMode m)
{
    switch (m) {
    case SvfMode::LowPass: return BiquadType::LowPass;
    case SvfMode::BandPass: return BiquadType::BandPass;
    case SvfMode::HighPass: return BiquadType::HighPass;
    case SvfMode::Notch: return BiquadType::Notch;
    }
    return BiquadType::LowPass;
}

} // namespace

TEST_CASE("SVF: measured response matches responseAt, and equals the RBJ biquad's, at a static cutoff", "[filter][svf]")
{
    const auto mode = GENERATE(SvfMode::LowPass, SvfMode::BandPass, SvfMode::HighPass, SvfMode::Notch);
    const float fc = GENERATE(100.0f, 1000.0f, 10000.0f);
    const float q = GENERATE(0.7071f, 5.0f);
    Svf svf;
    svf.prepare(kFs);
    svf.set(fc, q);
    const BiquadCoeffs bq = designBiquad(biquadTypeFor(mode), kFs, fc, q);

    std::vector<float> h(1 << 15);
    for (std::size_t n = 0; n < h.size(); ++n)
        h[n] = svf.tick(n == 0 ? 1.0f : 0.0f, mode);

    for (int i = 0; i < 40; ++i) {
        const double hz = 20.0 * std::pow(1000.0, i / 39.0);
        const auto computed = svf.responseAt(hz, mode);
        const auto rbj = responseAt(bq, hz, kFs);
        const auto measured = dftAt(h, hz);
        INFO("mode " << static_cast<int>(mode) << ", fc " << fc << ", Q " << q << ", at " << hz << " Hz: computed "
                     << db(computed) << " dB, RBJ " << db(rbj) << " dB, measured " << db(measured) << " dB");
        // Same analog prototype, same bilinear transform, same prewarping: the same filter (to float
        // precision of the cutoff, which went through float tan()).
        CHECK(std::abs(computed - rbj) < 1e-5);
        if (db(computed) > -60.0)
            CHECK_THAT(db(measured), WithinAbs(db(computed), 0.1));
        else
            CHECK(std::abs(measured - computed) < 1e-4);
    }
}

TEST_CASE("SVF: survives the modulation torture test (50 Hz sweep, 100 Hz..15 kHz, Q 10)", "[filter][svf]")
{
    Svf svf;
    svf.prepare(kFs);
    std::mt19937 rng(3);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    float peak = 0.0f;
    bool finite = true;
    for (int n = 0; n < 48000; ++n) {
        const double lfo = 0.5 + 0.5 * std::sin(2.0 * std::numbers::pi * 50.0 * n / kFs);
        svf.set(static_cast<float>(100.0 * std::pow(150.0, lfo)), 10.0f);
        const Svf::Out o = svf.tick(noise(rng));
        finite = finite && std::isfinite(o.low) && std::isfinite(o.band) && std::isfinite(o.high);
        peak = std::max({peak, std::abs(o.low), std::abs(o.high)});
    }
    CHECK(finite);
    // Unit-level noise through a Q-10 resonance peaks at a few units; a blow-up would be orders larger.
    CHECK(peak < 30.0f);
}

TEST_CASE("SVF: random cutoff and Q on every sample never produce non-finite or runaway output (fuzz)", "[filter][svf]")
{
    const auto seed = GENERATE(1u, 2u, 3u, 4u);
    Svf svf;
    svf.prepare(kFs);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f), noise(-1.0f, 1.0f);
    float peak = 0.0f;
    bool finite = true;
    for (int n = 0; n < 100000; ++n) {
        // Cutoff anywhere from 10 Hz to beyond Nyquist (it's clamped), Q from 0.5 to 50.
        svf.set(10.0f * std::pow(3000.0f, unit(rng)), 0.5f * std::pow(100.0f, unit(rng)));
        const Svf::Out o = svf.tick(noise(rng));
        finite = finite && std::isfinite(o.low) && std::isfinite(o.band) && std::isfinite(o.high);
        peak = std::max({peak, std::abs(o.low), std::abs(o.band), std::abs(o.high)});
    }
    CHECK(finite);
    CHECK(peak < 1000.0f);
}
