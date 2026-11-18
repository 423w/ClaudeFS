// Scenario tests for playing the Engine like an instrument (L2.5): given these events, these voices should be
// in these states. Held-note stack, sustain pedal, mono / legato, note priority, glide, pedal-aware stealing,
// and a long random "no stuck notes" run.
#include <fss/engine.h>
#include <fss/synth/held_notes.h>
#include <fss/synth/params.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

using namespace fss;
using Catch::Matchers::WithinRel;
using synth::NotePriority;
using synth::VoiceMode;

namespace {

constexpr double kFs = 48000.0;

/// A pure sine voice (op A only) that sustains at full level and releases in 50 ms.
synth::Patch sinePatch(VoiceMode mode = VoiceMode::Poly)
{
    synth::Patch p;
    p.algorithm = synth::kAlgorithms[7];
    p.algorithmNumber = 8;
    p.algorithm.carrier = {1.0f, 0.0f, 0.0f, 0.0f};
    p.ops[0] = {.level = 0.5f, .env = {.attackMs = 1.0f, .decayMs = 1.0f, .sustain = 1.0f, .releaseMs = 50.0f}};
    p.voice.mode = mode;
    return p;
}

/// A tiny test harness: an Engine, and a way to send events "now" and let time pass.
struct Player {
    Engine eng;
    std::vector<float> l, r, out;
    explicit Player(const synth::Patch& p, int polyphony = Engine::kMaxPolyphony)
    {
        eng.prepare(kFs, 4800);
        eng.setPatch(p);
        eng.setPolyphony(polyphony);
        l.resize(4800);
        r.resize(4800);
    }
    void send(std::initializer_list<Event> events, int frames = 480) // default: 10 ms after the events
    {
        std::vector<Event> ev(events);
        eng.process(ev, l.data(), r.data(), std::min(frames, 4800));
        out.insert(out.end(), l.begin(), l.begin() + std::min(frames, 4800));
        for (int left = frames - 4800; left > 0; left -= 4800) {
            eng.process({}, l.data(), r.data(), std::min(left, 4800));
            out.insert(out.end(), l.begin(), l.begin() + std::min(left, 4800));
        }
    }
    void on(std::uint8_t k, int frames = 480) { send({Event::noteOn(0, k, 0.8f)}, frames); }
    void off(std::uint8_t k, int frames = 480) { send({Event::noteOff(0, k)}, frames); }
    void pedal(bool down, int frames = 480) { send({Event::controller(0, 64, down ? 1.0f : 0.0f)}, frames); }

    /// Voices that are sounding and not being faded out by a steal.
    [[nodiscard]] std::vector<const synth::Voice*> live() const
    {
        std::vector<const synth::Voice*> v;
        for (const synth::Voice& x : eng.voices())
            if (x.busy() && !x.fading())
                v.push_back(&x);
        return v;
    }
    [[nodiscard]] int fading() const
    {
        return static_cast<int>(std::count_if(eng.voices().begin(), eng.voices().end(),
                                              [](const synth::Voice& x) { return x.busy() && x.fading(); }));
    }
};

} // namespace

TEST_CASE("HeldNotes: press order, re-press moves to top, priorities", "[play]")
{
    synth::HeldNotes h;
    h.press(60, 1.0f);
    h.press(64, 1.0f);
    h.press(55, 1.0f);
    CHECK(h.pick(NotePriority::Last).key == 55);
    CHECK(h.pick(NotePriority::Low).key == 55);
    CHECK(h.pick(NotePriority::High).key == 64);
    h.press(60, 0.5f); // pressed again: now the latest
    CHECK(h.pick(NotePriority::Last).key == 60);
    CHECK(h.size() == 3);
    CHECK(h.release(55));
    CHECK_FALSE(h.release(55));
    CHECK(h.pick(NotePriority::Low).key == 60);
    for (int k = 0; k < 40; ++k) // overflow forgets the oldest, never grows
        h.press(static_cast<std::uint8_t>(k), 1.0f);
    CHECK(h.size() == synth::HeldNotes::kCapacity);
    CHECK(h.pick(NotePriority::Last).key == 39);
}

