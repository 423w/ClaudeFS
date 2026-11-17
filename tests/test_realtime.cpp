// Tests for the real-time guard and allocation trap (L2.2), and the audit they enable: the Engine must
// never allocate or free inside process(), under heavy playing (stealing, bends, retriggers, panics).
#include <fss/engine.h>
#include <fss/synth/params.h>
#include <fss/synth/presets.h>
#include <fss/util/realtime_guard.h>

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <random>
#include <string>
#include <vector>

using namespace fss;

TEST_CASE("RealtimeGuard: scopes nest and unwind", "[realtime]")
{
    CHECK_FALSE(util::inRealtimeScope());
    {
        const util::RealtimeGuard a;
        CHECK(util::inRealtimeScope());
        {
            const util::RealtimeGuard b;
            const util::RealtimeExemption allowed;
            CHECK_FALSE(util::inRealtimeScope());
        }
        CHECK(util::inRealtimeScope());
    }
    CHECK_FALSE(util::inRealtimeScope());
}

TEST_CASE("allocation trap: catches new, vector growth, string and delete inside a guard", "[realtime]")
{
    REQUIRE(util::realtimeTrapInstalled()); // the test binary links fss_rt_trap
    util::setRealtimeTrapMode(util::RealtimeTrapMode::Count);
    util::resetRealtimeViolations();

    auto outside = std::make_unique<int>(1); // allocations outside a guard are fine
    std::vector<int> v;
    CHECK(util::realtimeViolations() == 0);
    {
        const util::RealtimeGuard rt;
        v.push_back(1);                                          // vector growth: allocates
        std::string s = "a string long enough to defeat the small-string buffer"; // allocates, then frees
        outside.reset();                                         // delete: a free on the audio thread
        (void)s;
    }
    CHECK(util::realtimeViolations() >= 4);
    util::resetRealtimeViolations();
}

TEST_CASE("Engine::process never allocates or frees, under heavy random playing", "[realtime][engine]")
{
    REQUIRE(util::realtimeTrapInstalled());
    util::setRealtimeTrapMode(util::RealtimeTrapMode::Count);

    Engine eng;
    eng.prepare(48000.0, 512); // prepare may allocate: it's outside any guard
    eng.setPatch(synth::presets::epiano());
    eng.setPolyphony(6);       // force constant stealing
    std::vector<float> l(512), r(512);
    std::vector<Event> events;
    events.reserve(64);        // the *test* must not allocate inside the loop either (it's not guarded, but tidy)
    std::mt19937 rng(99);
    std::uniform_int_distribution<int> key(30, 100), kind(0, 11), frame(0, 511);
    std::uniform_real_distribution<float> val(-1.0f, 1.0f);

    util::resetRealtimeViolations();
    for (int block = 0; block < 2000; ++block) { // ~21 s of audio
        events.clear();
        for (int i = 0; i < 6; ++i) {
            const auto k = static_cast<std::uint8_t>(key(rng));
            const auto f = static_cast<std::uint32_t>(frame(rng));
            switch (kind(rng)) {
            case 0: case 1: case 2: case 3: events.push_back(Event::noteOn(f, k, 0.5f + 0.5f * val(rng))); break;
            case 4: case 5: case 6: events.push_back(Event::noteOff(f, k)); break;
            case 7: events.push_back(Event::pitchBend(f, val(rng))); break;
            case 8: events.push_back(Event::noteOn(f, k, 0.0f)); break; // velocity 0 = note-off
            case 9: events.push_back({Event::Type::AllNotesOff, f}); break;
            default: { // L2.4: live parameter changes of every kind (levels, algorithm, envelopes, ratios...)
                const auto& p = synth::kParams[static_cast<std::size_t>(k) % synth::kParams.size()];
                events.push_back(Event::paramChange(f, p.id, synth::fromNormalized(p, 0.5f + 0.5f * val(rng))));
                break;
            }
            }
        }
        std::sort(events.begin(), events.end(), [](const Event& a, const Event& b) { return a.frame < b.frame; });
        eng.process(events, l.data(), r.data(), 512);
    }
    CHECK(util::realtimeViolations() == 0);
    CHECK(eng.steals() > 100); // the stealing paths really ran
}
