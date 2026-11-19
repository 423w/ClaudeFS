// Tests for L4.2: operator waveforms, PolyBLEP under phase modulation, live waveform changes, smoothed matrix
// slot amounts.
#include "alias_meter.h" // tools/render (on the include path via fss::wav)

#include <fss/dsp/op_waves.h>
#include <fss/engine.h>
#include <fss/synth/operator.h>
#include <fss/synth/params.h>
#include <fss/synth/presets.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <complex>
#include <numbers>
#include <span>
#include <string>
#include <vector>

using namespace fss;

namespace {

constexpr double kFs = 48000.0;

/// One cycle of waveform w, N points (exact phases), naive (dt = 0).
std::vector<float> cycle(dsp::OpWave w, int n)
{
    std::vector<float> y(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i)
        y[static_cast<std::size_t>(i)] = dsp::opWave(w, static_cast<dsp::Phase>(static_cast<std::uint64_t>(i) * (1ull << 32) / static_cast<std::uint64_t>(n)), 0.0);
    return y;
}

/// |harmonic h| of one cycle (a DFT bin).
double harmonic(const std::vector<float>& y, int h)
{
    std::complex<double> s = 0.0;
    const double n = static_cast<double>(y.size());
    for (std::size_t i = 0; i < y.size(); ++i)
        s += static_cast<double>(y[i]) * std::polar(1.0, -2.0 * std::numbers::pi * h * static_cast<double>(i) / n);
    return 2.0 * std::abs(s) / n;
}

double db(double x) { return 20.0 * std::log10(x + 1e-30); }

/// An operator as a carrier at f0 = k·fs/N (exactly periodic), with an optional sine modulator at the same
/// frequency (index I); returns N samples after a settling period.
std::vector<float> operatorTone(dsp::OpWave w, std::size_t n, std::size_t k, float index)
{
    synth::Operator car, mod;
    for (synth::Operator* o : {&car, &mod}) {
        o->prepare(kFs, 0.0f);
        o->setFrequency(static_cast<double>(k) * kFs / static_cast<double>(n));
        o->setEnvelope({.attackMs = 1.0f, .decayMs = 1.0f, .sustain = 1.0f, .releaseMs = 1.0f});
        o->noteOn();
    }
    car.setLevel(1.0f);
    car.setWave(w);
    mod.setLevel(index);
    std::vector<float> y;
    for (std::size_t i = 0; i < n + 4800; ++i) {
        const float v = car.tick(mod.tick());
        if (i >= 4800)
            y.push_back(v);
    }
    return y;
}

} // namespace

TEST_CASE("op waves: zero mean, bounded, Sine is the sine table", "[opwaves]")
{
    for (int w = 0; w < dsp::kNumOpWaves; ++w) {
        INFO("wave " << w);
        const auto y = cycle(static_cast<dsp::OpWave>(w), 4096);
        double mean = 0.0;
        for (float v : y) {
            mean += v;
            CHECK(std::abs(v) <= 1.0001f);
        }
        CHECK(std::abs(mean / 4096.0) < 2e-3); // DC removed: a carrier never thumps
    }
    for (dsp::Phase p : {0u, 0x12345678u, 0x80000000u, 0xFFFFFFFFu})
        CHECK(dsp::opWave(dsp::OpWave::Sine, p, 0.1) == dsp::sineTable(p));
}

TEST_CASE("op waves: the spectra the shapes promise (L4.2 prediction 1)", "[opwaves]")
{
    using W = dsp::OpWave;
    const auto half = cycle(W::HalfSine, 4096);
    CHECK(std::abs(harmonic(half, 1) - 0.5) < 1e-3);
    CHECK(std::abs(db(harmonic(half, 2) / harmonic(half, 1)) + 7.36) < 0.1); // 2/(3π) vs 1/2
    CHECK(harmonic(half, 3) < 1e-3);                                        // no odd harmonics past the 1st
    const auto abs = cycle(W::AbsSine, 4096);
    CHECK(harmonic(abs, 1) < 1e-3); // the fundamental is gone: an octave up
    CHECK(harmonic(abs, 3) < 1e-3);
    CHECK(std::abs(db(harmonic(abs, 4) / harmonic(abs, 2)) + 13.98) < 0.1); // 4/(15π) vs 4/(3π)
    const auto alt = cycle(W::AltSine, 4096);
    for (int h : {1, 3, 4, 5})
        CHECK(harmonic(alt, 2) > harmonic(alt, h)); // centred on the 2nd
    const auto quarter = cycle(W::QuarterSine, 4096);
    CHECK(harmonic(quarter, 15) > 0.25 * harmonic(quarter, 3) / 5.0); // jumps: a slow, saw-like roll-off
}

