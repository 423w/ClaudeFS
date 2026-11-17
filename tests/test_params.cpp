// Tests for fss/synth/params.h, fss/dsp/smoother.h and live parameter changes in the Engine (L2.4).
#include <fss/dsp/smoother.h>
#include <fss/engine.h>
#include <fss/synth/params.h>
#include <fss/synth/presets.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <set>
#include <string>
#include <vector>

using namespace fss;
using namespace fss::synth;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

TEST_CASE("param table: unique ids, unique names per group, sane ranges, defaults in range", "[params]")
{
    std::set<std::uint32_t> ids;
    std::set<std::string> names;
    for (const ParamInfo& p : kParams) {
        INFO("param " << p.id << " " << p.name);
        CHECK(ids.insert(p.id).second);
        CHECK(names.insert(std::to_string(p.op) + "/" + std::string(p.name)).second);
        CHECK(p.min < p.max);
        CHECK(p.def >= p.min);
        CHECK(p.def <= p.max);
        if (p.skew == Skew::Exp)
            CHECK(p.min > 0.0f);
        if (p.skew == Skew::Stepped)
            CHECK(p.smoothingMs >= 0.0f);
        for (char c : p.name)
            CHECK(static_cast<unsigned char>(c) < 128); // ASCII (CLAUDE.md: console/DAW text)
    }
    CHECK(kParams.size() == 4 + 4 * kNumOpFields);
}

TEST_CASE("param table: defaults are exactly what a default Patch holds", "[params]")
{
    const Patch fresh{};
    for (const ParamInfo& p : kParams) {
        INFO("param " << p.id << " " << p.name);
        CHECK_THAT(getParam(fresh, p), WithinAbs(p.def, 1e-4));
    }
}

TEST_CASE("param table: set then get returns the value, for every parameter", "[params]")
{
    std::mt19937 rng(5);
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    for (const ParamInfo& p : kParams) {
        for (float x : {0.0f, 0.37f, 1.0f, u(rng), u(rng)}) {
            Patch patch = presets::epiano();
            const float v = fromNormalized(p, x);
            setParam(patch, p, v);
            INFO("param " << p.id << " " << p.name << " = " << v);
            CHECK_THAT(getParam(patch, p), WithinAbs(v, std::max(1e-4, 1e-5 * std::abs(v))));
        }
    }
}

TEST_CASE("param table: normalized <-> plain round trips, and skews do what they say", "[params]")
{
    for (const ParamInfo& p : kParams) {
        for (float x : {0.0f, 0.25f, 0.5f, 1.0f}) {
            const float v = fromNormalized(p, x);
            INFO("param " << p.id << " " << p.name << " x=" << x);
            if (p.skew != Skew::Stepped)
                CHECK_THAT(toNormalized(p, v), WithinAbs(x, 1e-4));
        }
    }
    const ParamInfo& attack = *findParam(param_id::op(0, OpField::Attack)); // Exp 1..10000 ms
    CHECK_THAT(fromNormalized(attack, 0.5f), WithinRel(100.0f, 1e-4f));    // geometric middle
    const ParamInfo& level = *findParam(param_id::op(0, OpField::Level));   // Power2 0..8
    CHECK_THAT(fromNormalized(level, 0.5f), WithinAbs(2.0f, 1e-5));          // 8 · 0.5²
    CHECK(fromNormalized(*findParam(param_id::kAlgorithm), 0.5f) == 5.0f);   // 1 + 7·0.5 = 4.5 → rounds to 5
}

TEST_CASE("param table: setting the algorithm keeps per-operator feedback", "[params]")
{
    Patch p;
    setParam(p, *findParam(param_id::op(0, OpField::Feedback)), 1.25f);
    setParam(p, *findParam(param_id::kAlgorithm), 5.0f);
    CHECK(p.algorithmNumber == 5);
    CHECK(p.algorithm.mod[0][0] == 1.25f);
    CHECK(p.algorithm.mod[0][1] == 1.0f); // algorithm 5: A > B
    CHECK(p.algorithm.carrier[1] == 0.5f);
}

TEST_CASE("OnePoleSmoother: 63 percent after one time constant; 0 ms jumps", "[smoother]")
{
    dsp::OnePoleSmoother s;
    s.prepare(48000.0, 10.0f);
    s.reset(0.0f);
    s.setTarget(1.0f);
    float v = 0.0f;
    for (int i = 0; i < 480; ++i) // 10 ms
        v = s.tick();
    CHECK_THAT(v, WithinAbs(1.0 - std::exp(-1.0), 2e-3));
    dsp::OnePoleSmoother jump;
    jump.prepare(48000.0, 0.0f);
    jump.reset(0.0f);
    jump.setTarget(0.7f);
    CHECK(jump.tick() == 0.7f);
}

