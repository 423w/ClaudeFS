// Tests for the modulated-delay effects (L5.4): chorus, flanger, phaser.
#include <fss/engine.h>
#include <fss/fx/chorus.h>
#include <fss/fx/flanger.h>
#include <fss/fx/phaser.h>
#include <fss/synth/params.h>
#include <fss/synth/presets.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <numbers>
#include <random>
#include <vector>

using namespace fss;

namespace {

constexpr double kFs = 48000.0;

/// Steady-state peak of an effect's left output for a sine at `hz` (the second half of 0.5 s).
template <class Fx>
float sineGain(Fx& fx, double hz)
{
    fx.reset();
    const int n = 24000;
    std::vector<float> L(n), R(n);
    for (int i = 0; i < n; ++i)
        L[static_cast<std::size_t>(i)] = R[static_cast<std::size_t>(i)] =
            static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * i / kFs));
    for (int s = 0; s < n; s += 256)
        fx.process({L.data() + s, R.data() + s, std::min(256, n - s)});
    float peak = 0.0f;
    for (int i = n / 2; i < n; ++i)
        peak = std::max(peak, std::abs(L[static_cast<std::size_t>(i)]));
    return peak;
}

float maxStep(const std::vector<float>& x)
{
    float m = 0.0f;
    for (std::size_t i = 1; i < x.size(); ++i)
        m = std::max(m, std::abs(x[i] - x[i - 1]));
    return m;
}

} // namespace

TEST_CASE("Chorus: with no depth, every voice is the same pure delay", "[modfx]")
{
    fx::Chorus c;
    c.prepare(kFs, 256);
    for (int voices : {1, 3}) {
        c.setParams({.enabled = true, .rateHz = 1.0f, .depthMs = 0.0f, .delayMs = 10.0f,
                     .voices = static_cast<std::uint8_t>(voices), .mixPct = 100.0f});
        c.reset();
        std::vector<float> L(1000, 0.0f), R(1000, 0.0f);
        L[0] = R[0] = 1.0f;
        c.process({L.data(), R.data(), 1000});
        // 10 ms = 480 samples, the cubic read exact at a whole sample. The voices are normalized by power
        // (1/sqrt(N)), right for moving, uncorrelated copies; identical copies (depth 0) add up to sqrt(N).
        const float want = std::sqrt(static_cast<float>(voices));
        CHECK(std::abs(L[480] - want) < 1e-5f);
        CHECK(std::abs(R[480] - want) < 1e-5f);
        CHECK(std::abs(L[0]) < 1e-6f);
    }
}

TEST_CASE("Chorus: makes a mono input stereo, and changing the voice count doesn't click", "[modfx]")
{
    fx::Chorus c;
    c.prepare(kFs, 256);
    fx::ChorusParams p{.enabled = true, .rateHz = 0.8f, .depthMs = 4.0f, .delayMs = 8.0f, .voices = 2, .mixPct = 50.0f};
    c.setParams(p);
    c.reset();
    const std::size_t n = 48000;
    std::vector<float> L(n), R(n);
    for (std::size_t i = 0; i < n; ++i)
        L[i] = R[i] = 0.5f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 300.0 * static_cast<double>(i) / kFs));
    for (std::size_t s = 0; s < n; s += 256) {
        if (s == 12032) {
            p.voices = 4;
            c.setParams(p);
        }
        if (s == 24064) {
            p.voices = 1;
            c.setParams(p);
        }
        c.process({L.data() + s, R.data() + s, static_cast<int>(std::min<std::size_t>(256, n - s))});
    }
    float diff = 0.0f;
    for (std::size_t i = 0; i < n; ++i)
        diff = std::max(diff, std::abs(L[i] - R[i]));
    CHECK(diff > 0.05f);       // stereo
    // A 300 Hz sine of 0.5 moves at most 0.02 per sample; the wobble adds little. A voice popping in or out
    // would step by up to ~0.5.
    CHECK(maxStep(L) < 0.05f);
    CHECK(maxStep(R) < 0.05f);
}

TEST_CASE("Flanger: a frozen 1 ms comb notches 500 Hz and passes 1 kHz (prediction 3)", "[modfx]")
{
    fx::Flanger f;
    f.prepare(kFs, 256);
    f.setParams({.enabled = true, .rateHz = 0.1f, .delayMs = 1.0f, .depthMs = 0.0f, .feedbackPct = 0.0f, .mixPct = 50.0f});
    CHECK(sineGain(f, 500.0) < 0.01f);
    CHECK(sineGain(f, 1500.0) < 0.01f);
    CHECK(std::abs(sineGain(f, 1000.0) - 1.0f) < 0.01f);
    CHECK(std::abs(sineGain(f, 2000.0) - 1.0f) < 0.01f);
    // Positive feedback sharpens the peaks: the 1 kHz peak rises to 0.5 + 0.5/(1 − fb) = 1.5 at fb 0.5.
    f.setParams({.enabled = true, .rateHz = 0.1f, .delayMs = 1.0f, .depthMs = 0.0f, .feedbackPct = 50.0f, .mixPct = 50.0f});
    CHECK(std::abs(sineGain(f, 1000.0) - 1.5f) < 0.02f);
    // ...and deepens the notch: 500 Hz falls to 0.5 − 0.5/(1 + fb) = 0.167.
    CHECK(std::abs(sineGain(f, 500.0) - 1.0f / 6.0f) < 0.01f);
    // Negative feedback moves the feedback path's resonances to 500 Hz (where z^−D = −1), which in a 50 % mix
    // FILLS the notch and flattens the peak: 500 Hz → 0.5 − 0.5/(1 − 0.5) ... = |0.5 − 1| = 0.5, 1 kHz → 0.833.
    // (The journal's prediction 3 said "swaps peaks and notches": true of the wet path alone, not of the mix.)
    f.setParams({.enabled = true, .rateHz = 0.1f, .delayMs = 1.0f, .depthMs = 0.0f, .feedbackPct = -50.0f, .mixPct = 50.0f});
    CHECK(std::abs(sineGain(f, 1000.0) - 5.0f / 6.0f) < 0.01f);
    CHECK(std::abs(sineGain(f, 500.0) - 0.5f) < 0.01f);
}