TEST_CASE("operator: saw carriers are band-limited, also under light phase modulation", "[opwaves]")
{
    // f0 = 3549·48000/65536 ≈ 2599 Hz (alias_meter.h). Naive = the same phases with dt = 0.
    constexpr std::size_t kN = 65536, kK = 3549;
    for (float index : {0.0f, 0.5f}) {
        INFO("index " << index);
        const auto blep = operatorTone(dsp::OpWave::Saw, kN, kK, index);
        // The naive version: same operator arithmetic, no correction.
        synth::Operator car, mod;
        for (synth::Operator* o : {&car, &mod}) {
            o->prepare(kFs, 0.0f);
            o->setFrequency(static_cast<double>(kK) * kFs / static_cast<double>(kN));
        }
        dsp::Phasor pc, pm;
        pc.prepare(kFs);
        pm.prepare(kFs);
        pc.setFrequency(static_cast<double>(kK) * kFs / static_cast<double>(kN));
        pm.setFrequency(static_cast<double>(kK) * kFs / static_cast<double>(kN));
        std::vector<float> naive;
        for (std::size_t i = 0; i < kN + 4800; ++i) {
            const float m = index * dsp::sineTable(pm.tick());
            const float v = dsp::opWave(dsp::OpWave::Saw, pc.tick() + synth::radiansToPhase(m), 0.0);
            if (i >= 4800)
                naive.push_back(v);
        }
        const double aBlep = render::measureAliasing(blep, kK).aliasDb();
        const double aNaive = render::measureAliasing(naive, kK).aliasDb();
        INFO("blep " << aBlep << " dB, naive " << aNaive << " dB");
        CHECK(aBlep < aNaive - 10.0);
    }
}

TEST_CASE("Engine: an operator's waveform changes while the note sounds; all-sine patches are unchanged", "[opwaves][engine]")
{
    auto render = [](bool change) {
        Engine eng;
        eng.prepare(kFs, 256);
        eng.setPatch(synth::presets::epiano());
        std::vector<float> L(9600), R(9600);
        const Event on[] = {Event::noteOn(0, 60, 0.8f)};
        eng.process(on, L.data(), R.data(), 4800);
        const Event w[] = {Event::paramChange(0, synth::param_id::op(1, synth::OpField::Wave), 6.0f)}; // B: saw
        eng.process(change ? std::span<const Event>(w) : std::span<const Event>(), L.data() + 4800, R.data() + 4800, 4800);
        return L;
    };
    const auto a = render(false), b = render(true);
    CHECK(std::equal(a.begin(), a.begin() + 4800, b.begin())); // identical up to the change
    double diff = 0.0;
    for (std::size_t i = 4800; i < a.size(); ++i)
        diff += std::abs(a[i] - b[i]);
    CHECK(diff > 1.0); // and different after it
    CHECK(std::string(synth::valueLabel(*synth::findParam(synth::param_id::op(0, synth::OpField::Wave)), 6)) == "Saw");
}

TEST_CASE("Engine: a matrix slot amount glides (no zipper) and lands exactly on its target", "[opwaves][engine][modmatrix]")
{
    // Slot 1: velocity -> Amp. A note at a constant level; at frame T the amount goes 0 -> 50 % (+12 dB).
    synth::Patch base = synth::presets::organ();
    base.mod.slots[0] = {.src = synth::ModSrc::Velocity, .dst = synth::ModDst::Amp, .amountPct = 0.0f};
    synth::Patch full = base;
    full.mod.slots[0].amountPct = 50.0f;
    constexpr int kT = 9600, kLen = 28800;
    auto render = [&](const synth::Patch& p, bool change) {
        Engine eng;
        eng.prepare(kFs, 512);
        eng.setPatch(p);
        std::vector<float> L(kLen), R(kLen);
        const Event on[] = {Event::noteOn(0, 57, 1.0f)};
        eng.process(on, L.data(), R.data(), kT);
        const Event a[] = {Event::paramChange(0, synth::param_id::modSlot(0, synth::param_id::ModField::Amount), 50.0f)};
        eng.process(change ? std::span<const Event>(a) : std::span<const Event>(), L.data() + kT, R.data() + kT, kLen - kT);
        return L;
    };
    const auto before = render(base, false), after = render(full, false), glide = render(base, true);
    // 1 ms after the change the gain is still close to where it was (a step would already be at +12 dB).
    double dOld = 0.0, dNew = 0.0;
    for (int i = kT; i < kT + 48; ++i) {
        dOld += std::abs(glide[static_cast<std::size_t>(i)] - before[static_cast<std::size_t>(i)]);
        dNew += std::abs(glide[static_cast<std::size_t>(i)] - after[static_cast<std::size_t>(i)]);
    }
    CHECK(dOld < dNew);
    // 300 ms later it has arrived: bit-identical to the patch that had 50 % all along. (The first version's glide
    // got stuck 0.002 % short of the target, forever: a constant −86 dB difference, and a recompile every sample.)
    CHECK(std::equal(glide.begin() + kT + 14400, glide.end(), after.begin() + kT + 14400));
}

