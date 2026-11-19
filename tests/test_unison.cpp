// Tests for unison (L4.4): layout, the bit-exact unison-1 path, click-free count changes, stereo, invariance.
#include <fss/engine.h>
#include <fss/synth/params.h>
#include <fss/synth/presets.h>
#include <fss/synth/voice.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <vector>

using namespace fss;

namespace {

constexpr double kFs = 48000.0;

synth::Patch sinePatch(int unison)
{
    synth::Patch p; // Init: carrier D only, A-C silent
    p.ops[0].level = p.ops[1].level = p.ops[2].level = 0.0f;
    p.ops[3].env = {.attackMs = 5.0f, .decayMs = 100.0f, .sustain = 1.0f, .releaseMs = 100.0f};
    p.outputDb = -12.0f;
    p.unison = {.voices = static_cast<std::uint8_t>(unison), .detuneCents = 20.0f, .widthPct = 100.0f};
    return p;
}

struct Stereo {
    std::vector<float> L, R;
};

Stereo render(const synth::Patch& p, std::size_t n, const std::vector<std::pair<std::size_t, Event>>& score, int block = 256)
{
    Engine eng;
    eng.prepare(kFs, block);
    eng.setPatch(p);
    Stereo out{std::vector<float>(n), std::vector<float>(n)};
    std::vector<Event> ev;
    std::size_t next = 0;
    for (std::size_t s = 0; s < n; s += static_cast<std::size_t>(block)) {
        const auto len = std::min<std::size_t>(static_cast<std::size_t>(block), n - s);
        ev.clear();
        while (next < score.size() && score[next].first < s + len) {
            Event e = score[next].second;
            e.frame = static_cast<std::uint32_t>(score[next].first - s);
            ev.push_back(e);
            ++next;
        }
        eng.process(ev, out.L.data() + s, out.R.data() + s, static_cast<int>(len));
    }
    return out;
}

float maxStep(const std::vector<float>& x, std::size_t from)
{
    float m = 0.0f;
    for (std::size_t i = from + 1; i < x.size(); ++i)
        m = std::max(m, std::abs(x[i] - x[i - 1]));
    return m;
}

double rms(const std::vector<float>& x, std::size_t from)
{
    double s = 0.0;
    for (std::size_t i = from; i < x.size(); ++i)
        s += static_cast<double>(x[i]) * x[i];
    return std::sqrt(s / static_cast<double>(x.size() - from));
}

} // namespace

TEST_CASE("unison layout: evenly spaced, inner positions first", "[unison]")
{
    CHECK(synth::unisonPosition(0, 1) == 0.0f);
    CHECK(synth::unisonPosition(0, 2) == -1.0f);
    CHECK(synth::unisonPosition(1, 2) == 1.0f);
    CHECK(synth::unisonPosition(0, 3) == 0.0f);
    CHECK(synth::unisonPosition(1, 3) == -1.0f);
    CHECK(synth::unisonPosition(2, 3) == 1.0f);
    CHECK(std::abs(synth::unisonPosition(0, 4) + 1.0f / 3.0f) < 1e-6f);
    CHECK(std::abs(synth::unisonPosition(1, 4) - 1.0f / 3.0f) < 1e-6f);
    CHECK(synth::unisonPosition(3, 4) == 1.0f);
    for (int n = 2; n <= synth::kMaxUnison; ++n) { // every layout spans exactly −1..1, each position once
        std::vector<float> p;
        for (int k = 0; k < n; ++k)
            p.push_back(synth::unisonPosition(k, n));
        std::sort(p.begin(), p.end());
        CHECK(p.front() == -1.0f);
        CHECK(std::abs(p.back() - 1.0f) < 1e-6f);
        for (std::size_t k = 1; k < p.size(); ++k)
            CHECK(std::abs((p[k] - p[k - 1]) - 2.0f / static_cast<float>(n - 1)) < 1e-5f);
    }
}

TEST_CASE("unison: 1 copy is mono (L == R), more copies are stereo at about the same level", "[unison]")
{
    const std::vector<std::pair<std::size_t, Event>> note = {{0, Event::noteOn(0, 57, 1.0f)}};
    const Stereo one = render(sinePatch(1), 48000, note);
    CHECK(one.L == one.R);
    const Stereo four = render(sinePatch(4), 48000, note);
    CHECK(four.L != four.R);
    // Per-channel level within 1.5 dB of the single copy (power-normalized copies, √2 constant-power pan).
    const double ratioDb = 20.0 * std::log10(0.5 * (rms(four.L, 4800) + rms(four.R, 4800)) / rms(one.L, 4800));
    INFO("4 copies vs 1: " << ratioDb << " dB per channel");
    CHECK(std::abs(ratioDb) < 1.5);
}

