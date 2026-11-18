// Tests for the delay line (L5.3) and the stereo delay effect built on it.
#include <fss/dsp/delay_line.h>
#include <fss/engine.h>
#include <fss/fx/delay.h>
#include <fss/fx/effect.h>
#include <fss/synth/params.h>

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

/// Push an impulse, then read `delay` after each push: the impulse response of the read.
template <class Interp>
std::vector<float> impulseResponse(double delay, int length)
{
    dsp::DelayLine<Interp> line;
    line.prepare(64);
    std::vector<float> y;
    for (int t = 0; t < length; ++t) {
        line.push(t == 0 ? 1.0f : 0.0f);
        y.push_back(line.read(delay));
    }
    return y;
}

fx::DelayParams wetOnly(float feedbackPct)
{
    return {.enabled = true, .sync = true, .division = 6, .feedbackPct = feedbackPct, .pingPong = true,
            .dampingHz = 20000.0f, .mixPct = 100.0f};
}

float maxStep(const std::vector<float>& x)
{
    float m = 0.0f;
    for (std::size_t i = 1; i < x.size(); ++i)
        m = std::max(m, std::abs(x[i] - x[i - 1]));
    return m;
}

} // namespace

TEST_CASE("DelayLine: integer delays are exact with every interpolation", "[delay]")
{
    for (int d : {1, 5, 17}) {
        INFO("delay " << d);
        const auto none = impulseResponse<dsp::NoInterp>(d, 30);
        const auto lin = impulseResponse<dsp::LinearInterp>(d, 30);
        const auto cub = impulseResponse<dsp::CubicInterp>(d, 30);
        for (int t = 0; t < 30; ++t) {
            const float want = t == d ? 1.0f : 0.0f;
            CHECK(none[static_cast<std::size_t>(t)] == want);
            CHECK(lin[static_cast<std::size_t>(t)] == want);
            CHECK(std::abs(cub[static_cast<std::size_t>(t)] - want) < 1e-6f);
        }
    }
}

TEST_CASE("DelayLine: a fractional delay lands between the samples (prediction 1)", "[delay]")
{
    const auto lin = impulseResponse<dsp::LinearInterp>(10.25, 20);
    CHECK(lin[10] == 0.75f);
    CHECK(lin[11] == 0.25f);
    CHECK(lin[9] == 0.0f);
    CHECK(lin[12] == 0.0f);

    const auto cub = impulseResponse<dsp::CubicInterp>(10.25, 20);
    float sum = 0.0f;
    for (float v : cub)
        sum += v;
    CHECK(std::abs(sum - 1.0f) < 1e-6f); // DC gain 1: the four Lagrange weights add to one
    CHECK(cub[10] > 0.7f);                // most of it at 10 and 11...
    CHECK(cub[11] > 0.2f);
    CHECK(cub[9] < 0.0f);                 // ...with the small negative lobes on either side
    CHECK(cub[12] < 0.0f);
    CHECK(cub[8] == 0.0f);
    CHECK(cub[13] == 0.0f);
}

TEST_CASE("StereoDelay: ping-pong echoes alternate L, R, L at the synced time", "[delay][fx]")
{
    fx::StereoDelay d;
    d.prepare(kFs, 512);
    d.setParams(wetOnly(50.0f));
    d.setTempo(120.0); // 1/8 dotted at 120 BPM: 0.375 s = 18000 samples (prediction 4)
    d.reset();
    REQUIRE(d.targetSamples() == 18000.0);
    std::vector<float> L(60000, 0.0f), R(60000, 0.0f);
    L[0] = R[0] = 1.0f;
    for (std::size_t s = 0; s < L.size(); s += 512)
        d.process({L.data() + s, R.data() + s, static_cast<int>(std::min<std::size_t>(512, L.size() - s))});
    CHECK(L[0] == 0.0f); // mix 100 %: no dry
    CHECK(L[18000] == 1.0f); // echo 1: left only, undamped (the damping is in the loop, after the tap)
    CHECK(R[18000] == 0.0f);
    auto energyAround = [](const std::vector<float>& x, std::size_t c) {
        float s = 0.0f;
        for (std::size_t i = c - 50; i < c + 50; ++i)
            s += x[i];
        return s;
    };
    CHECK(std::abs(energyAround(R, 36000) - 0.5f) < 1e-3f); // echo 2: right, × feedback
    CHECK(std::abs(energyAround(L, 36000)) < 1e-6f);
    CHECK(std::abs(energyAround(L, 54000) - 0.25f) < 1e-3f); // echo 3: left again
}

TEST_CASE("StereoDelay: the time follows the tempo, or the ms knob when sync is off", "[delay][fx]")
{
    fx::StereoDelay d;
    d.prepare(kFs, 64);
    fx::DelayParams p = wetOnly(0.0f);
    d.setParams(p);
    d.setTempo(100.0);
    CHECK(d.targetSamples() == 21600.0); // 0.75 beat at 100 BPM = 0.45 s
    p.division = 8;                      // 1/4
    d.setParams(p);
    CHECK(d.targetSamples() == 28800.0);
    p.sync = false;
    p.timeMs = 250.0f;
    d.setParams(p);
    CHECK(d.targetSamples() == 12000.0);
    d.setTempo(10.0); // clamped to 20 BPM; and a whole note at 20 BPM (12 s) is clamped to the 4 s maximum
    p.sync = true;
    p.division = 13;
    d.setParams(p);
    CHECK(d.targetSamples() == 4.0 * kFs);
}

