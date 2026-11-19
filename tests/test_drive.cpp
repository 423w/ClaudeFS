// Tests for L5.2: the halfband filters and the oversampler, the Drive and Crush effects, the latency-aware FX
// slot, and the Engine's latency rules.
#include "alias_meter.h" // tools/render (on the include path via fss::wav)

#include <fss/dsp/halfband.h>
#include <fss/dsp/shapers.h>
#include <fss/engine.h>
#include <fss/fx/crush.h>
#include <fss/fx/drive.h>
#include <fss/fx/effect.h>
#include <fss/synth/params.h>
#include <fss/synth/presets.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>
#include <numbers>
#include <random>
#include <vector>

using namespace fss;

namespace {

constexpr double kFs = 48000.0;

/// |H(f)| in dB of a halfband (f in cycles per sample at the filter's own rate).
template <int N>
double halfbandDb(const dsp::Halfband<N>& hb, double f)
{
    std::complex<double> h = 0.5 * std::polar(1.0, -2.0 * std::numbers::pi * f * dsp::Halfband<N>::kCenter);
    for (int j = 0; j < dsp::Halfband<N>::kTaps; ++j)
        h += static_cast<double>(hb.taps()[static_cast<std::size_t>(j)]) * std::polar(1.0, -2.0 * std::numbers::pi * f * (2 * j));
    return 20.0 * std::log10(std::abs(h) + 1e-300);
}

fx::DriveParams driveParams(dsp::ShaperType t, float db, int osIndex, float toneDb = 0.0f)
{
    return {.enabled = true, .type = static_cast<std::uint8_t>(t), .driveDb = db, .toneDb = toneDb,
            .oversampling = static_cast<std::uint8_t>(osIndex)};
}

/// A Drive, prepared, set and committed (its oversampling factor in use), smoothers settled.
fx::Drive makeDrive(const fx::DriveParams& p)
{
    fx::Drive d;
    d.prepare(kFs, 512);
    d.setParams(p);
    d.commitLatency();
    return d;
}

std::vector<float> sine(std::size_t n, double hz, float amp)
{
    std::vector<float> x(n);
    for (std::size_t i = 0; i < n; ++i)
        x[i] = amp * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / kFs));
    return x;
}

/// Run a (stereo-identical) signal through an effect in blocks of 256; returns the left channel.
template <class Fx>
std::vector<float> run(Fx& fx, std::vector<float> x)
{
    std::vector<float> r = x;
    for (std::size_t s = 0; s < x.size(); s += 256) {
        const auto n = static_cast<int>(std::min<std::size_t>(256, x.size() - s));
        fx.process({x.data() + s, r.data() + s, n});
    }
    return x;
}

} // namespace

TEST_CASE("halfband: DC gain 1, flat passband, stopband below -94 dB (both stages)", "[drive][oversampling]")
{
    dsp::Halfband<63> s1;
    s1.design(9.6);
    dsp::Halfband<23> s2;
    s2.design(10.0);
    CHECK(std::abs(halfbandDb(s1, 0.0)) < 1e-5);
    CHECK(std::abs(halfbandDb(s2, 0.0)) < 1e-5);
    CHECK(std::abs(halfbandDb(s1, 0.25) + 6.0206) < 0.01); // a halfband is exactly 1/2 at a quarter of its rate
    double pass1 = 0.0, stop1 = -300.0, pass2 = 0.0, stop2 = -300.0;
    for (int i = 0; i <= 2000; ++i) {
        const double f = 0.5 * i / 2000.0;
        if (f <= 0.2) // stage 1: 0.2 at 2x = 0.4 of the base rate (19.2 kHz at 48 kHz)
            pass1 = std::max(pass1, std::abs(halfbandDb(s1, f)));
        if (f >= 0.3)
            stop1 = std::max(stop1, halfbandDb(s1, f));
        if (f <= 0.1) // stage 2 runs at 4x: the same 0.4·fs is 0.1 of its rate
            pass2 = std::max(pass2, std::abs(halfbandDb(s2, f)));
        if (f >= 0.4)
            stop2 = std::max(stop2, halfbandDb(s2, f));
    }
    CHECK(pass1 < 0.01);
    CHECK(stop1 < -94.0);
    CHECK(pass2 < 0.002);
    CHECK(stop2 < -94.0);
}