TEST_CASE("Sustain pedal (poly): released keys keep sounding until the pedal comes up", "[play][pedal]")
{
    Player p(sinePatch());
    p.pedal(true);
    p.on(60);
    p.on(64);
    p.off(60);
    p.off(64);
    REQUIRE(p.live().size() == 2);
    for (const synth::Voice* v : p.live()) {
        CHECK_FALSE(v->released());
        CHECK(v->sustained());
    }
    p.on(67); // played while the pedal is down: an ordinary held note
    p.pedal(false);
    int released = 0, held = 0;
    for (const synth::Voice* v : p.live())
        (v->released() ? released : held) += 1;
    CHECK(released == 2); // 60 and 64 go into release at pedal-up
    CHECK(held == 1);     // 67 is still held by its key
    p.off(67, 9600);      // 200 ms: every release (50 ms) has finished
    CHECK(p.eng.busyVoices() == 0);
}

TEST_CASE("Sustain pedal: re-striking a sustained key fades the old voice and starts a new one", "[play][pedal]")
{
    Player p(sinePatch());
    p.pedal(true);
    p.on(60);
    p.off(60);
    p.on(60);
    REQUIRE(p.live().size() == 1);
    CHECK_FALSE(p.live()[0]->sustained()); // the new strike is held by its key, not the pedal
    p.pedal(false);
    CHECK_FALSE(p.live()[0]->released());
}

TEST_CASE("Legato: one voice, no retrigger; falls back to the held note; last key up releases", "[play][mono]")
{
    Player p(sinePatch(VoiceMode::Legato));
    p.on(60);
    REQUIRE(p.live().size() == 1);
    const synth::Voice* first = p.live()[0];
    const auto age = first->age();
    p.on(64); // overlapping: legato
    REQUIRE(p.live().size() == 1);
    CHECK(p.live()[0] == first);
    CHECK(first->age() == age); // the same note, never restarted
    CHECK(first->key() == 64);
    p.off(64); // back to the still-held 60, still the same note
    CHECK(first->key() == 60);
    CHECK(p.live()[0] == first);
    CHECK_FALSE(first->released());
    p.off(60);
    CHECK(first->released());
    p.on(67); // detached (nothing held): a new note
    REQUIRE(p.live().size() == 1);
    CHECK(p.live()[0]->age() > age);
}

TEST_CASE("Mono (retrigger): every new key restarts the note, the old one fades out", "[play][mono]")
{
    Player p(sinePatch(VoiceMode::Mono));
    p.on(60);
    const auto age = p.live()[0]->age();
    p.send({Event::noteOn(0, 64, 0.8f)}, 1); // look right after the event, inside the 5 ms fade
    REQUIRE(p.live().size() == 1);
    CHECK(p.live()[0]->key() == 64);
    CHECK(p.live()[0]->age() > age);
    CHECK(p.fading() == 1);
}

TEST_CASE("Note priority: low and high keep the right note when others are pressed", "[play][mono]")
{
    auto patch = sinePatch(VoiceMode::Legato);
    patch.voice.priority = NotePriority::Low;
    Player low(patch);
    low.on(60);
    low.on(64); // higher: loses to 60 under low priority
    CHECK(low.live()[0]->key() == 60);
    low.on(55); // lower: wins
    CHECK(low.live()[0]->key() == 55);
    low.off(55);
    CHECK(low.live()[0]->key() == 60);

    patch.voice.priority = NotePriority::High;
    Player high(patch);
    high.on(60);
    high.on(55);
    CHECK(high.live()[0]->key() == 60);
    high.off(60);
    CHECK(high.live()[0]->key() == 55);
}

namespace {

/// Frequency of a sine from the upward zero crossings in out[a, b).
double measuredHz(const std::vector<float>& x, std::size_t a, std::size_t b)
{
    double first = -1, last = -1;
    int n = 0;
    for (std::size_t i = a + 1; i < b; ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            const double t = static_cast<double>(i - 1) + x[i - 1] / (x[i - 1] - x[i]);
            if (first < 0)
                first = t;
            else
                ++n;
            last = t;
        }
    return n > 0 ? kFs * n / (last - first) : 0.0;
}

} // namespace

