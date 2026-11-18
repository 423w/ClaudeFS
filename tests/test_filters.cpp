// Tests for fss/dsp/one_pole.h and fss/dsp/biquad.h (L1.9): the computed response H(e^jw) must match the
// response *measured* by running the filter (the DFT of its impulse response) within 0.1 dB.
#include <fss/dsp/biquad.h>
#include <fss/dsp/one_pole.h>

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

/// DFT of a (decayed) impulse response at one frequency: Σ h[n]·e^{−jωn}. For an IR that has decayed to
/// nothing, this *is* H(e^{jω}), measured from what the filter actually did.
std::complex<double> dftAt(const std::vector<float>& h, double hz)
{
    // e^{−jωn} by repeated rotation (one complex multiply per sample instead of a sin and a cos): the
    // rounding drift over 32768 steps is ~1e-12, far below what the 0.1 dB check can see.
    const std::complex<double> step = std::polar(1.0, -2.0 * std::numbers::pi * hz / kFs);
    std::complex<double> rot = 1.0, sum = 0.0;
    for (float v : h) {
        sum += static_cast<double>(v) * rot;
        rot *= step;
    }
    return sum;
}

double db(std::complex<double> h) { return 20.0 * std::log10(std::abs(h) + 1e-300); }

/// 40 log-spaced test frequencies from 20 Hz to 20 kHz.
std::vector<double> testFrequencies()
{
    std::vector<double> f;
    for (int i = 0; i < 40; ++i)
        f.push_back(20.0 * std::pow(1000.0, i / 39.0));
    return f;
}

} // namespace

TEST_CASE("one-pole: coefficient is 1 - exp(-2 pi fc/fs), and it stays stable at any cutoff", "[filter][onepole]")
{
    CHECK_THAT(onePoleCoefficient(1000.0, kFs), WithinAbs(1.0 - std::exp(-2.0 * std::numbers::pi / 48.0), 1e-15));
    CHECK(onePoleCoefficient(0.0, kFs) == 0.0);
    CHECK(onePoleCoefficient(1e9, kFs) == 1.0); // far above Nyquist: pass-through, never > 1
    // The naive 2 pi fc/fs would exceed 2 (unstable) above fs/pi; the exponential form never exceeds 1.
    CHECK(onePoleCoefficient(kFs / std::numbers::pi * 1.5, kFs) < 1.0);
}

TEST_CASE("one-pole: measured response matches responseAt within 0.1 dB (LP and HP)", "[filter][onepole]")
{
    const double fc = GENERATE(30.0, 500.0, 5000.0);
    const bool highPass = GENERATE(false, true);
    OnePole f;
    f.prepare(kFs);
    f.setCutoff(fc);
    std::vector<float> h(1 << 15);
    for (std::size_t n = 0; n < h.size(); ++n)
        h[n] = highPass ? f.tickHighPass(n == 0 ? 1.0f : 0.0f) : f.tickLowPass(n == 0 ? 1.0f : 0.0f);
    for (double hz : testFrequencies()) {
        INFO("fc " << fc << " Hz, " << (highPass ? "HP" : "LP") << ", at " << hz << " Hz");
        CHECK_THAT(db(dftAt(h, hz)), WithinAbs(db(f.responseAt(hz, highPass)), 0.1));
    }
}

