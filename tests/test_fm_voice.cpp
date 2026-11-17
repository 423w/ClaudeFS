// Tests for fss/synth/algorithm.h and fm_voice.h (L1.5).
#include <fss/synth/fm_voice.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <random>

using namespace fss::synth;
using Catch::Matchers::WithinAbs;

namespace {

constexpr double kFs = 48000.0;
const fss::dsp::AdsrParams kEnv{.attackMs = 3.0f, .decayMs = 200.0f, .sustain = 0.6f, .releaseMs = 50.0f};

void setupOp(Operator& op, double hz, float level)
{
    op.prepare(kFs);
    op.setEnvelope(kEnv);
    op.setFrequency(hz);
    op.setLevel(level);
}

void setupVoice(FmVoice& v, const double (&hz)[kNumOps], const float (&level)[kNumOps])
{
    v.prepare(kFs);
    for (int i = 0; i < kNumOps; ++i)
        setupOp(v.op(i), hz[i], level[i]);
}

} // namespace

TEST_CASE("algorithms: every preset edge points forward, carriers sum to 1, no feedback", "[algorithm]")
{
    for (const Algorithm& a : kAlgorithms) {
        INFO(a.name);
        float sum = 0.0f;
        for (int s = 0; s < kNumOps; ++s) {
            sum += a.carrier[static_cast<std::size_t>(s)];
            for (int d = 0; d < kNumOps; ++d)
                if (a.mod[static_cast<std::size_t>(s)][static_cast<std::size_t>(d)] != 0.0f)
                    CHECK(s < d); // forward edges only: presets need no back-edge delay
        }
        CHECK_THAT(sum, WithinAbs(1.0, 1e-6));
    }
}

TEST_CASE("FmVoice: algorithm 1 equals four operators wired by hand", "[fmvoice]")
{
    const double hz[] = {440.0 * 3, 440.0 * 2, 440.0 * 1, 440.0};
    const float lvl[] = {0.8f, 1.2f, 2.0f, 0.5f};
    FmVoice v;
    setupVoice(v, hz, lvl);
    Operator a, b, c, d;
    setupOp(a, hz[0], lvl[0]);
    setupOp(b, hz[1], lvl[1]);
    setupOp(c, hz[2], lvl[2]);
    setupOp(d, hz[3], lvl[3]);
    v.noteOn();
    for (Operator* op : {&a, &b, &c, &d})
        op->noteOn();
    for (int n = 0; n < 20000; ++n) {
        if (n == 15000) {
            v.noteOff();
            for (Operator* op : {&a, &b, &c, &d})
                op->noteOff();
        }
        const float manual = d.tick(c.tick(b.tick(a.tick(0.0f))));
        REQUIRE(v.tick(kAlgorithms[0]) == manual); // exactly: same operations in the same order
    }
}

TEST_CASE("FmVoice: algorithm 5 is two 2-op stacks mixed at 1/2", "[fmvoice]")
{
    const double hz[] = {2000.0, 500.0, 1100.0, 550.0};
    const float lvl[] = {1.5f, 1.0f, 3.0f, 1.0f};
    FmVoice v;
    setupVoice(v, hz, lvl);
    Operator a, b, c, d;
    setupOp(a, hz[0], lvl[0]);
    setupOp(b, hz[1], lvl[1]);
    setupOp(c, hz[2], lvl[2]);
    setupOp(d, hz[3], lvl[3]);
    v.noteOn();
    for (Operator* op : {&a, &b, &c, &d})
        op->noteOn();
    for (int n = 0; n < 5000; ++n) {
        const float manual = 0.5f * b.tick(a.tick(0.0f)) + 0.5f * d.tick(c.tick(0.0f));
        REQUIRE_THAT(v.tick(kAlgorithms[4]), WithinAbs(manual, 1e-6));
    }
}

TEST_CASE("FmVoice: feedback amount 0 reproduces the no-feedback result exactly", "[fmvoice][feedback]")
{
    const double hz[] = {220.0, 220.0, 440.0, 220.0};
    const float lvl[] = {1.0f, 1.0f, 1.0f, 1.0f};
    Algorithm withZeroFb = kAlgorithms[0];
    withZeroFb.mod[0][0] = 0.0f; // an explicit zero on the diagonal...
    Algorithm withFb = kAlgorithms[0];
    withFb.mod[0][0] = 1.5f;     // ...versus real feedback, to prove the test can tell them apart
    FmVoice a, b, c;
    setupVoice(a, hz, lvl);
    setupVoice(b, hz, lvl);
    setupVoice(c, hz, lvl);
    a.noteOn();
    b.noteOn();
    c.noteOn();
    bool differs = false;
    for (int n = 0; n < 5000; ++n) {
        const float plain = a.tick(kAlgorithms[0]);
        REQUIRE(b.tick(withZeroFb) == plain);
        differs = differs || c.tick(withFb) != plain;
    }
    CHECK(differs);
}

TEST_CASE("FmVoice: any matrix in range gives finite output bounded by the carrier gains", "[fmvoice][property]")
{
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> amt(0.0f, 8.0f), lvl(0.0f, 10.0f), gain(0.0f, 1.0f);
    std::uniform_real_distribution<double> hz(20.0, 8000.0);
    for (int trial = 0; trial < 200; ++trial) {
        Algorithm alg{};
        for (auto& row : alg.mod)
            for (float& m : row)
                m = amt(rng); // dense, with every back edge and self-feedback present
        float bound = 0.0f;
        double f[kNumOps];
        float l[kNumOps];
        for (int i = 0; i < kNumOps; ++i) {
            alg.carrier[static_cast<std::size_t>(i)] = gain(rng);
            f[i] = hz(rng);
            l[i] = lvl(rng);
            bound += alg.carrier[static_cast<std::size_t>(i)] * l[i]; // |op output| <= level
        }
        FmVoice v;
        setupVoice(v, f, l);
        v.noteOn();
        for (int n = 0; n < 2000; ++n) {
            const float y = v.tick(alg);
            REQUIRE(std::isfinite(y));
            REQUIRE(std::abs(y) <= bound * 1.0001f + 1e-6f);
        }
    }
}

TEST_CASE("FmVoice: a voice is active while any carrier is", "[fmvoice]")
{
    const double hz[] = {440.0, 440.0, 440.0, 440.0};
    const float lvl[] = {1.0f, 1.0f, 1.0f, 1.0f};
    FmVoice v;
    setupVoice(v, hz, lvl);
    CHECK_FALSE(v.isActive(kAlgorithms[0]));
    v.noteOn();
    CHECK(v.isActive(kAlgorithms[0]));
    v.noteOff();
    for (int n = 0; n < 48000; ++n)
        (void)v.tick(kAlgorithms[0]);
    CHECK_FALSE(v.isActive(kAlgorithms[0]));
}
