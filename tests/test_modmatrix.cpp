// Tests for the modulation matrix (L4.1). The first test is the GOLDEN test the lesson asks for: every factory
// preset, rendered with a fixed score, must produce exactly the same bits as before the matrix existed.
// The expected hashes were computed with the pre-L4.1 engine (commit 0c31d7f, L5.4) and pasted here.
#include <fss/engine.h>
#include <fss/synth/params.h>
#include <fss/synth/presets.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace fss;

namespace {

constexpr double kFs = 48000.0;

/// A score that exercises everything the L1.11 routes touch except the mod wheel: notes (LFO 1 vibrato with
/// its delay/fade, LFO 2 on cutoff and index), pitch bend, sustain pedal, retriggers. 2 s.
std::vector<float> goldenRender(const synth::Patch& patch, bool moveWheel)
{
    Engine eng;
    eng.prepare(kFs, 256);
    eng.setPatch(patch);
    struct At {
        std::uint32_t at;
        Event e;
    };
    std::vector<At> score = {
        {0, Event::noteOn(0, 48, 0.9f)},       {0, Event::noteOn(0, 55, 0.7f)},
        {9000, Event::noteOn(0, 64, 0.5f)},    {20000, Event::pitchBend(0, 0.4f)},
        {30000, Event::controller(0, 64, 1.0f)}, {31000, Event::noteOff(0, 48)},
        {40000, Event::pitchBend(0, -0.3f)},   {48000, Event::noteOn(0, 67, 1.0f)},
        {60000, Event::controller(0, 64, 0.0f)}, {62000, Event::noteOff(0, 55)},
        {70000, Event::noteOff(0, 64)},        {80000, Event::noteOff(0, 67)},
    };
    if (moveWheel) {
        score.push_back({15000, Event::controller(0, 1, 0.8f)});
        score.push_back({50000, Event::controller(0, 1, 0.2f)});
    }
    std::stable_sort(score.begin(), score.end(), [](const At& a, const At& b) { return a.at < b.at; });
    const std::uint32_t total = 96000;
    std::vector<float> L(total), R(total);
    std::vector<Event> block;
    std::size_t next = 0;
    for (std::uint32_t s = 0; s < total; s += 256) {
        block.clear();
        while (next < score.size() && score[next].at < s + 256) {
            Event e = score[next].e;
            e.frame = score[next].at - s;
            block.push_back(e);
            ++next;
        }
        eng.process(block, L.data() + s, R.data() + s, 256);
    }
    L.insert(L.end(), R.begin(), R.end());
    return L;
}

std::uint64_t fnv1a(const std::vector<float>& x)
{
    std::uint64_t h = 1469598103934665603ull;
    const auto* b = reinterpret_cast<const unsigned char*>(x.data());
    for (std::size_t i = 0; i < x.size() * sizeof(float); ++i) {
        h ^= b[i];
        h *= 1099511628211ull;
    }
    return h;
}

struct Golden {
    const char* name;
    std::uint64_t hash;
};
// Pre-L4.1 engine (0c31d7f). Regenerate only for a change that is MEANT to alter these sounds, and say why.
// One table per build type: an unoptimized (Debug) build rounds some float operations differently from an
// optimized one (5 of the 12 hashes differ between them), so each is compared with the old code built the
// same way. Both tables were produced from commit 0c31d7f (Debug: /Od /Ob0 /RTC1, like CMake's Debug preset).
#ifndef NDEBUG
constexpr Golden kGolden[] = {
    {"E-piano", 0xcdd95f928d5a0043ull},     {"FM bass", 0x411363fee2563973ull},
    {"Reso pluck", 0xa63081745282d4e7ull},  {"Wobble bass", 0x957171c571436303ull},
    {"FM pad", 0x1847e6a466ccfd87ull},      {"Legato bass", 0xb69cc35063a94adbull},
    {"Bell", 0xeba676492b8da4fbull},        {"Brass", 0x6017734a7a234253ull},
    {"Organ", 0xb4fb85818c6e1dbbull},       {"Marimba", 0x1175d8013787c483ull},
    {"E-piano chorus", 0x43d409a9e520f198ull}, {"Init", 0x35d9298d8a7e87bfull},
};
#else
constexpr Golden kGolden[] = {
    {"E-piano", 0x08c8e7263e5fda23ull},     {"FM bass", 0x411363fee2563973ull},
    {"Reso pluck", 0xa63081745282d4e7ull},  {"Wobble bass", 0xc75e8260e529f0ebull},
    {"FM pad", 0x1847e6a466ccfd87ull},      {"Legato bass", 0xb69cc35063a94adbull},
    {"Bell", 0x680534a2d6f7035full},        {"Brass", 0x6017734a7a234253ull},
    {"Organ", 0xb4fb85818c6e1dbbull},       {"Marimba", 0x836271eb38fd8267ull},
    {"E-piano chorus", 0xaf0446fc1a4a5034ull}, {"Init", 0x1a424268b45b609full},
};
#endif

} // namespace