TEST_CASE("unison: changing the count while a note holds never clicks", "[unison]")
{
    synth::Patch p = sinePatch(1);
    const std::vector<std::pair<std::size_t, Event>> score = {
        {0, Event::noteOn(0, 57, 1.0f)},
        {9600, Event::paramChange(0, synth::param_id::kUnisonVoices, 4.0f)},
        {19200, Event::paramChange(0, synth::param_id::kUnisonVoices, 2.0f)},
        {28800, Event::paramChange(0, synth::param_id::kUnisonVoices, 8.0f)},
        {38400, Event::paramChange(0, synth::param_id::kUnisonVoices, 1.0f)},
        {43000, Event::paramChange(0, synth::param_id::kUnisonDetune, 80.0f)},
    };
    const Stereo s = render(p, 52000, score);
    // A 220 Hz sine at −12 dB moves at most 2π·220/48000·0.25 ≈ 0.0072 per sample; a copy popping in at full
    // level would step by up to ~0.25.
    CHECK(maxStep(s.L, 2000) < 0.02f);
    CHECK(maxStep(s.R, 2000) < 0.02f);
    // And it ends up mono again after going back to 1 copy.
    CHECK(std::equal(s.L.begin() + 48000, s.L.end(), s.R.begin() + 48000));
}

TEST_CASE("unison: the wide pad survives mono summing (prediction 2)", "[unison]")
{
    std::vector<std::pair<std::size_t, Event>> chord;
    for (std::uint8_t k : {std::uint8_t{48}, std::uint8_t{55}, std::uint8_t{64}})
        chord.push_back({0, Event::noteOn(0, k, 0.8f)});
    const Stereo s = render(synth::presets::widePad(), 4 * 48000, chord);
    std::vector<float> mono(s.L.size()), side(s.L.size());
    for (std::size_t i = 0; i < mono.size(); ++i) {
        mono[i] = 0.5f * (s.L[i] + s.R[i]);
        side[i] = 0.5f * (s.L[i] - s.R[i]);
    }
    const double chDb = 20.0 * std::log10(0.5 * (rms(s.L, 48000) + rms(s.R, 48000)));
    const double monoDb = 20.0 * std::log10(rms(mono, 48000));
    INFO("per channel " << chDb << " dB, mono sum " << monoDb << " dB, side " << 20.0 * std::log10(rms(side, 48000)));
    CHECK(monoDb > chDb - 3.0); // mono loses less than 3 dB: no big cancellation
    CHECK(rms(side, 48000) > 0.1 * rms(mono, 48000)); // and it really is wide
}

TEST_CASE("Engine: block-size invariance with unison, count changes and matrix pan", "[unison][invariance]")
{
    synth::Patch p = synth::presets::widePad();
    p.mod.slots[0] = {.src = synth::ModSrc::Lfo1, .dst = synth::ModDst::Pan, .amountPct = 50.0f};
    std::vector<std::pair<std::size_t, Event>> score;
    for (int i = 0; i < 10; ++i) {
        const auto k = static_cast<std::uint8_t>(50 + 3 * i);
        score.push_back({static_cast<std::size_t>(i) * 5000, Event::noteOn(0, k, 0.7f)});
        score.push_back({static_cast<std::size_t>(i) * 5000 + 12000, Event::noteOff(0, k)});
    }
    score.push_back({11000, Event::paramChange(0, synth::param_id::kUnisonVoices, 7.0f)});
    score.push_back({26000, Event::paramChange(0, synth::param_id::kUnisonWidth, 30.0f)});
    score.push_back({33000, Event::paramChange(0, synth::param_id::kUnisonVoices, 1.0f)});
    std::stable_sort(score.begin(), score.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    const Stereo ref = render(p, 60000, score, 4096);
    for (int block : {1, 64, 333}) {
        INFO("block " << block);
        const Stereo got = render(p, 60000, score, block);
        REQUIRE(std::memcmp(got.L.data(), ref.L.data(), ref.L.size() * sizeof(float)) == 0);
        REQUIRE(std::memcmp(got.R.data(), ref.R.data(), ref.R.size() * sizeof(float)) == 0);
    }
}