TEST_CASE("oversampler: latency 0 / 31 / 37 samples, an impulse comes out symmetric around it", "[drive][oversampling]")
{
    CHECK(dsp::Oversampler::latencyFor(1) == 0);
    CHECK(dsp::Oversampler::latencyFor(2) == 31);
    CHECK(dsp::Oversampler::latencyFor(4) == 37);
    for (int factor : {1, 2, 4}) {
        INFO("factor " << factor);
        dsp::Oversampler os;
        os.setFactor(factor);
        std::vector<float> y(400);
        for (std::size_t i = 0; i < y.size(); ++i)
            y[i] = os.process(i == 100 ? 1.0f : 0.0f, [](float v) { return v; });
        const auto peak = static_cast<int>(std::max_element(y.begin(), y.end()) - y.begin());
        const int c = 100 + os.latency();
        CHECK(peak == c);
        for (int k = 1; k < 60; ++k) // linear phase: the impulse response is symmetric about its centre
            CHECK(std::abs(y[static_cast<std::size_t>(c - k)] - y[static_cast<std::size_t>(c + k)]) < 1e-6f);
    }
}

TEST_CASE("shapers: bounded, 0 -> 0, slope 1 at 0; the tube offset is tanh(bias)", "[drive]")
{
    CHECK(std::abs(dsp::kTubeOffset - std::tanh(dsp::kTubeBias)) < 1e-7f);
    for (int t = 0; t < dsp::kNumShaperTypes; ++t) {
        const auto type = static_cast<dsp::ShaperType>(t);
        INFO("type " << t);
        CHECK(dsp::shape(type, 0.0f) == 0.0f);
        const float slope = dsp::shape(type, 1e-4f) / 1e-4f;
        CHECK(slope > 0.9f);
        CHECK(slope < 1.01f);
        for (float x : {-1e6f, -100.0f, -3.0f, 3.0f, 100.0f, 1e6f})
            CHECK(std::abs(dsp::shape(type, x)) <= 1.25f);
    }
}

TEST_CASE("drive: Hard below the clip point is transparent at 1x, at any tone (exact pre/post emphasis)", "[drive]")
{
    const auto x = sine(9600, 3000.0, 0.05f);
    for (float tone : {-12.0f, 0.0f, 12.0f}) {
        INFO("tone " << tone);
        auto d = makeDrive(driveParams(dsp::ShaperType::Hard, 6.0f, 0, tone)); // 0.05 · 2 · ≤4: never reaches 1
        const auto y = run(d, x);
        double err = 0.0;
        for (std::size_t i = 0; i < x.size(); ++i)
            err = std::max(err, static_cast<double>(std::abs(y[i] - x[i])));
        CHECK(20.0 * std::log10(err / 0.05 + 1e-30) < -100.0);
    }
}

TEST_CASE("drive: at 2x and 4x the clean output is the input delayed by exactly the latency", "[drive][oversampling]")
{
    const auto x = sine(9600, 1000.0, 0.05f);
    for (int os : {1, 2}) {
        auto d = makeDrive(driveParams(dsp::ShaperType::Hard, 6.0f, os));
        const int lat = d.latencySamples();
        INFO("latency " << lat);
        CHECK(lat == (os == 1 ? 31 : 37));
        const auto y = run(d, x);
        double err = 0.0;
        for (std::size_t i = 200; i < x.size(); ++i)
            err = std::max(err, static_cast<double>(std::abs(y[i] - x[i - static_cast<std::size_t>(lat)])));
        CHECK(20.0 * std::log10(err / 0.05) < -80.0);
        // Mix 50 %: dry and wet must be aligned, or a 50/50 mix would be a comb filter (a notch at fs/(2·latency)).
        auto params = driveParams(dsp::ShaperType::Hard, 6.0f, os);
        params.mixPct = 50.0f;
        auto m = makeDrive(params);
        const auto ym = run(m, sine(9600, kFs / (2.0 * lat), 0.05f)); // right on the would-be notch
        float peak = 0.0f;
        for (std::size_t i = 4800; i < ym.size(); ++i)
            peak = std::max(peak, std::abs(ym[i]));
        CHECK(peak > 0.049f);
    }
}

