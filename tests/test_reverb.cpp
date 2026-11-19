// Tests for L5.5: the FDN reverb (its decay IS the knob: the lesson's exit criterion), and the delay line's
// O(1) lazy reset.
#include "rt60.h" // tools/render (on the include path via fss::wav)

#include <fss/dsp/delay_line.h>
#include <fss/engine.h>
#include <fss/fx/reverb.h>
#include <fss/synth/params.h>
#include <fss/synth/presets.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <vector>

using namespace fss;

namespace {

constexpr double kFs = 48000.0;

fx::ReverbParams wetOnly(float decayS, float dampingPct = 0.0f)
{
    return {.enabled = true, .decayS = decayS, .sizePct = 100.0f, .predelayMs = 0.0f, .dampingPct = dampingPct,
            .modPct = 30.0f, .lowCutHz = 20.0f, .mixPct = 100.0f};
}

struct Ir {
    std::vector<float> L, R;
};

/// The reverb's response to a unit impulse on both channels, `seconds` long.
Ir impulseResponse(const fx::ReverbParams& p, double seconds)
{
    fx::Reverb rv;
    rv.prepare(kFs, 512);
    rv.setParams(p);
    rv.reset();
    const auto n = static_cast<std::size_t>(seconds * kFs);
    Ir ir{std::vector<float>(n, 0.0f), std::vector<float>(n, 0.0f)};
    ir.L[0] = ir.R[0] = 1.0f;
    for (std::size_t s = 0; s < n; s += 512) {
        const auto len = static_cast<int>(std::min<std::size_t>(512, n - s));
        rv.process({ir.L.data() + s, ir.R.data() + s, len});
    }
    return ir;
}

} // namespace

TEST_CASE("reverb: the measured RT60 is the Decay knob, 0.3 to 10 s (L5.5 exit criterion)", "[reverb]")
{
    for (float rt : {0.3f, 1.0f, 2.0f, 5.0f, 10.0f}) {
        INFO("decay " << rt << " s");
        const Ir ir = impulseResponse(wetOnly(rt), std::max(1.0, 0.8 * rt) + 0.3);
        const render::DecayFit fit = render::measureRt60(ir.L, kFs);
        const render::DecayFit mid = render::measureRt60Band(ir.L, kFs, 1000.0);
        INFO("measured broadband " << fit.rt60 << " s (r^2 " << fit.r2 << "), 1 kHz band " << mid.rt60 << " s");
        // RT60 is quoted at mid frequencies (acoustics' convention): there the knob is exact to a few %. Broadband
        // the top octave pulls it ~6 % short (the cubic read inside the loop loses a little treble each pass).
        CHECK(std::abs(mid.rt60 / rt - 1.0) < 0.05);
        CHECK(std::abs(fit.rt60 / rt - 1.0) < 0.10); // the lesson's exit criterion: within ~10 %
        CHECK(fit.r2 > 0.99);                         // a straight line in dB: one clean exponential decay
    }
}

TEST_CASE("reverb: damping shortens the highs and keeps the 1 kHz band at the knob", "[reverb]")
{
    const Ir ir = impulseResponse(wetOnly(2.0f, 50.0f), 2.5);
    const double mid = render::measureRt60Band(ir.L, kFs, 1000.0).rt60;
    const double high = render::measureRt60Band(ir.L, kFs, 8000.0).rt60;
    INFO("1 kHz " << mid << " s, 8 kHz " << high << " s");
    CHECK(std::abs(mid / 2.0 - 1.0) < 0.1);
    CHECK(high < 0.85 * mid);
    CHECK(high > 0.4 * mid);
}

TEST_CASE("reverb: dense within 60 ms, wide (L/R uncorrelated), silent in -> silent out", "[reverb]")
{
    const Ir ir = impulseResponse(wetOnly(2.0f), 1.5);
    CHECK(render::echoDensity(ir.L, static_cast<std::size_t>(0.06 * kFs), 960) > 0.8);
    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (std::size_t i = static_cast<std::size_t>(0.1 * kFs); i < ir.L.size(); ++i) {
        lr += static_cast<double>(ir.L[i]) * ir.R[i];
        ll += static_cast<double>(ir.L[i]) * ir.L[i];
        rr += static_cast<double>(ir.R[i]) * ir.R[i];
    }
    const double rho = lr / std::sqrt(ll * rr);
    INFO("L/R correlation " << rho);
    CHECK(std::abs(rho) < 0.1);
    // Silence after a reset is exact silence (the FX slot relies on it to sleep).
    fx::Reverb rv;
    rv.prepare(kFs, 256);
    rv.setParams(wetOnly(5.0f));
    rv.reset();
    std::vector<float> L(4096, 0.0f), R(4096, 0.0f);
    rv.process({L.data(), R.data(), 4096});
    CHECK(std::all_of(L.begin(), L.end(), [](float v) { return v == 0.0f; }));
    CHECK(rv.tailSamples() >= static_cast<int>(1.5 * 5.0 * kFs));
}

TEST_CASE("DelayLine: the O(1) lazy reset reads exactly like a cleared buffer", "[reverb][delay]")
{
    std::mt19937 rng(17);
    std::uniform_real_distribution<float> sig(-1.0f, 1.0f);
    std::uniform_real_distribution<double> del(0.0, 300.0);
    dsp::DelayLine<dsp::CubicInterp> used, fresh;
    used.prepare(256);
    fresh.prepare(256);
    for (int i = 0; i < 1000; ++i)
        used.push(sig(rng)); // fill it with history that must never come back
    used.reset();
    for (int i = 0; i < 400; ++i) {
        const float x = sig(rng);
        used.push(x);
        fresh.push(x);
        for (int k = 0; k < 8; ++k) {
            const double d = del(rng);
            REQUIRE(used.read(d) == fresh.read(d));
            REQUIRE(used.read<dsp::LinearInterp>(d) == fresh.read<dsp::LinearInterp>(d));
            REQUIRE(used.read<dsp::NoInterp>(d) == fresh.read<dsp::NoInterp>(d));
        }
    }
}

TEST_CASE("Engine: block-size invariance with the reverb on and its settings changing", "[reverb][engine][invariance]")
{
    constexpr std::size_t kLen = 48000;
    std::vector<std::pair<std::size_t, Event>> score;
    std::mt19937 rng(55);
    std::uniform_int_distribution<std::size_t> when(0, kLen - 1);
    std::uniform_int_distribution<int> key(48, 90);
    for (int i = 0; i < 12; ++i) {
        const auto t = when(rng);
        const auto k = static_cast<std::uint8_t>(key(rng));
        score.push_back({t, Event::noteOn(0, k, 0.9f)});
        score.push_back({std::min(kLen - 1, t + 3000), Event::noteOff(0, k)});
    }
    score.push_back({when(rng), Event::paramChange(0, synth::param_id::kReverbSize, 160.0f)});
    score.push_back({when(rng), Event::paramChange(0, synth::param_id::kReverbDecay, 6.0f)});
    score.push_back({when(rng), Event::paramChange(0, synth::param_id::kReverbPredelay, 80.0f)});
    score.push_back({when(rng), Event::paramChange(0, synth::param_id::kReverbDamping, 90.0f)});
    score.push_back({when(rng), Event::paramChange(0, synth::param_id::kReverbOn, 0.0f)});
    std::stable_sort(score.begin(), score.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    auto render = [&](int block) {
        Engine eng;
        eng.prepare(kFs, block);
        synth::Patch p = synth::presets::bell();
        p.fx.reverb = {.enabled = true, .mixPct = 40.0f};
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
