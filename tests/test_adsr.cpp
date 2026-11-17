// Tests for fss/dsp/adsr.h (L1.3).
#include <fss/dsp/adsr.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstdint>
#include <random>

using namespace fss::dsp;
using Catch::Matchers::WithinAbs;
using Stage = Adsr::Stage;

namespace {

Adsr make(double fs, AdsrParams p)
{
    Adsr e;
    e.prepare(fs);
    e.setParams(p);
    return e;
}

/// Tick until `done(env, level)` is true; return the number of ticks (or −1 after `limit`).
template <typename Done>
long ticksUntil(Adsr& e, Done done, long limit = 10'000'000)
{
    for (long n = 1; n <= limit; ++n) {
        const float v = e.tick();
        if (done(e, v))
            return n;
    }
    return -1;
}

} // namespace

TEST_CASE("Adsr: idle until noteOn, and silent", "[adsr]")
{
    Adsr e = make(48000.0, {});
    CHECK_FALSE(e.isActive());
    for (int i = 0; i < 100; ++i)
        REQUIRE(e.tick() == 0.0f);
    e.noteOff(); // ignored when idle
    CHECK(e.stage() == Stage::Idle);
}

TEST_CASE("Adsr: segment times are right at 44.1, 48 and 96 kHz", "[adsr][timing]")
{
    const double fs = GENERATE(44100.0, 48000.0, 96000.0);
    const AdsrParams p{.attackMs = 10.0f, .decayMs = 200.0f, .sustain = 0.0f, .releaseMs = 500.0f};
    INFO("fs = " << fs);

    // Attack: 0 → 1 in attackMs (±1 sample).
    Adsr a = make(fs, p);
    a.noteOn();
    const long attack = ticksUntil(a, [](const Adsr& e, float) { return e.stage() != Stage::Attack; });
    CHECK(std::abs(attack - std::lround(0.010 * fs)) <= 1);

    // Release rate: from full scale, −60 dB (0.001) after releaseMs. Get to 1.0 with sustain = 1 first.
    Adsr r = make(fs, {.attackMs = 1.0f, .decayMs = 1.0f, .sustain = 1.0f, .releaseMs = 500.0f});
    r.noteOn();
    (void)ticksUntil(r, [](const Adsr& e, float) { return e.stage() == Stage::Sustain; });
    r.noteOff();
    const long t60 = ticksUntil(r, [](const Adsr&, float v) { return v <= 0.001f; });
    CHECK(std::abs(t60 - std::lround(0.500 * fs)) <= 1);
    // ...and it really ends (≈ 11 % later), going idle so the voice can be freed.
    const long toZero = t60 + ticksUntil(r, [](const Adsr& e, float) { return !e.isActive(); });
    CHECK(std::abs(static_cast<double>(toZero) / (0.5 * fs) - 1.1115) < 0.002);

    // Decay uses the same rate definition: with sustain 0 it is a release.
    Adsr d = make(fs, p);
    d.noteOn();
    (void)ticksUntil(d, [](const Adsr& e, float) { return e.stage() == Stage::Decay; });
    const long d60 = ticksUntil(d, [](const Adsr&, float v) { return v <= 0.001f; });
    CHECK(std::abs(d60 - std::lround(0.200 * fs)) <= 1);
}

TEST_CASE("Adsr: decay settles exactly on sustain and holds", "[adsr]")
{
    Adsr e = make(48000.0, {.attackMs = 2.0f, .decayMs = 50.0f, .sustain = 0.4f, .releaseMs = 50.0f});
    e.noteOn();
    REQUIRE(ticksUntil(e, [](const Adsr& x, float) { return x.stage() == Stage::Sustain; }) > 0);
    for (int i = 0; i < 48000; ++i)
        REQUIRE(e.tick() == 0.4f);
}

TEST_CASE("Adsr: note-off during attack releases smoothly from the current level", "[adsr][edge]")
{
    Adsr e = make(48000.0, {.attackMs = 100.0f, .decayMs = 100.0f, .sustain = 0.8f, .releaseMs = 100.0f});
    e.noteOn();
    float level = 0.0f;
    for (int i = 0; i < 1000; ++i) // ~20 ms into a 100 ms attack
        level = e.tick();
    REQUIRE(level > 0.1f);
    REQUIRE(level < 0.9f);
    e.noteOff();
    CHECK(e.stage() == Stage::Release);
    float prev = level;
    while (e.isActive()) {
        const float v = e.tick();
        REQUIRE(v <= prev);                  // only goes down, no jump up to sustain or anywhere else
        REQUIRE(prev - v < 0.01f);           // and never in a big step
        prev = v;
    }
    CHECK(prev == 0.0f);
}

