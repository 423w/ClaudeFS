// Tests for fss/dsp/lfo.h, fss/util/random.h and the LFO routes in the Engine (L1.11).
#include <fss/dsp/lfo.h>
#include <fss/engine.h>
#include <fss/util/random.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace fss;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

constexpr double kFs = 48000.0;

/// An LFO at fs/64 (750 Hz: absurd for an LFO, but one cycle = exactly 64 samples, so phases are exact).
dsp::Lfo lfoAt64(dsp::LfoShape shape, std::uint32_t seed = 1)
{
    dsp::Lfo l;
    l.prepare(kFs);
    l.setRate(kFs / 64.0);
    l.setShape(shape);
    l.setDelayFade(0.0f, 0.0f);
    l.restart(0, seed);
    return l;
}

std::vector<float> run(dsp::Lfo& l, int n)
{
    std::vector<float> v(static_cast<std::size_t>(n));
    for (float& x : v)
        x = l.tick();
    return v;
}

} // namespace

TEST_CASE("LFO: periodic shapes hit their defining values", "[lfo]")
{
    using dsp::LfoShape;
    auto lfo = lfoAt64(LfoShape::Sine);
    auto sine = run(lfo, 64);
    CHECK_THAT(sine[0], WithinAbs(0.0, 1e-6));
    CHECK_THAT(sine[16], WithinAbs(1.0, 1e-5));  // quarter cycle
    CHECK_THAT(sine[48], WithinAbs(-1.0, 1e-5));
    lfo = lfoAt64(LfoShape::Triangle);
    auto tri = run(lfo, 64);
    CHECK(tri[0] == 0.0f);
    CHECK(tri[16] == 1.0f);
    CHECK(tri[32] == 0.0f);
    CHECK(tri[48] == -1.0f);
    lfo = lfoAt64(LfoShape::SawUp);
    auto up = run(lfo, 64);
    CHECK(up[0] == -1.0f);
    CHECK(up[32] == 0.0f);
    lfo = lfoAt64(LfoShape::SawDown);
    auto down = run(lfo, 64);
    CHECK(down[0] == 1.0f);
    CHECK(down[32] == 0.0f);
    lfo = lfoAt64(LfoShape::Square);
    auto sq = run(lfo, 64);
    CHECK(sq[0] == 1.0f);
    CHECK(sq[31] == 1.0f);
    CHECK(sq[32] == -1.0f);
}

TEST_CASE("LFO: sample & hold holds one value per cycle; smooth random never jumps", "[lfo]")
{
    auto sh = lfoAt64(dsp::LfoShape::SampleHold, 42);
    const auto v = run(sh, 64 * 8);
    for (int c = 0; c < 8; ++c) {
        const float first = v[static_cast<std::size_t>(64 * c)];
        CHECK(first >= -1.0f);
        CHECK(first < 1.0f);
        for (int i = 1; i < 64; ++i)
            REQUIRE(v[static_cast<std::size_t>(64 * c + i)] == first);
        if (c > 0)
            CHECK(first != v[static_cast<std::size_t>(64 * (c - 1))]); // a new value each cycle
    }
    // Same seed, same sequence (renders must be reproducible).
    auto again = lfoAt64(dsp::LfoShape::SampleHold, 42);
    CHECK(run(again, 64 * 8) == v);

    auto smooth = lfoAt64(dsp::LfoShape::SmoothRandom, 42);
    const auto s = run(smooth, 64 * 8);
    float maxStep = 0.0f;
    for (std::size_t i = 1; i < s.size(); ++i)
        maxStep = std::max(maxStep, std::abs(s[i] - s[i - 1]));
    // A cosine glide across at most 2 units over 64 samples: largest step ≈ 2 · (π/2)/64 ≈ 0.049.
    CHECK(maxStep < 0.06f);
}

TEST_CASE("LFO: delay then a linear fade-in", "[lfo]")
{
    dsp::Lfo l;
    l.prepare(kFs);
    l.setRate(5.0);
    l.setDelayFade(10.0f, 10.0f); // 480 samples of nothing, then 480 samples of ramp
    l.restart(0, 1);
    std::vector<float> g;
    for (int i = 0; i < 1200; ++i) {
        (void)l.tick();
        g.push_back(l.fadeGain());
    }
    CHECK(g[479] == 0.0f);
    CHECK_THAT(g[480 + 239], WithinAbs(0.5, 0.01));
    CHECK(g[1100] == 1.0f);
}