namespace {

std::vector<float> renderWithParams(int block, bool smoothing)
{
    // 2 s: chords plus live tweaks of every kind (levels, feedback, algorithm, ratio, envelope, output).
    struct Timed {
        int at;
        Event e;
    };
    std::vector<Timed> ev;
    std::mt19937 rng(11);
    std::uniform_int_distribution<int> when(0, 95999), key(40, 80);
    std::uniform_real_distribution<float> u(0.0f, 1.0f);
    for (int i = 0; i < 60; ++i) {
        const int t = when(rng);
        const auto k = static_cast<std::uint8_t>(key(rng));
        ev.push_back({t, Event::noteOn(0, k, 0.4f + 0.6f * u(rng))});
        ev.push_back({std::min(95999, t + 6000), Event::noteOff(0, k)});
    }
    for (int i = 0; i < 80; ++i) {
        const ParamInfo& p = kParams[static_cast<std::size_t>(std::uniform_int_distribution<int>(0, int(kParams.size()) - 1)(rng))];
        ev.push_back({when(rng), Event::paramChange(0, p.id, fromNormalized(p, u(rng)))});
    }
    std::stable_sort(ev.begin(), ev.end(), [](const Timed& a, const Timed& b) { return a.at < b.at; });

    Engine eng;
    eng.setSmoothingEnabled(smoothing);
    eng.prepare(48000.0, block);
    eng.setPatch(presets::epiano());
    std::vector<float> l(96000), r(96000);
    std::vector<Event> blockEv;
    std::size_t next = 0;
    for (int start = 0; start < 96000; start += block) {
        const int n = std::min(block, 96000 - start);
        blockEv.clear();
        while (next < ev.size() && ev[next].at < start + n) {
            Event e = ev[next].e;
            e.frame = static_cast<std::uint32_t>(ev[next].at - start);
            blockEv.push_back(e);
            ++next;
        }
        eng.process(blockEv, l.data() + start, r.data() + start, n);
    }
    return l;
}

} // namespace

TEST_CASE("Engine: block-size invariance holds with live parameter changes and smoothing", "[params][invariance]")
{
    const auto ref = renderWithParams(4096, true);
    for (int block : {1, 64, 500}) {
        INFO("block " << block);
        const auto got = renderWithParams(block, true);
        REQUIRE(std::memcmp(got.data(), ref.data(), ref.size() * sizeof(float)) == 0);
    }
}

TEST_CASE("Engine: smoothing turns a level jump into a glide", "[params]")
{
    // One sustained note, then the carrier level jumps 1 -> 0.1 mid-note. Smoothed: the output envelope
    // must change over ~5 time constants, never in one step.
    auto run = [](bool smoothing) {
        Engine eng;
        eng.setSmoothingEnabled(smoothing);
        eng.prepare(48000.0, 256);
        Patch p;
        p.algorithm = kAlgorithms[7];
        p.algorithm.carrier = {1.0f, 0.0f, 0.0f, 0.0f};
        p.ops[0].env = {.attackMs = 1.0f, .decayMs = 1.0f, .sustain = 1.0f, .releaseMs = 50.0f};
        eng.setPatch(p);
        std::vector<float> l(9600), r(9600);
        const Event on[] = {Event::noteOn(0, 69, 1.0f)};
        eng.process(on, l.data(), r.data(), 4800);
        // Frame 27: a quarter cycle of 440 Hz after sample 4800 (where 44 whole cycles end), i.e. at the
        // waveform's PEAK. At a zero crossing (frame 0) even a hard jump makes no step at all.
        const Event jump[] = {Event::paramChange(27, param_id::op(0, OpField::Level), 0.1f)};
        eng.process(jump, l.data() + 4800, r.data() + 4800, 4800);
        float maxStep = 0.0f;
        for (std::size_t i = 4790; i < 4900; ++i)
            maxStep = std::max(maxStep, std::abs(l[i] - l[i - 1]));
        return maxStep;
    };
    const float smooth = run(true), hard = run(false);
    CHECK(hard > 0.2f);    // a 1 -> 0.1 step at a random phase jumps by up to ~0.8 (× output gain 1, soft clip off)
    CHECK(smooth < 0.06f); // ≈ the steepest slope of the sine itself (2π·440/48000 ≈ 0.058)
}