TEST_CASE("Adsr: retrigger during release restarts the attack from the current level", "[adsr][edge]")
{
    Adsr e = make(48000.0, {.attackMs = 50.0f, .decayMs = 50.0f, .sustain = 0.5f, .releaseMs = 300.0f});
    e.noteOn();
    (void)ticksUntil(e, [](const Adsr& x, float) { return x.stage() == Stage::Sustain; });
    e.noteOff();
    float level = 0.0f;
    for (int i = 0; i < 2000; ++i)
        level = e.tick();
    REQUIRE(level > 0.05f);

    e.noteOn(); // retrigger mid-release
    CHECK(e.stage() == Stage::Attack);
    const float next = e.tick();
    CHECK(next >= level);                     // continues upward from where it was...
    CHECK(next - level < 0.01f);              // ...without jumping (no reset to zero, no click)
    const long rest = ticksUntil(e, [](const Adsr& x, float) { return x.stage() != Stage::Attack; });
    CHECK(rest < std::lround(0.050 * 48000)); // starting higher, the attack arrives sooner
}

TEST_CASE("Adsr: zero-time segments are clamped to 1 ms and stay finite", "[adsr][edge]")
{
    Adsr e = make(48000.0, {.attackMs = 0.0f, .decayMs = 0.0f, .sustain = 0.5f, .releaseMs = 0.0f});
    e.noteOn();
    const long attack = ticksUntil(e, [](const Adsr& x, float) { return x.stage() != Stage::Attack; });
    CHECK(std::abs(attack - 48) <= 1); // 1 ms at 48 kHz
    e.noteOff();
    const long release = ticksUntil(e, [](const Adsr& x, float) { return !x.isActive(); });
    CHECK(release > 0);
    CHECK(release < 60);

    // Negative and NaN times must not produce NaN levels either.
    Adsr bad = make(48000.0, {.attackMs = -5.0f, .decayMs = std::nanf(""), .sustain = 2.0f, .releaseMs = -1.0f});
    bad.noteOn();
    for (int i = 0; i < 1000; ++i) {
        const float v = bad.tick();
        REQUIRE(std::isfinite(v));
        REQUIRE(v >= 0.0f);
        REQUIRE(v <= 1.0f);
    }
}

TEST_CASE("Adsr: changing sustain while sustaining glides instead of jumping", "[adsr]")
{
    AdsrParams p{.attackMs = 1.0f, .decayMs = 1.0f, .sustain = 0.2f, .releaseMs = 100.0f};
    Adsr e = make(48000.0, p);
    e.noteOn();
    (void)ticksUntil(e, [](const Adsr& x, float) { return x.stage() == Stage::Sustain; });
    p.sustain = 0.9f;
    e.setParams(p);
    float prev = e.level();
    for (int i = 0; i < 4800; ++i) {
        const float v = e.tick();
        REQUIRE(std::abs(v - prev) < 0.01f);
        prev = v;
    }
    CHECK_THAT(prev, WithinAbs(0.9, 1e-3)); // 100 ms = 20 time constants later: arrived
}

TEST_CASE("Adsr: fuzz - random params and gates keep the output in [0, 1]", "[adsr][property]")
{
    std::mt19937 rng(1234); // fixed seed: failures are reproducible
    std::uniform_real_distribution<float> ms(0.0f, 300.0f), lvl(0.0f, 1.0f), coin(0.0f, 1.0f);
    Adsr e = make(48000.0, {});
    for (int block = 0; block < 2000; ++block) {
        if (coin(rng) < 0.3f)
            e.setParams({ms(rng), ms(rng), lvl(rng), ms(rng)});
        if (coin(rng) < 0.5f)
            e.noteOn();
        else
            e.noteOff();
        for (int i = 0; i < 64; ++i) {
            const float v = e.tick();
            REQUIRE(v >= 0.0f);
            REQUIRE(v <= 1.0f);
        }
    }
}