TEST_CASE("mod matrix golden test: every factory preset renders bit-identically to the pre-matrix engine", "[modmatrix][golden]")
{
    for (const synth::presets::NamedPreset& p : synth::presets::kFactoryPresets) {
        const std::uint64_t h = fnv1a(goldenRender(p.make(), false));
        const Golden* g = nullptr;
        for (const Golden& k : kGolden)
            if (std::string(k.name) == p.name)
                g = &k;
        INFO(p.name << " hash 0x" << std::hex << h);
        if (!g)
            continue; // a preset added since L4.1 ("Evolving matrix") has no pre-matrix version to match
        if (g->hash == 0ull)
            std::printf("    {\"%s\", 0x%016llxull},\n", p.name, static_cast<unsigned long long>(h)); // first run
        CHECK(g->hash == h);
    }
}

TEST_CASE("mod matrix golden test, with the mod wheel: optional raw dump for a before/after comparison", "[modmatrix][golden]")
{
    // With the wheel moving on a patch that also has vibrato, the matrix sums the two vibrato routes as two
    // slots (lfo1·(fade·vib) + lfo1·(wheel·wheelVib)) where L1.11 had lfo1·(vib·fade + wheel·wheelVib): the same
    // maths, rounded differently. Set CFS_GOLDEN_DUMP=<folder> to write each preset's render as raw float32
    // (L block then R block); the L4.1 journal compares the pre- and post-matrix dumps.
    char* dir = nullptr;
    std::size_t len = 0;
    if (_dupenv_s(&dir, &len, "CFS_GOLDEN_DUMP") != 0 || !dir) {
        SUCCEED("CFS_GOLDEN_DUMP not set: nothing to dump");
        return;
    }
    const std::string folder(dir);
    std::free(dir);
    for (const synth::presets::NamedPreset& p : synth::presets::kFactoryPresets) {
        const auto x = goldenRender(p.make(), true);
        std::FILE* f = nullptr;
        if (fopen_s(&f, (folder + "/" + p.name + ".f32").c_str(), "wb") == 0 && f) {
            std::fwrite(x.data(), sizeof(float), x.size(), f);
            std::fclose(f);
        }
    }
}

namespace {

/// One note through an Engine; returns L (mono voices: L = R before stereo FX).
std::vector<float> renderNote(const synth::Patch& p, std::uint8_t key, float velocity, int frames,
                              const std::vector<Event>& extra = {})
{
    Engine eng;
    eng.prepare(kFs, 512);
    eng.setPatch(p);
    std::vector<Event> ev = {Event::noteOn(0, key, velocity, 1)};
    ev.insert(ev.end(), extra.begin(), extra.end());
    std::vector<float> L(static_cast<std::size_t>(frames)), R(static_cast<std::size_t>(frames));
    eng.process(ev, L.data(), R.data(), frames);
    return L;
}

/// Frequency from upward zero crossings over the second half of a buffer.
double zeroCrossingHz(const std::vector<float>& x)
{
    int n = 0;
    std::size_t first = 0, last = 0;
    for (std::size_t i = x.size() / 2 + 1; i < x.size(); ++i)
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            if (n == 0)
                first = i;
            last = i;
            ++n;
        }
    return n > 1 ? (n - 1) * kFs / static_cast<double>(last - first) : 0.0;
}