TEST_CASE("drive: oversampling reduces aliasing (measured on an exactly periodic sine)", "[drive][oversampling]")
{
    // f0 = k·fs/N with k odd (alias_meter.h): 3549·48000/65536 ≈ 2599 Hz. Prediction 1 in the L5.2 journal.
    constexpr std::size_t kN = 65536, kK = 3549;
    const double f0 = static_cast<double>(kK) * kFs / static_cast<double>(kN);
    auto aliasDb = [&](dsp::ShaperType t, float db, int os) {
        auto params = driveParams(t, db, os);
        params.autoGain = false; // a slowly settling gain would smear the harmonics into neighbouring bins
        auto d = makeDrive(params);
        auto y = run(d, sine(kN + 4096, f0, 0.25f));
        y.erase(y.begin(), y.begin() + 4096); // past the latency and the smoothers: one exact period block
        return render::measureAliasing(y, kK).aliasDb();
    };
    for (auto t : {dsp::ShaperType::Hard, dsp::ShaperType::Soft}) {
        INFO("type " << static_cast<int>(t));
        const double a1 = aliasDb(t, 24.0f, 0), a2 = aliasDb(t, 24.0f, 1), a4 = aliasDb(t, 24.0f, 2);
        INFO("1x " << a1 << " dB, 2x " << a2 << " dB, 4x " << a4 << " dB");
        CHECK(a2 < a1 - 6.0);
        CHECK(a4 < a2 - 6.0);
    }
}

TEST_CASE("drive: auto gain keeps a sine at its loudness, for every shaper, drive and level", "[drive]")
{
    for (int t = 0; t < dsp::kNumShaperTypes; ++t)
        for (float db : {0.0f, 12.0f, 24.0f, 36.0f})
            for (float amp : {0.03f, 0.25f, 0.9f}) {
            INFO("type " << t << ", drive " << db << " dB, amplitude " << amp);
            auto d = makeDrive(driveParams(static_cast<dsp::ShaperType>(t), db, 1));
            const auto y = run(d, sine(48000, 200.0, amp)); // 200 Hz: its harmonics stay below Nyquist
            double s = 0.0, m = 0.0;
            for (std::size_t i = 24000; i < y.size(); ++i)
                m += y[i];
            m /= 24000.0;
            for (std::size_t i = 24000; i < y.size(); ++i)
                s += (y[i] - m) * (y[i] - m);
            const double rmsDb = 10.0 * std::log10(s / 24000.0), refDb = 20.0 * std::log10(amp / std::numbers::sqrt2);
            CHECK(std::abs(rmsDb - refDb) < 0.2);
            }
}

TEST_CASE("crush: silence stays silent, values snap to the grid, the hold repeats each sample", "[drive][crush]")
{
    fx::Crush c;
    c.prepare(kFs, 512);
    c.setParams({.enabled = true, .bits = 4.0f, .rateHz = 12000.0f, .mixPct = 100.0f}); // step 1/8, hold 4 samples
    c.reset();
    std::vector<float> L(64), R(64, 0.0f);
    for (std::size_t i = 0; i < L.size(); ++i)
        L[i] = 0.9f * static_cast<float>(std::sin(0.37 * static_cast<double>(i)));
    const auto in = L;
    c.process({L.data(), R.data(), 64});
    for (std::size_t i = 0; i < L.size(); ++i) {
        INFO("sample " << i);
        CHECK(R[i] == 0.0f);
        CHECK(L[i] == std::nearbyint(in[i - i % 4] * 8.0f) / 8.0f); // held from the start of each group of 4
    }
}

