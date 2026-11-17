// Tests for fss/util/math.h (L0.2).
//
// Three kinds of test, a pattern the rest of CFS reuses:
//   1. Anchors: values you can check by hand or that are exact by definition (midiToHz(69) == 440).
//   2. Round trips: f⁻¹(f(x)) ≈ x across the whole useful range, via GENERATE.
//   3. Properties: things that must hold for *any* input (monotonic, an octave is exactly ×2).
//
// Float tolerances: these helpers run in float (24-bit mantissa, ≈7 significant digits), so "equal"
// means "within a relative error of ~1e-6". WithinRel compares relative error, which is right for
// frequencies and gains that span many orders of magnitude; WithinAbs is used where the value can be 0.
//
// Test *names* must be plain ASCII. catch_discover_tests makes CTest pass each name back to fss_tests as
// a command-line filter, and on Windows non-ASCII characters (→, −) get mangled by the ANSI code page on
// the way, so the test can't find itself and "fails". Unicode in comments and strings is fine.
#include <fss/util/math.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using namespace fss::util;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// ---- 1. anchors --------------------------------------------------------------------------------

TEST_CASE("midiToHz: A4 is exactly 440 Hz", "[math][pitch]")
{
    // exp2(0) == 1 exactly, so this is exact, not approximate.
    REQUIRE(midiToHz(69.0f) == 440.0f);
}

TEST_CASE("midiToHz: hand-computed notes", "[math][pitch]")
{
    CHECK_THAT(midiToHz(81.0f), WithinRel(880.0f, 1e-6f));       // A5, one octave up
    CHECK_THAT(midiToHz(57.0f), WithinRel(220.0f, 1e-6f));       // A3, one octave down
    CHECK_THAT(midiToHz(60.0f), WithinRel(261.625565f, 1e-6f));  // middle C = 440·2^(−9/12)
    CHECK_THAT(midiToHz(76.0f), WithinRel(659.255114f, 1e-6f));  // 69 + 7 semitones = E5
    CHECK_THAT(midiToHz(0.0f), WithinRel(8.175799f, 1e-5f));     // lowest MIDI note
    CHECK_THAT(midiToHz(127.0f), WithinRel(12543.854f, 1e-5f));  // highest MIDI note
    CHECK_THAT(midiToHz(69.5f), WithinRel(452.892984f, 1e-6f));  // fractional: a quarter tone above A4
}

TEST_CASE("hzToMidi: anchors", "[math][pitch]")
{
    CHECK_THAT(hzToMidi(440.0f), WithinAbs(69.0, 1e-6));
    CHECK_THAT(hzToMidi(880.0f), WithinAbs(81.0, 1e-5));
    CHECK_THAT(hzToMidi(261.625565f), WithinAbs(60.0, 1e-4));
}

TEST_CASE("dbToGain: anchors", "[math][loudness]")
{
    CHECK(dbToGain(0.0f) == 1.0f);
    CHECK_THAT(dbToGain(-6.0206f), WithinRel(0.5f, 1e-5f));  // the curriculum's anchor
    CHECK_THAT(dbToGain(6.0206f), WithinRel(2.0f, 1e-5f));
    CHECK_THAT(dbToGain(-12.0f), WithinRel(0.251188643f, 1e-6f));
    CHECK_THAT(dbToGain(-20.0f), WithinRel(0.1f, 1e-6f));
    CHECK_THAT(dbToGain(-60.0f), WithinRel(0.001f, 1e-5f));
}

TEST_CASE("gainToDb: anchors and the silence floor", "[math][loudness]")
{
    CHECK_THAT(gainToDb(1.0f), WithinAbs(0.0, 1e-6));
    CHECK_THAT(gainToDb(0.5f), WithinAbs(-6.0206, 1e-4));
    CHECK_THAT(gainToDb(0.001f), WithinAbs(-60.0, 1e-4));

    // Polarity doesn't change loudness.
    CHECK_THAT(gainToDb(-0.5f), WithinAbs(-6.0206, 1e-4));

    // Silence maps to the floor, never to −∞ or NaN.
    CHECK_THAT(gainToDb(0.0f), WithinAbs(kSilenceDb, 1e-3));
    CHECK_THAT(gainToDb(1e-30f), WithinAbs(kSilenceDb, 1e-3));
    CHECK(std::isfinite(gainToDb(0.0f)));
}

TEST_CASE("intervals: semitones and cents to ratio", "[math][pitch]")
{
    CHECK(centsToRatio(0.0f) == 1.0f);
    CHECK(centsToRatio(1200.0f) == 2.0f);    // exp2(1) is exact
    CHECK(centsToRatio(-1200.0f) == 0.5f);
    CHECK_THAT(centsToRatio(100.0f), WithinRel(1.05946309f, 1e-6f));  // one semitone = 2^(1/12)
    CHECK_THAT(semitonesToRatio(7.0f), WithinRel(1.49830708f, 1e-6f)); // an equal-tempered fifth
    CHECK(semitonesToRatio(12.0f) == 2.0f);
    CHECK_THAT(ratioToCents(1.5f), WithinAbs(701.955, 1e-3));          // a *just* fifth: 2 cents wider
}

// ---- 2. round trips ----------------------------------------------------------------------------

TEST_CASE("round trip: note -> Hz -> note over the MIDI range", "[math][pitch][roundtrip]")
{
    // Quarter-tone steps across all 128 notes. Tolerance 1e-4 semitones = 0.01 cents, far below
    // anything audible (the ear's pitch resolution is a few cents at best).
    const float note = GENERATE(range(0.0f, 127.25f, 0.25f));
    CHECK_THAT(hzToMidi(midiToHz(note)), WithinAbs(note, 1e-4));
}

TEST_CASE("round trip: dB -> gain -> dB from -120 to +24 dB", "[math][loudness][roundtrip]")
{
    const float db = GENERATE(range(-120.0f, 24.5f, 0.5f));
    CHECK_THAT(gainToDb(dbToGain(db)), WithinAbs(db, 1e-4));
}

TEST_CASE("round trip: cents -> ratio -> cents", "[math][pitch][roundtrip]")
{
    const float cents = GENERATE(range(-4800.0f, 4801.0f, 25.0f));
    CHECK_THAT(ratioToCents(centsToRatio(cents)), WithinAbs(cents, 1e-3));
}

// ---- 3. properties -----------------------------------------------------------------------------

TEST_CASE("property: every +12 notes doubles the frequency", "[math][pitch][property]")
{
    const float note = GENERATE(range(0.0f, 115.0f, 1.0f));
    CHECK_THAT(midiToHz(note + 12.0f) / midiToHz(note), WithinRel(2.0f, 1e-6f));
}

TEST_CASE("property: midiToHz is strictly increasing", "[math][pitch][property]")
{
    // A pitch knob that ever went *down* while you turned it up would be a bug, even by a hair.
    float previous = midiToHz(-24.0f);
    for (float note = -23.9f; note < 140.0f; note += 0.1f)
    {
        const float hz = midiToHz(note);
        REQUIRE(hz > previous);
        previous = hz;
    }
}

TEST_CASE("property: +6.0206 dB doubles the gain anywhere on the scale", "[math][loudness][property]")
{
    const float db = GENERATE(range(-100.0f, 20.0f, 5.0f));
    CHECK_THAT(dbToGain(db + 6.0206f) / dbToGain(db), WithinRel(2.0f, 1e-5f));
}