float peakOf(const std::vector<float>& x, std::size_t from)
{
    float m = 0.0f;
    for (std::size_t i = from; i < x.size(); ++i)
        m = std::max(m, std::abs(x[i]));
    return m;
}

synth::Patch quietSine()
{
    synth::Patch p; // Init: one carrier (D) at the key's pitch, operators A-C silent
    p.ops[0].level = p.ops[1].level = p.ops[2].level = 0.0f;
    p.ops[3].env = {.attackMs = 1.0f, .decayMs = 100.0f, .sustain = 1.0f, .releaseMs = 100.0f};
    p.outputDb = -12.0f; // below the soft clip's knee
    return p;
}

std::vector<float> renderTwo(const synth::Patch& p, const std::vector<Event>& extra)
{
    Engine eng;
    eng.prepare(kFs, 512);
    eng.setPatch(p);
    std::vector<Event> ev = {Event::noteOn(0, 60, 1.0f, 1), Event::noteOn(0, 67, 1.0f, 2)};
    ev.insert(ev.end(), extra.begin(), extra.end());
    std::vector<float> L(9600), R(9600);
    eng.process(ev, L.data(), R.data(), 9600);
    return L;
}

} // namespace

TEST_CASE("mod matrix: a slot moves its destination in its unit (mod env -> pitch +12 st = 880 Hz)", "[modmatrix]")
{
    synth::Patch p = quietSine();
    p.mod.env = {.attackMs = 1.0f, .decayMs = 1000.0f, .sustain = 1.0f, .releaseMs = 100.0f};
    p.mod.slots[3] = {.src = synth::ModSrc::ModEnv, .dst = synth::ModDst::Pitch, .amountPct = 50.0f}; // 50 % of 24 st
    CHECK(std::abs(zeroCrossingHz(renderNote(p, 69, 1.0f, 24000)) - 880.0) < 2.0);
    p.mod.slots[3].amountPct = 0.0f;
    CHECK(std::abs(zeroCrossingHz(renderNote(p, 69, 1.0f, 24000)) - 440.0) < 1.0);
}

TEST_CASE("mod matrix: per-voice sources (velocity -> op level) and a via (velocity x wheel)", "[modmatrix]")
{
    synth::Patch p = quietSine();
    p.mod.slots[0] = {.src = synth::ModSrc::Velocity, .dst = synth::ModDst::OpDLevel, .amountPct = -100.0f};
    CHECK(peakOf(renderNote(p, 60, 1.0f, 4800), 2400) < 1e-6f); // x max(0, 1 - 1) = silent at full velocity
    CHECK(peakOf(renderNote(p, 60, 0.5f, 4800), 2400) > 0.05f); // x 0.5 at half velocity
    // A via: velocity x mod wheel. Wheel down: nothing happens even at full velocity.
    p.mod.slots[0].via = synth::ModSrc::ModWheel;
    CHECK(peakOf(renderNote(p, 60, 1.0f, 4800), 2400) > 0.1f);
    CHECK(peakOf(renderNote(p, 60, 1.0f, 4800, {Event::controller(0, 1, 1.0f)}), 4000) < 0.02f); // wheel up: silent
}