TEST_CASE("biquad: measured response matches responseAt within 0.1 dB for every RBJ type", "[filter][biquad]")
{
    const auto type = GENERATE(BiquadType::LowPass, BiquadType::HighPass, BiquadType::BandPass, BiquadType::Notch,
                               BiquadType::Peak, BiquadType::LowShelf, BiquadType::HighShelf);
    const double fc = GENERATE(100.0, 1000.0, 10000.0);
    const double q = GENERATE(0.7071, 4.0);
    const BiquadCoeffs c = designBiquad(type, kFs, fc, q, 9.0);
    REQUIRE(poleRadius(c) < 1.0);

    Biquad f;
    f.setCoeffs(c);
    std::vector<float> h(1 << 15); // Q 4 at 100 Hz rings for ~0.1 s: 0.7 s of IR has decayed to nothing
    for (std::size_t n = 0; n < h.size(); ++n)
        h[n] = f.tick(n == 0 ? 1.0f : 0.0f);

    for (double hz : testFrequencies()) {
        const std::complex<double> computed = responseAt(c, hz, kFs);
        const std::complex<double> measured = dftAt(h, hz);
        INFO("type " << static_cast<int>(type) << ", fc " << fc << ", Q " << q << ", at " << hz << " Hz: computed "
                     << db(computed) << " dB, measured " << db(measured) << " dB");
        if (db(computed) > -60.0)
            CHECK_THAT(db(measured), WithinAbs(db(computed), 0.1));
        else // deep stopband or a notch's null: dB is meaningless there, compare linear gain instead
            CHECK(std::abs(measured - computed) < 1e-4);
    }
}

TEST_CASE("biquad: RBJ designs hit their textbook values", "[filter][biquad]")
{
    // Butterworth LP/HP: exactly -3.01 dB at the cutoff, 0 dB far from it.
    const auto lp = designBiquad(BiquadType::LowPass, kFs, 1000.0, std::numbers::sqrt2 / 2);
    CHECK_THAT(db(responseAt(lp, 1000.0, kFs)), WithinAbs(-3.0103, 1e-3));
    CHECK_THAT(db(responseAt(lp, 1.0, kFs)), WithinAbs(0.0, 1e-6));
    // A resonant LP peaks at about +20*log10(Q) near the cutoff (Q 10: ~ +20 dB).
    const auto res = designBiquad(BiquadType::LowPass, kFs, 1000.0, 10.0);
    CHECK_THAT(db(responseAt(res, 1000.0, kFs)), WithinAbs(20.0, 0.1));
    // Notch: (numerically) zero at the centre; band-pass: 0 dB at the centre.
    CHECK(std::abs(responseAt(designBiquad(BiquadType::Notch, kFs, 1000.0, 2.0), 1000.0, kFs)) < 1e-9);
    CHECK_THAT(db(responseAt(designBiquad(BiquadType::BandPass, kFs, 1000.0, 2.0), 1000.0, kFs)), WithinAbs(0.0, 1e-9));
    // Peak and shelves: the requested gain where it should be.
    CHECK_THAT(db(responseAt(designBiquad(BiquadType::Peak, kFs, 1000.0, 1.0, 6.0), 1000.0, kFs)), WithinAbs(6.0, 1e-9));
    CHECK_THAT(db(responseAt(designBiquad(BiquadType::LowShelf, kFs, 1000.0, 0.7071, 6.0), 10.0, kFs)), WithinAbs(6.0, 0.01));
    CHECK_THAT(db(responseAt(designBiquad(BiquadType::HighShelf, kFs, 1000.0, 0.7071, -6.0), 20000.0, kFs)),
               WithinAbs(-6.0, 0.05));
}

TEST_CASE("biquad: DF1 and TDF2 compute the same filter", "[filter][biquad]")
{
    const auto c = designBiquad(BiquadType::LowPass, kFs, 2000.0, 3.0);
    BiquadDf1<float> a;
    BiquadTdf2<float> b;
    a.setCoeffs(c);
    b.setCoeffs(c);
    std::mt19937 rng(1);
    std::uniform_real_distribution<float> noise(-1.0f, 1.0f);
    double maxDiff = 0.0;
    for (int n = 0; n < 48000; ++n) {
        const float x = noise(rng);
        maxDiff = std::fmax(maxDiff, std::abs(static_cast<double>(a.tick(x) - b.tick(x))));
    }
    CHECK(maxDiff < 1e-5); // same maths, different rounding: differences at float-noise level only
}
