// Tests for fss/util/units.h and the note → operator mapping in fm_voice.h (L1.6).
#include <fss/synth/fm_voice.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <type_traits>

using namespace fss::synth;
using namespace fss::util;
using namespace fss::util::literals;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// ---- compile-time: the unit mix-ups that must NOT compile --------------------------------------
// (The L1.6 journal shows the actual compiler error for one of these written as real code.)
static_assert(!std::is_convertible_v<double, Hz>, "a bare number must not silently become Hz");
static_assert(!std::is_convertible_v<Hz, Cents>, "Hz is not Cents");
static_assert(!std::is_constructible_v<Cents, Hz>, "...not even explicitly");
static_assert(!std::is_constructible_v<Ratio, Semitones>, "semitones need toRatio(), not a cast");
static_assert(!std::is_assignable_v<Cents&, Hz>, "p.detune = Hz{440} must be an error");
static_assert(std::is_same_v<decltype(Hz{} * Ratio{}), Hz>, "Hz x Ratio = Hz");
static_assert((440.0_Hz * Ratio{2.0}).value == 880.0);
static_assert(sizeof(Hz) == sizeof(double), "zero overhead");

TEST_CASE("units: conversions to ratio", "[units]")
{
    CHECK_THAT(toRatio(1200.0_cents).value, WithinRel(2.0, 1e-12));
    CHECK_THAT(toRatio(7.0_st).value, WithinRel(1.4983070768766815, 1e-12));
    CHECK_THAT(toRatio(-12.0_st).value, WithinRel(0.5, 1e-12));
}

TEST_CASE("note: frequency from key, tuning, bend, ratio, detune and fixed mode", "[note]")
{
    Patch p;
    OperatorParams op;
    CHECK_THAT(operatorFrequencyHz(p, op, 69, 0.0f), WithinRel(440.0, 1e-12));
    op.ratio = Ratio{2.0};
    CHECK_THAT(operatorFrequencyHz(p, op, 69, 0.0f), WithinRel(880.0, 1e-12));
    op.detune = 1200.0_cents;
    CHECK_THAT(operatorFrequencyHz(p, op, 69, 0.0f), WithinRel(1760.0, 1e-12));
    op = {};
    CHECK_THAT(operatorFrequencyHz(p, op, 69, 1.0f), WithinRel(440.0 * std::exp2(2.0 / 12.0), 1e-12)); // +2 st
    p.tuningA4 = 432.0_Hz;
    CHECK_THAT(operatorFrequencyHz(p, op, 81, 0.0f), WithinRel(864.0, 1e-12));
    op.fixed = true;
    op.fixedHz = 1234.5_Hz;
    CHECK(operatorFrequencyHz(p, op, 30, 1.0f) == 1234.5); // fixed mode ignores key and bend
}

TEST_CASE("note: velocity sensitivity", "[note][velocity]")
{
    OperatorParams op;
    op.level = 2.0f;
    CHECK(operatorLevel(op, 60, 0.1f) == 2.0f); // velSens 0: velocity ignored
    op.velSens = 1.0f;
    CHECK_THAT(operatorLevel(op, 60, 1.0f), WithinRel(2.0, 1e-6));                       // full velocity: 0 dB
    CHECK_THAT(operatorLevel(op, 60, 0.0f), WithinRel(2.0 * dbToGain(-36.0f), 1e-5));     // −36 dB
    op.velSens = 0.5f;
    CHECK_THAT(operatorLevel(op, 60, 0.0f), WithinRel(2.0 * dbToGain(-18.0f), 1e-5));
}

TEST_CASE("note: key level scaling attenuates only above the breakpoint", "[note][keyscale]")
{
    OperatorParams op;
    op.keyLevelDbPerOct = 6.0f;
    CHECK(operatorLevel(op, 60, 1.0f) == 1.0f);
    CHECK(operatorLevel(op, 36, 1.0f) == 1.0f);                                 // below: untouched
    CHECK_THAT(operatorLevel(op, 72, 1.0f), WithinRel(static_cast<double>(dbToGain(-6.0f)), 1e-5));  // one octave up: −6 dB
    CHECK_THAT(operatorLevel(op, 84, 1.0f), WithinRel(static_cast<double>(dbToGain(-12.0f)), 1e-5));
}

TEST_CASE("note: key rate scaling shortens envelopes up high, lengthens down low", "[note][keyscale]")
{
    OperatorParams op;
    op.env = {.attackMs = 10.0f, .decayMs = 400.0f, .sustain = 0.5f, .releaseMs = 200.0f};
    op.keyRateScale = 1.0f;
    const auto up = scaledEnvelope(op, 72), down = scaledEnvelope(op, 48), mid = scaledEnvelope(op, 60);
    CHECK_THAT(up.decayMs, WithinRel(200.0, 1e-6));
    CHECK_THAT(down.decayMs, WithinRel(800.0, 1e-6));
    CHECK(mid.decayMs == 400.0f);
    CHECK(up.sustain == 0.5f); // a level, not a time: untouched
}

TEST_CASE("FmVoice::start applies the patch, and setBend retunes without restarting", "[note][fmvoice]")
{
    Patch p;
    p.ops[0].ratio = Ratio{3.0};
    p.ops[1].fixed = true;
    p.ops[1].fixedHz = 100.0_Hz;
    FmVoice v;
    v.prepare(48000.0);
    v.start({.key = 69, .velocity = 1.0f, .bend = 0.0f}, p);
    CHECK_THAT(v.op(0).frequencyHz(), WithinAbs(1320.0, 1e-3));
    CHECK_THAT(v.op(1).frequencyHz(), WithinAbs(100.0, 1e-3));
    CHECK_THAT(v.op(2).frequencyHz(), WithinAbs(440.0, 1e-3));
    v.setBend(-1.0f); // −2 semitones
    CHECK_THAT(v.op(2).frequencyHz(), WithinAbs(440.0 * std::exp2(-2.0 / 12.0), 1e-3));
    CHECK_THAT(v.op(1).frequencyHz(), WithinAbs(100.0, 1e-3)); // fixed ops don't bend
    CHECK(v.key() == 69);
}