TEST_CASE("StereoDelay: changing the time while it plays never jumps (clean and tape)", "[delay][fx]")
{
    for (auto mode : {fx::DelayTimeMode::Clean, fx::DelayTimeMode::Tape}) {
        INFO("mode " << static_cast<int>(mode));
        fx::StereoDelay d;
        d.prepare(kFs, 256);
        fx::DelayParams p = wetOnly(60.0f);
        p.timeMode = mode;
        p.sync = false;
        p.timeMs = 300.0f;
        d.setParams(p);
        d.reset();
        const std::size_t n = 96000;
        std::vector<float> L(n), R(n);
        for (std::size_t i = 0; i < n; ++i) // a 200 Hz sine for the first second, then let the echoes run
            L[i] = R[i] = i < 48000 ? 0.5f * static_cast<float>(std::sin(2.0 * std::numbers::pi * 200.0 * static_cast<double>(i) / kFs)) : 0.0f;
        for (std::size_t s = 0; s < n; s += 256) {
            if (s == 40192) { // mid-echo: halve the time, then a second change during the first's crossfade
                p.timeMs = 150.0f;
                d.setParams(p);
            }
            if (s == 41216) {
                p.timeMs = 420.0f;
                d.setParams(p);
            }
            d.process({L.data() + s, R.data() + s, 256});
        }
        // The material's own steepest step: 2π·200/48000 × 0.5 × (sum of echoes ≤ 1/(1−0.6)) ≈ 0.033.
        // A jump of the read position would be a step of up to ~2.
        CHECK(maxStep(L) < 0.12f);
        CHECK(maxStep(R) < 0.12f);
    }
}

TEST_CASE("StereoDelay: the tail follows feedback and time (prediction 5)", "[delay][fx]")
{
    fx::StereoDelay d;
    d.prepare(kFs, 64);
    fx::DelayParams p = wetOnly(35.0f);
    d.setParams(p);
    d.setTempo(120.0);
    d.reset();
    // 0.35 per repeat: 90 dB in ceil(4.5 / 0.456) = 10 repeats, plus the first echo.
    CHECK(d.tailSamples() == 18001 * 11);
    p.feedbackPct = 0.0f;
    d.setParams(p);
    CHECK(d.tailSamples() == 18001); // one echo
    p.feedbackPct = 90.0f;
    d.setParams(p);
    CHECK(d.tailSamples() == 18001 * (1 + 99)); // ~37 s
}

TEST_CASE("Engine: block-size invariance with the delay on, tempo changes and time changes", "[delay][engine][invariance]")
{
    constexpr std::size_t kLen = 72000;
    struct At {
        std::size_t at;
        Event e;
    };
    std::vector<At> score;
    std::mt19937 rng(11);
    std::uniform_int_distribution<std::size_t> when(0, kLen - 1);
    std::uniform_int_distribution<int> key(48, 84);
    for (int i = 0; i < 30; ++i) {
        const auto t = when(rng);
        const auto k = static_cast<std::uint8_t>(key(rng));
        score.push_back({t, Event::noteOn(0, k, 0.8f)});
        score.push_back({std::min(kLen - 1, t + 3000), Event::noteOff(0, k)});
    }
    score.push_back({when(rng), Event::tempo(0, 97.0f)});
    score.push_back({when(rng), Event::tempo(0, 140.0f)});
    score.push_back({when(rng), Event::paramChange(0, synth::param_id::kDelaySync, 0.0f)});
    score.push_back({when(rng), Event::paramChange(0, synth::param_id::kDelayTime, 80.0f)});
    score.push_back({when(rng), Event::paramChange(0, synth::param_id::kDelayTimeMode, 1.0f)});
    score.push_back({when(rng), Event::paramChange(0, synth::param_id::kDelayOn, 0.0f)}); // spill
    score.push_back({when(rng), Event::paramChange(0, synth::param_id::kDelayOn, 1.0f)});
    std::stable_sort(score.begin(), score.end(), [](const At& a, const At& b) { return a.at < b.at; });

    auto render = [&](int block) {
        Engine eng;
        eng.prepare(kFs, block);
        synth::Patch p;
        p.fx.delay = {.enabled = true, .division = 4, .feedbackPct = 60.0f, .mixPct = 40.0f}; // 1/8 T, short
        eng.setPatch(p);
        std::vector<float> L(kLen), R(kLen);
        std::vector<Event> ev;
        std::size_t next = 0;
        for (std::size_t s = 0; s < kLen; s += static_cast<std::size_t>(block)) {
            const auto n = std::min<std::size_t>(static_cast<std::size_t>(block), kLen - s);
            ev.clear();
            while (next < score.size() && score[next].at < s + n) {
                Event e = score[next].e;
                e.frame = static_cast<std::uint32_t>(score[next].at - s);
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
    // And the delay really made the channels differ (ping-pong).
    float diff = 0.0f;
    for (std::size_t i = 0; i < kLen; ++i)
        diff = std::max(diff, std::abs(ref[i] - ref[kLen + i]));
    CHECK(diff > 0.01f);
}