namespace {

/// A bright, exactly periodic patch through the whole Engine: C (index 4, feedback 0.25 x 4 = 1) > D, both at a
/// fixed f0 = k·fs/N, no filter, no LFOs (alias_meter.h needs an exactly periodic output).
synth::Patch brightFixed(double f0, int fmOversampling)
{
    synth::Patch p;
    p.algorithm = synth::kAlgorithms[0];
    p.ops[0].level = p.ops[1].level = 0.0f;
    const dsp::AdsrParams env{.attackMs = 1.0f, .decayMs = 1.0f, .sustain = 1.0f, .releaseMs = 50.0f};
    p.ops[2] = {.fixed = true, .fixedHz = util::Hz{f0}, .level = 4.0f, .env = env};
    p.ops[3] = {.fixed = true, .fixedHz = util::Hz{f0}, .level = 0.5f, .env = env};
    p.algorithm.mod[2][2] = 0.25f;
    p.voice.fmOversampling = static_cast<std::uint8_t>(fmOversampling);
    return p;
}

} // namespace

TEST_CASE("Engine: FM oversampling (2x) cuts the audible aliasing of a bright high note", "[opwaves][engine][oversampling]")
{
    constexpr std::size_t kN = 65536, kK = 2859; // ~2094 Hz (C7), k odd
    const double f0 = static_cast<double>(kK) * kFs / static_cast<double>(kN);
    double alias[2]{};
    for (int os = 0; os < 2; ++os) {
        Engine eng;
        eng.prepare(kFs, 512);
        eng.setPatch(brightFixed(f0, os));
        std::vector<float> L(kN + 9600), R(kN + 9600);
        const Event on[] = {Event::noteOn(0, 96, 1.0f)};
        eng.process(on, L.data(), R.data(), 512);
        for (std::size_t s = 512; s < L.size(); s += 512)
            eng.process({}, L.data() + s, R.data() + s, static_cast<int>(std::min<std::size_t>(512, L.size() - s)));
        const std::vector<float> y(L.begin() + 9600, L.end());
        alias[os] = render::measureAliasing(y, kK, static_cast<std::size_t>(16000.0 * kN / kFs)).lowAliasDb();
    }
    INFO("alias below 16 kHz: 1x " << alias[0] << " dB, 2x " << alias[1] << " dB");
    CHECK(alias[1] < alias[0] - 20.0);
}

TEST_CASE("Engine: FM oversampling adds 16 samples of latency, deferred like the drive's", "[opwaves][engine][oversampling]")
{
    std::vector<float> l(64), r(64);
    for (bool defer : {false, true}) {
        INFO("defer " << defer);
        Engine eng;
        eng.setDeferLatencyChanges(defer);
        eng.prepare(kFs, 64);
        eng.setPatch(synth::Patch{});
        const Event ev[] = {Event::paramChange(0, synth::param_id::kFmOversampling, 1.0f),
                            Event::paramChange(0, synth::param_id::kDriveOversampling, 2.0f)};
        eng.process(ev, l.data(), r.data(), 64);
        CHECK(eng.latencySamples() == (defer ? 0 : 16 + 37));
        CHECK(eng.latencyPending() == defer);
        eng.setPatch(eng.patch());
        CHECK(eng.latencySamples() == 16 + 37);
        CHECK_FALSE(eng.latencyPending());
    }
    CHECK(Engine::kFmOversamplingLatency == 16);
}

TEST_CASE("Engine: block-size invariance with FM oversampling, unison and waveforms", "[opwaves][engine][invariance]")
{
    constexpr std::size_t kLen = 48000;
    std::vector<std::pair<std::size_t, Event>> score;
    std::mt19937 rng(42);
    std::uniform_int_distribution<std::size_t> when(0, kLen - 1);
    std::uniform_int_distribution<int> key(40, 96);
    for (int i = 0; i < 14; ++i) {
        const auto t = when(rng);
        const auto k = static_cast<std::uint8_t>(key(rng));
        score.push_back({t, Event::noteOn(0, k, 0.9f)});
        score.push_back({std::min(kLen - 1, t + 4000), Event::noteOff(0, k)});
    }
    score.push_back({when(rng), Event::paramChange(0, synth::param_id::op(3, synth::OpField::Wave), 7.0f)});
    score.push_back({when(rng), Event::paramChange(0, synth::param_id::kUnisonVoices, 5.0f)});
    score.push_back({when(rng), Event::paramChange(0, synth::param_id::modSlot(0, synth::param_id::ModField::Amount), -60.0f)});
    std::stable_sort(score.begin(), score.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    auto render = [&](int block) {
        Engine eng;
        eng.prepare(kFs, block);
        synth::Patch p = synth::presets::supersaw();
        p.unison.voices = 3;
        p.voice.fmOversampling = 1;
        p.mod.slots[0] = {.src = synth::ModSrc::Lfo2, .dst = synth::ModDst::Pitch, .amountPct = 10.0f};
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