TEST_CASE("Phaser: 6 stages at 1 kHz notch exactly 268 Hz, 1 kHz and 3.66 kHz (prediction 4)", "[modfx]")
{
    // The digital (TPT, bilinear) allpass has phase −2·atan(tan(πf/fs)/g), g = tan(π·fc/fs); notches where
    // 6 stages reach an odd multiple of π: f = fs/π · atan(g · tan((2k+1)·π/12)).
    fx::Phaser ph;
    ph.prepare(kFs, 256);
    ph.setParams({.enabled = true, .rateHz = 0.1f, .centerHz = 1000.0f, .depthOct = 0.0f, .feedbackPct = 0.0f, .stages = 6,
                  .mixPct = 50.0f});
    const double g = std::tan(std::numbers::pi * 1000.0 / kFs);
    for (int k = 0; k < 3; ++k) {
        const double notch = kFs / std::numbers::pi * std::atan(g * std::tan((2 * k + 1) * std::numbers::pi / 12.0));
        INFO("notch " << k << " at " << notch << " Hz");
        CHECK(sineGain(ph, notch) < 0.01f);
    }
    CHECK(sineGain(ph, 600.0) > 0.3f);   // between notches: plenty passes
    CHECK(sineGain(ph, 10000.0) > 0.3f); // and there's no fourth notch above (a flanger would have hundreds)
    // Only N/2 notches: sweep 50 Hz .. 20 kHz in 1/24-octave steps and count the dips below 0.05.
    int dips = 0;
    bool inDip = false;
    for (double f = 50.0; f < 20000.0; f *= std::pow(2.0, 1.0 / 24.0)) {
        const bool low = sineGain(ph, f) < 0.15f;
        dips += (low && !inDip) ? 1 : 0;
        inDip = low;
    }
    CHECK(dips == 3);
}

TEST_CASE("Phaser: changing the stage count crossfades (no click)", "[modfx]")
{
    fx::Phaser ph;
    ph.prepare(kFs, 256);
    fx::PhaserParams p{.enabled = true, .rateHz = 0.5f, .centerHz = 700.0f, .depthOct = 2.0f, .feedbackPct = 60.0f,
                       .stages = 4, .mixPct = 50.0f};
    ph.setParams(p);
    ph.reset();
    const std::size_t n = 36000;
    std::vector<float> L(n), R(n);
    for (std::size_t i = 0; i < n; ++i)
        L[i] = R[i] = 0.4f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 220.0 * static_cast<double>(i) / kFs));
    for (std::size_t s = 0; s < n; s += 256) {
        if (s == 12032) {
            p.stages = 12;
            ph.setParams(p);
        }
        if (s == 24064) {
            p.stages = 2;
            ph.setParams(p);
        }
        ph.process({L.data() + s, R.data() + s, static_cast<int>(std::min<std::size_t>(256, n - s))});
    }
    CHECK(maxStep(L) < 0.06f); // the sine alone: 0.0115 per sample; with feedback resonance a few times that
}

TEST_CASE("Engine: block-size invariance with chorus, flanger and phaser on", "[modfx][engine][invariance]")
{
    constexpr std::size_t kLen = 48000;
    std::vector<std::pair<std::size_t, Event>> score;
    std::mt19937 rng(3);
    std::uniform_int_distribution<std::size_t> when(0, kLen - 1);
    std::uniform_int_distribution<int> key(48, 84);
    for (int i = 0; i < 20; ++i) {
        const auto t = when(rng);
        const auto k = static_cast<std::uint8_t>(key(rng));
        score.push_back({t, Event::noteOn(0, k, 0.8f)});
        score.push_back({std::min(kLen - 1, t + 4000), Event::noteOff(0, k)});
    }
    score.push_back({when(rng), Event::paramChange(0, synth::param_id::kChorusVoices, 4.0f)});
    score.push_back({when(rng), Event::paramChange(0, synth::param_id::kPhaserStages, 10.0f)});
    score.push_back({when(rng), Event::paramChange(0, synth::param_id::kFlangerOn, 0.0f)});
    std::stable_sort(score.begin(), score.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    auto render = [&](int block) {
        Engine eng;
        eng.prepare(kFs, block);
        synth::Patch p = synth::presets::epianoChorus();
        p.fx.flanger.enabled = true;
        p.fx.phaser.enabled = true;
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
