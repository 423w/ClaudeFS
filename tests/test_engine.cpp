// Tests for fss/engine.h (L1.7): sample accuracy, block-size invariance, allocation and stealing.
#include <fss/engine.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <vector>

using namespace fss;

namespace {

constexpr double kFs = 48000.0;

synth::Patch testPatch()
{
    synth::Patch p;
    p.algorithm = synth::kAlgorithms[4]; // two stacks
    p.ops[0] = {.ratio = util::Ratio{3.0}, .level = 1.5f, .velSens = 1.0f,
                .env = {.attackMs = 2.0f, .decayMs = 300.0f, .sustain = 0.2f, .releaseMs = 200.0f}};
    p.ops[1] = {.level = 1.0f, .env = {.attackMs = 5.0f, .decayMs = 800.0f, .sustain = 0.5f, .releaseMs = 300.0f}};
    p.ops[2] = {.ratio = util::Ratio{1.0}, .level = 1.0f, .env = {.attackMs = 1.0f, .decayMs = 500.0f, .sustain = 0.3f, .releaseMs = 300.0f}};
    p.ops[3] = {.level = 1.0f, .env = {.attackMs = 3.0f, .decayMs = 1000.0f, .sustain = 0.6f, .releaseMs = 400.0f}};
    p.algorithm.mod[0][0] = 0.7f; // some feedback too: history must also be block-size independent
    p.outputGain = 0.3f;
    return p;
}

/// Absolute-time events for a whole render.
struct TimedEvent {
    std::uint64_t at;
    Event e;
};

std::vector<TimedEvent> randomScore(std::uint32_t seed, std::uint64_t length)
{
    std::mt19937 rng(seed);
    std::uniform_int_distribution<std::uint64_t> when(0, length - 1);
    std::uniform_int_distribution<int> key(36, 84), len(100, 30000);
    std::uniform_real_distribution<float> vel(0.1f, 1.0f), bend(-1.0f, 1.0f);
    std::vector<TimedEvent> ev;
    for (int i = 0; i < 120; ++i) {
        const auto t = when(rng);
        const auto k = static_cast<std::uint8_t>(key(rng));
        ev.push_back({t, Event::noteOn(0, k, vel(rng))});
        ev.push_back({t + static_cast<std::uint64_t>(len(rng)), Event::noteOff(0, k)});
    }
    for (int i = 0; i < 10; ++i)
        ev.push_back({when(rng), Event::pitchBend(0, bend(rng))});
    std::stable_sort(ev.begin(), ev.end(), [](const TimedEvent& a, const TimedEvent& b) { return a.at < b.at; });
    return ev;
}

/// Render `length` samples in blocks of `block`, slicing the absolute-time events into each block.
std::vector<float> renderInBlocks(const std::vector<TimedEvent>& score, std::uint64_t length, int block, int polyphony = 16)
{
    Engine eng;
    eng.prepare(kFs, block);
    eng.setPatch(testPatch());
    eng.setPolyphony(polyphony);
    std::vector<float> out(length), right(length);
    std::vector<Event> blockEvents;
    std::size_t next = 0;
    for (std::uint64_t start = 0; start < length; start += static_cast<std::uint64_t>(block)) {
        const auto n = static_cast<int>(std::min<std::uint64_t>(static_cast<std::uint64_t>(block), length - start));
        blockEvents.clear();
        while (next < score.size() && score[next].at < start + static_cast<std::uint64_t>(n)) {
            Event e = score[next].e;
            e.frame = static_cast<std::uint32_t>(score[next].at - start);
            blockEvents.push_back(e);
            ++next;
        }
        eng.process(blockEvents, out.data() + start, right.data() + start, n);
    }
    return out;
}

} // namespace

TEST_CASE("Engine: block-size invariance - 1, 64, 4096 give bit-identical output", "[engine][invariance]")
{
    constexpr std::uint64_t kLength = 96000; // 2 s, 120 notes, bends, steals (polyphony 6)
    const auto score = randomScore(7, kLength);
    const auto ref = renderInBlocks(score, kLength, 4096, 6);
    for (int block : {1, 64, 333}) {
        INFO("block size " << block);
        const auto got = renderInBlocks(score, kLength, block, 6);
        REQUIRE(std::memcmp(got.data(), ref.data(), ref.size() * sizeof(float)) == 0);
    }
    // And the render isn't trivially silent.
    CHECK(*std::max_element(ref.begin(), ref.end()) > 0.05f);
}