TEST_CASE("FxSlot with latency: off is a pure delay; switching crossfades two aligned signals", "[drive][fx]")
{
    fx::FxSlot<fx::Drive> slot;
    slot.prepare(kFs, 64);
    slot.effect().setParams(driveParams(dsp::ShaperType::Hard, 6.0f, 1));
    CHECK(slot.latencyPending());
    slot.commitLatency();
    CHECK(slot.latencySamples() == 31);
    CHECK_FALSE(slot.latencyPending());
    const auto x = sine(9600, 440.0, 0.05f);
    std::vector<float> L = x, R = x;
    // Off for 3000 samples, on (a 10 ms fade) until 6000, then off again.
    for (std::size_t s = 0; s < x.size(); s += 64) {
        slot.setEnabled(s >= 3000 && s < 6000);
        slot.process(L.data() + s, R.data() + s, static_cast<int>(std::min<std::size_t>(64, x.size() - s)));
    }
    for (std::size_t i = 0; i < 3000; ++i) // off: exactly the input, 31 samples late
        REQUIRE(L[i] == (i < 31 ? 0.0f : x[i - 31]));
    double err = 0.0;
    for (std::size_t i = 3000; i < x.size(); ++i) // through both fades: still the same (aligned) signal
        err = std::max(err, static_cast<double>(std::abs(L[i] - x[i - 31])));
    CHECK(20.0 * std::log10(err / 0.05) < -60.0);
    CHECK(slot.tailSamples() >= 31);
}

TEST_CASE("Engine: a new oversampling factor applies at once, or (deferred) only at setPatch", "[drive][engine]")
{
    std::vector<float> l(64), r(64);
    for (bool defer : {false, true}) {
        INFO("defer " << defer);
        Engine eng;
        eng.setDeferLatencyChanges(defer);
        eng.prepare(kFs, 64);
        eng.setPatch(synth::Patch{});
        CHECK(eng.latencySamples() == 0);
        const Event ev[] = {Event::paramChange(0, synth::param_id::kDriveOversampling, 2.0f)};
        eng.process(ev, l.data(), r.data(), 64);
        CHECK(eng.latencySamples() == (defer ? 0 : 37));
        CHECK(eng.latencyPending() == defer);
        eng.setPatch(eng.patch()); // what the plugin's activate does
        CHECK(eng.latencySamples() == 37);
        CHECK_FALSE(eng.latencyPending());
    }
}

TEST_CASE("Engine: block-size invariance with drive (4x) and crush on, oversampling changing", "[drive][engine][invariance]")
{
    constexpr std::size_t kLen = 48000;
    std::vector<std::pair<std::size_t, Event>> score;
    std::mt19937 rng(52);
    std::uniform_int_distribution<std::size_t> when(0, kLen - 1);
    std::uniform_int_distribution<int> key(36, 84);
    for (int i = 0; i < 16; ++i) {
        const auto t = when(rng);
        const auto k = static_cast<std::uint8_t>(key(rng));
        score.push_back({t, Event::noteOn(0, k, 0.9f)});
        score.push_back({std::min(kLen - 1, t + 5000), Event::noteOff(0, k)});
    }
    score.push_back({when(rng), Event::paramChange(0, synth::param_id::kDrive, 30.0f)});
    score.push_back({when(rng), Event::paramChange(0, synth::param_id::kDriveTone, 9.0f)});
    score.push_back({when(rng), Event::paramChange(0, synth::param_id::kDriveType, 3.0f)});
    score.push_back({when(rng), Event::paramChange(0, synth::param_id::kDriveOversampling, 1.0f)});
    score.push_back({when(rng), Event::paramChange(0, synth::param_id::kCrushBits, 5.5f)});
    score.push_back({when(rng), Event::paramChange(0, synth::param_id::kDriveOn, 0.0f)});
    std::stable_sort(score.begin(), score.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    auto render = [&](int block) {
        Engine eng;
        eng.prepare(kFs, block);
        synth::Patch p = synth::presets::brass();
        p.fx.drive = driveParams(dsp::ShaperType::Soft, 18.0f, 2);
        p.fx.crush = {.enabled = true, .bits = 10.0f, .rateHz = 20000.0f, .mixPct = 60.0f};
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