TEST_CASE("Glide: constant time, linear in semitones, lands exactly on the note", "[play][glide]")
{
    auto patch = sinePatch(VoiceMode::Legato);
    patch.voice.glideMs = 100.0f;
    Player p(patch);
    p.on(60, 4800);           // 100 ms of C4
    const std::size_t t0 = p.out.size();
    p.on(72, 9600);           // legato to C5, gliding over 100 ms; record 200 ms
    const double c4 = 440.0 * std::exp2((60 - 69) / 12.0);
    // Around 50 ms (a 10 ms window, centred): halfway in semitones = F#4 = C4 x sqrt(2).
    CHECK_THAT(measuredHz(p.out, t0 + 2400 - 240, t0 + 2400 + 240), WithinRel(c4 * std::sqrt(2.0), 0.02));
    // After 100 ms: exactly C5.
    CHECK_THAT(measuredHz(p.out, t0 + 5000, t0 + 9600), WithinRel(2.0 * c4, 1e-4));

    // Constant TIME: two octaves also take 100 ms, so at 50 ms it's one octave up.
    Player q(patch);
    q.on(48, 4800);
    const std::size_t u0 = q.out.size();
    q.on(72, 9600);
    CHECK_THAT(measuredHz(q.out, u0 + 2400 - 240, u0 + 2400 + 240), WithinRel(c4, 0.02));
}

TEST_CASE("Stealing: pedal-sustained voices go before held ones", "[play][pedal][voices]")
{
    Player p(sinePatch(), 3);
    p.on(60);           // held, oldest
    p.pedal(true);
    p.on(62);
    p.off(62);          // sustained by the pedal
    p.on(64);           // held
    p.on(65);           // over the limit of 3: someone must go
    std::vector<std::uint8_t> keys;
    for (const synth::Voice* v : p.live())
        keys.push_back(v->key());
    std::sort(keys.begin(), keys.end());
    CHECK(keys == std::vector<std::uint8_t>{60, 64, 65}); // 62 (pedal-only) was stolen, not 60 (oldest held)
}

TEST_CASE("No stuck notes: random notes, pedal and mode switches, then all keys up -> all voices free", "[play]")
{
    const auto mode = GENERATE(VoiceMode::Poly, VoiceMode::Mono, VoiceMode::Legato);
    auto patch = sinePatch(mode);
    patch.voice.glideMs = 30.0f;
    Player p(patch, 6);
    std::mt19937 rng(static_cast<unsigned>(mode) + 11);
    std::uniform_int_distribution<int> key(48, 72), kind(0, 9), len(1, 600);
    std::vector<bool> down(128, false);
    for (int i = 0; i < 3000; ++i) {
        const auto k = static_cast<std::uint8_t>(key(rng));
        switch (kind(rng)) {
        case 0: case 1: case 2: case 3: p.on(k, len(rng)); down[k] = true; break;
        case 4: case 5: case 6: p.off(k, len(rng)); down[k] = false; break;
        case 7: p.pedal(true, len(rng)); break;
        case 8: p.pedal(false, len(rng)); break;
        case 9:
            if (k % 4 == 0) // now and then, switch voice mode mid-performance
                p.send({Event::paramChange(0, synth::param_id::kVoiceMode, static_cast<float>(k % 3))}, len(rng));
            else
                p.send({Event::pitchBend(0, (static_cast<float>(k) - 60.0f) / 12.0f)}, len(rng));
            break;
        default: break;
        }
    }
    for (int k = 0; k < 128; ++k)
        if (down[static_cast<std::size_t>(k)])
            p.off(static_cast<std::uint8_t>(k), 1);
    p.pedal(false, 9600); // 200 ms: longer than any release
    CHECK(p.eng.busyVoices() == 0);
}