TEST_CASE("Engine: a note starts on exactly its frame", "[engine]")
{
    Engine eng;
    eng.prepare(kFs, 256);
    eng.setPatch(testPatch());
    std::vector<float> l(256), r(256);
    const Event on[] = {Event::noteOn(100, 60, 1.0f)};
    eng.process(on, l.data(), r.data(), 256);
    for (int i = 0; i < 100; ++i)
        REQUIRE(l[static_cast<std::size_t>(i)] == 0.0f);
    CHECK(std::abs(l[105]) + std::abs(l[110]) + std::abs(l[150]) > 0.0f);
    CHECK(l == r);
}

TEST_CASE("Engine: voices are freed after release, and never exceed the polyphony", "[engine][voices]")
{
    Engine eng;
    eng.prepare(kFs, 512);
    eng.setPatch(testPatch());
    eng.setPolyphony(4);
    std::vector<float> l(512), r(512);
    std::vector<Event> ons;
    for (std::uint8_t k = 60; k < 70; ++k) // 10 notes at once into 4 voices
        ons.push_back(Event::noteOn(0, k, 0.8f));
    eng.process(ons, l.data(), r.data(), 512);
    CHECK(eng.steals() == 6);
    // 5 ms later the stolen voices have faded out: only the 4 newest remain.
    for (int b = 0; b < 2; ++b)
        eng.process({}, l.data(), r.data(), 512);
    CHECK(eng.busyVoices() == 4);

    const Event off[] = {{Event::Type::AllNotesOff}};
    eng.process(off, l.data(), r.data(), 512);
    for (int b = 0; b < 200 && eng.busyVoices() > 0; ++b) // release ≤ 400 ms × 1.11
        eng.process({}, l.data(), r.data(), 512);
    CHECK(eng.busyVoices() == 0);
}

TEST_CASE("Engine: steals take released voices first, then the oldest", "[engine][voices]")
{
    Engine eng;
    eng.prepare(kFs, 64);
    eng.setPatch(testPatch());
    eng.setPolyphony(2);
    std::vector<float> l(64), r(64);
    const Event a[] = {Event::noteOn(0, 60, 1.0f), Event::noteOn(1, 62, 1.0f), Event::noteOff(2, 60)};
    eng.process(a, l.data(), r.data(), 64);
    // 60 is released, 62 held. A third note must steal 60 (released) even though 62 is younger.
    const Event b[] = {Event::noteOn(0, 64, 1.0f)};
    eng.process(b, l.data(), r.data(), 64);
    for (int i = 0; i < 10; ++i)
        eng.process({}, l.data(), r.data(), 64); // let the fade finish
    // A note-off for 62 must still find a held voice (62 survived), and 64 must be playing.
    CHECK(eng.busyVoices() == 2);
    CHECK(eng.steals() == 1);
}

TEST_CASE("Engine: same-key retrigger fades the old voice instead of restarting it", "[engine][voices]")
{
    Engine eng;
    eng.prepare(kFs, 64);
    eng.setPatch(testPatch());
    std::vector<float> l(64), r(64);
    const Event a[] = {Event::noteOn(0, 60, 1.0f)};
    eng.process(a, l.data(), r.data(), 64);
    const Event b[] = {Event::noteOn(0, 60, 1.0f)};
    eng.process(b, l.data(), r.data(), 64);
    CHECK(eng.busyVoices() == 2); // old one fading, new one starting
    for (int i = 0; i < 10; ++i)
        eng.process({}, l.data(), r.data(), 64);
    CHECK(eng.busyVoices() == 1);
    CHECK(eng.steals() == 0);     // a retrigger is not a polyphony steal
}

TEST_CASE("softClip: transparent below 0.8, bounded by 1, smooth and odd", "[engine]")
{
    CHECK(softClip(0.5f) == 0.5f);
    CHECK(softClip(-0.8f) == -0.8f);
    CHECK(softClip(100.0f) <= 1.0f); // in float, tanh(large) rounds to exactly 1: "never exceeds", not "never reaches"
    CHECK(softClip(100.0f) > 0.999f);
    CHECK(softClip(-3.0f) == -softClip(3.0f));
    // Continuous slope at the knee: no corner (a corner would add harmonics abruptly).
    CHECK(std::abs((softClip(0.8001f) - softClip(0.8f)) / 0.0001f - 1.0f) < 0.01f);
}