TEST_CASE("mod matrix: CLAP-style polyphonic modulation moves one note's cutoff only", "[modmatrix][polymod]")
{
    synth::Patch p = quietSine();
    p.filter = {.mode = synth::FilterMode::LowPass, .cutoffHz = 2000.0f, .q = 0.7071f};
    const auto none = renderTwo(p, {});
    // A modulation for a note that isn't playing changes nothing, bit for bit.
    CHECK(renderTwo(p, {Event::paramMod(0, synth::param_id::kCutoff, -0.5f, 7)}) == none);
    // For note id 1: a different output...
    const auto one = renderTwo(p, {Event::paramMod(0, synth::param_id::kCutoff, -0.5f, 1)});
    CHECK(one != none);
    // ...and the difference is exactly what the modulation does to note 1 played alone.
    synth::Patch alone = p;
    const auto n1mod = renderNote(alone, 60, 1.0f, 9600, {Event::paramMod(0, synth::param_id::kCutoff, -0.5f, 1)});
    const auto n1 = renderNote(alone, 60, 1.0f, 9600);
    float err = 0.0f, effect = 0.0f;
    for (std::size_t i = 0; i < one.size(); ++i) {
        err = std::max(err, std::abs((one[i] - none[i]) - (n1mod[i] - n1[i])));
        effect = std::max(effect, std::abs(n1mod[i] - n1[i]));
    }
    CHECK(effect > 0.01f); // the modulation did something to note 1...
    CHECK(err < 1e-5f);    // ...and nothing else changed
    // A key-addressed modulation (no note id) hits the note on that key.
    CHECK(renderTwo(p, {Event::paramMod(0, synth::param_id::kCutoff, -0.5f, -1, 60)}) == one);
    // Only declared parameters are host-modulatable: a PARAM_MOD on the output level is ignored.
    CHECK(renderTwo(p, {Event::paramMod(0, synth::param_id::kOutputDb, -0.5f, 1)}) == none);
    // No note and no key: every note, as a monophonic modulation.
    CHECK(renderTwo(p, {Event::paramMod(0, synth::param_id::kCutoff, -0.5f)}) != one);
}

TEST_CASE("Engine: block-size invariance with every kind of matrix slot and host modulation", "[modmatrix][invariance]")
{
    synth::Patch p = synth::presets::evolvingMatrix();
    p.mod.slots[5] = {.src = synth::ModSrc::Lfo2, .dst = synth::ModDst::Pitch, .amountPct = 1.0f}; // a global slot
    p.mod.slots[6] = {.src = synth::ModSrc::Velocity, .via = synth::ModSrc::ModWheel, .dst = synth::ModDst::Lfo1Rate,
                      .amountPct = 30.0f};
    p.mod.slots[7] = {.src = synth::ModSrc::FilterEnv, .dst = synth::ModDst::Resonance, .amountPct = 50.0f};
    constexpr std::size_t kLen = 60000;
    std::vector<std::pair<std::size_t, Event>> score;
    for (int i = 0; i < 12; ++i) {
        const auto t = static_cast<std::size_t>(i) * 4700;
        const auto k = static_cast<std::uint8_t>(48 + (i * 7) % 30);
        score.push_back({t, Event::noteOn(0, k, 0.4f + 0.05f * static_cast<float>(i), i)});
        score.push_back({t + 9000, Event::noteOff(0, k, i)});
    }
    score.push_back({7000, Event::controller(0, 1, 0.7f)});
    score.push_back({15000, Event::paramMod(0, synth::param_id::kCutoff, -0.3f, 2)});
    score.push_back({22000, Event::paramMod(0, synth::param_id::kResonance, 0.2f)});
    score.push_back({30000, Event::paramChange(0, synth::param_id::modSlot(2, synth::param_id::ModField::Amount), -60.0f)});
    std::stable_sort(score.begin(), score.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    auto render = [&](int block) {
        Engine eng;
        eng.prepare(kFs, block);
        eng.setPatch(p);
        std::vector<float> L(kLen), R(kLen);
        std::vector<Event> ev;
        std::size_t next = 0;
        for (std::size_t s = 0; s < kLen; s += static_cast<std::size_t>(block)) {
            const auto n = std::min<std::size_t>(static_cast<std::size_t>(block), kLen - s);
            ev.clear();
            while (next < score.size() && score[next].first < s + n) {
                Event e = score[next].second;
                e.frame = static_cast<std::uint32_t>(score[next].first - s);
                ev.push_back(e);
                ++next;
            }
            eng.process(ev, L.data() + s, R.data() + s, static_cast<int>(n));
        }
        L.insert(L.end(), R.begin(), R.end());
        return L;
    };
    const auto ref = render(4096);
    for (int block : {1, 64, 333}) {
        INFO("block " << block);
        const auto got = render(block);
        REQUIRE(std::memcmp(got.data(), ref.data(), ref.size() * sizeof(float)) == 0);
    }
}