TEST_CASE("XorShift32: deterministic, never stuck, roughly uniform", "[lfo][random]")
{
    util::XorShift32 a(7), b(7), zero(0);
    double sum = 0.0;
    float lo = 1.0f, hi = -1.0f;
    for (int i = 0; i < 100000; ++i) {
        const float x = a.bipolar();
        REQUIRE(x == b.bipolar());
        sum += x;
        lo = std::min(lo, x);
        hi = std::max(hi, x);
    }
    CHECK(std::abs(sum / 100000.0) < 0.01); // mean ≈ 0
    CHECK(lo < -0.999f);
    CHECK(hi > 0.999f);
    CHECK(hi < 1.0f);
    CHECK(zero.next() != 0u); // seed 0 is replaced, or it would stay 0 forever
}

namespace {

/// A pure 440 Hz sine voice (algorithm 8, op A only) with the given routes; returns 1 s of output.
std::vector<float> sineNote(float vibratoSemis, float wheel)
{
    synth::Patch p;
    p.algorithm = synth::kAlgorithms[7];
    p.algorithmNumber = 8;
    p.algorithm.carrier = {1.0f, 0.0f, 0.0f, 0.0f};
    p.ops[0] = {.level = 0.5f, .env = {.attackMs = 1.0f, .decayMs = 1.0f, .sustain = 1.0f, .releaseMs = 100.0f}};
    p.lfo1 = {.rateHz = 5.0f, .shape = dsp::LfoShape::Sine, .keySync = true};
    p.routes.vibratoSemitones = vibratoSemis;
    p.routes.wheelVibratoSemitones = 0.5f;
    Engine eng;
    eng.prepare(kFs, 512);
    eng.setPatch(p);
    std::vector<float> l(48000), r(48000);
    const Event ev[] = {Event::controller(0, 1, wheel), Event::noteOn(0, 69, 1.0f)};
    eng.process(ev, l.data(), r.data(), 48000);
    return l;
}

/// Lowest and highest instantaneous frequency, from the spacing of upward zero crossings (interpolated).
std::pair<double, double> frequencyRange(const std::vector<float>& x)
{
    double lastCross = -1.0, lo = 1e9, hi = 0.0;
    for (std::size_t n = 2400; n < x.size(); ++n) { // skip the attack and the mod wheel's 10 ms glide
        if (x[n - 1] < 0.0f && x[n] >= 0.0f) {
            const double t = static_cast<double>(n - 1) + x[n - 1] / (x[n - 1] - x[n]);
            if (lastCross > 0.0) {
                const double f = kFs / (t - lastCross);
                lo = std::min(lo, f);
                hi = std::max(hi, f);
            }
            lastCross = t;
        }
    }
    return {lo, hi};
}

} // namespace

TEST_CASE("Engine: vibrato depth is in semitones (440 Hz +/- 1 st = 415.3..466.2 Hz)", "[lfo][engine]")
{
    const auto [lo, hi] = frequencyRange(sineNote(1.0f, 0.0f));
    CHECK_THAT(lo, WithinRel(440.0 * std::exp2(-1.0 / 12.0), 0.003));
    CHECK_THAT(hi, WithinRel(440.0 * std::exp2(1.0 / 12.0), 0.003));
}

TEST_CASE("Engine: the mod wheel adds vibrato; at 0 the note is untouched (bit-exact)", "[lfo][engine]")
{
    CHECK(sineNote(0.0f, 0.0f) == sineNote(0.0f, 0.0f));
    const auto [lo, hi] = frequencyRange(sineNote(0.0f, 1.0f)); // full wheel x 0.5 st
    CHECK_THAT(lo, WithinRel(440.0 * std::exp2(-0.5 / 12.0), 0.003));
    CHECK_THAT(hi, WithinRel(440.0 * std::exp2(0.5 / 12.0), 0.003));
    const auto [lo0, hi0] = frequencyRange(sineNote(0.0f, 0.0f));
    CHECK_THAT(lo0, WithinRel(440.0, 1e-4));
    CHECK_THAT(hi0, WithinRel(440.0, 1e-4));
}
