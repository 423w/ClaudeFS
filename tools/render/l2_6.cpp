// L2.6 scenarios: what CFS costs, where the time goes, and the denormal trap. Console output only (plus one
// CSV): run it in the RELEASE build, on a quiet machine; numbers from Debug are meaningless for DSP.
//
//   1. Per-voice cost: N held notes of a full patch (4 ops + SVF + filter env + LFO 1 vibrato + LFO 2 on cutoff
//      and index), 256-sample blocks at 48 kHz; median / p99 / max time per block, µs per voice, % of budget.
//   2. Hot spots by ablation: the same 16 voices with features switched off one at a time.
//   3. Micro-benchmarks of the per-sample transcendental functions the voice calls.
//   4. Denormals: 64 resonant SVFs rung by one impulse, then silence; time per block over 4 s with and
//      without FTZ/DAZ (DenormalGuard). Also written to renders/l2.6_denormals.csv for plotting.
#include "render_util.h"
#include "scenarios.h"

#include <fss/dsp/svf.h>
#include <fss/engine.h>
#include <fss/synth/presets.h>
#include <fss/util/denormal_guard.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>

namespace fss::render {

namespace {

using Clock = std::chrono::steady_clock;
constexpr int kBlock = 256;
constexpr double kBudgetUs = 1e6 * kBlock / kSampleRate; // 5333 µs per block at 48 kHz

/// The "full" M4 voice: the FM pad with a resonant filter, filter envelope, vibrato and LFO 2 on cutoff + index.
synth::Patch fullPatch()
{
    synth::Patch p = synth::presets::evolvingPad();
    for (auto& op : p.ops) // reach sustain quickly so we measure the steady state, not the attack
        op.env.attackMs = 5.0f;
    p.filter.q = 4.0f;
    p.filter.envOctaves = 2.0f;
    p.filter.env = {.attackMs = 5.0f, .decayMs = 500.0f, .sustain = 0.5f, .releaseMs = 500.0f};
    p.lfo1.delayMs = p.lfo1.fadeMs = 0.0f;
    return p;
}

struct Timing {
    double medianUs, p99Us, maxUs;
};

/// Hold `voices` notes, let them settle for 0.5 s, then time `blocks` calls of Engine::process.
Timing timeEngine(const synth::Patch& patch, int voices, int blocks = 3000)
{
    Engine eng;
    eng.prepare(kSampleRate, kBlock);
    eng.setPatch(patch);
    std::vector<float> l(kBlock), r(kBlock);
    std::vector<Event> on;
    for (int i = 0; i < voices; ++i)
        on.push_back(Event::noteOn(0, static_cast<std::uint8_t>(40 + 2 * i), 0.8f));
    eng.process(on, l.data(), r.data(), kBlock);
    for (int i = 0; i < kSampleRate / 2 / kBlock; ++i)
        eng.process({}, l.data(), r.data(), kBlock);
    std::vector<double> us(static_cast<std::size_t>(blocks));
    volatile float sink = 0.0f; // keep the optimizer from deciding the output is unused
    for (double& t : us) {
        const auto t0 = Clock::now();
        eng.process({}, l.data(), r.data(), kBlock);
        t = std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
        sink = sink + l[7];
    }
    std::sort(us.begin(), us.end());
    return {us[us.size() / 2], us[us.size() * 99 / 100], us.back()};
}

void perVoiceCost()
{
    std::printf("  1. full patch, %d-sample blocks (budget %.0f us):\n", kBlock, kBudgetUs);
    std::printf("     %6s %10s %10s %10s %14s %8s\n", "voices", "median us", "p99 us", "max us", "us/voice/blk", "load");
    const double idle = timeEngine(fullPatch(), 0).medianUs;
    for (int n : {0, 1, 4, 8, 16}) {
        const Timing t = timeEngine(fullPatch(), n);
        std::printf("     %6d %10.1f %10.1f %10.1f %14.2f %7.1f%%\n", n, t.medianUs, t.p99Us, t.maxUs,
                    n ? (t.medianUs - idle) / n : 0.0, 100.0 * t.medianUs / kBudgetUs);
    }
}

void ablation()
{
    struct Variant {
        const char* name;
        void (*edit)(synth::Patch&);
    };
    const Variant variants[] = {
        {"full", [](synth::Patch&) {}},
        {"filter off", [](synth::Patch& p) { p.filter.mode = synth::FilterMode::Off; }},
        {"vibrato off", [](synth::Patch& p) { p.routes.vibratoSemitones = 0.0f; }},
        {"LFO2 routes off", [](synth::Patch& p) { p.routes.lfo2CutoffOctaves = p.routes.lfo2IndexAmount = 0.0f; }},
        {"FM only (all mod off)", [](synth::Patch& p) {
             p.filter.mode = synth::FilterMode::Off;
             p.routes = {.wheelVibratoSemitones = 0.0f};
         }},
    };
    std::printf("  2. hot spots by ablation, 16 voices (median us/block; difference = that feature's cost):\n");
    double full = 0.0;
    for (const Variant& v : variants) {
        synth::Patch p = fullPatch();
        v.edit(p);
        const double us = timeEngine(p, 16).medianUs;
        if (full == 0.0)
            full = us;
        std::printf("     %-24s %8.1f us   (%+7.1f us vs full, %5.2f us/voice)\n", v.name, us, us - full, us / 16.0);
    }
}

/// ns per call of f over many inputs (the sum is printed so the loop can't be optimized away).
template <typename F>
double nsPerCall(F f)
{
    constexpr int kN = 4'000'000;
    float acc = 0.0f, x = 0.001f;
    const auto t0 = Clock::now();
    for (int i = 0; i < kN; ++i) {
        acc += f(x);
        x += 1e-7f;
    }
    const double ns = std::chrono::duration<double, std::nano>(Clock::now() - t0).count() / kN;
    std::printf("%s", acc == 12345.0f ? " " : ""); // uses acc
    return ns;
}

void microBenchmarks()
{
    std::printf("  3. per-call cost (ns): exp2f %.2f, tanf %.2f, exp2 (double) %.2f, Svf::set %.2f\n",
                nsPerCall([](float x) { return std::exp2(x * 10.0f); }),
                nsPerCall([](float x) { return std::tan(x * 1.5f); }),
                nsPerCall([](float x) { return static_cast<float>(std::exp2(static_cast<double>(x) * 10.0)); }),
                nsPerCall([](float x) {
                    static dsp::Svf s = [] { dsp::Svf v; v.prepare(48000.0); return v; }();
                    s.set(100.0f + x * 10000.0f, 2.0f);
                    return s.q();
                }));
    std::printf("     exp (double) %.2f, expf %.2f, llround %.2f, (int64)(x + 0.5) %.2f\n",
                nsPerCall([](float x) { return static_cast<float>(std::exp(static_cast<double>(x) * 6.9)); }),
                nsPerCall([](float x) { return std::exp(x * 6.9f); }),
                nsPerCall([](float x) { return static_cast<float>(std::llround(static_cast<double>(x) * 1e9)); }),
                nsPerCall([](float x) { return static_cast<float>(static_cast<std::int64_t>(static_cast<double>(x) * 1e9 + 0.5)); }));
}

void denormals(const std::filesystem::path& dir)
{
    // 64 SVFs at Q 20, cutoffs spread 200 Hz..3 kHz, one impulse at t = 0, then nothing but zeros.
    constexpr int kFilters = 64, kBlocks = 4 * kSampleRate / kBlock;
    std::vector<double> timeOff(kBlocks), timeOn(kBlocks);
    std::vector<float> peak(kBlocks);
    for (int mode = 0; mode < 2; ++mode) {
        std::vector<dsp::Svf> bank(kFilters);
        for (int i = 0; i < kFilters; ++i) {
            bank[static_cast<std::size_t>(i)].prepare(kSampleRate);
            bank[static_cast<std::size_t>(i)].set(200.0f * std::pow(15.0f, static_cast<float>(i) / kFilters), 20.0f);
        }
        std::vector<float> out(kBlock);
        for (int b = 0; b < kBlocks; ++b) {
            const auto t0 = Clock::now();
            {
                // mode 1: the guard, exactly as Engine::process uses it.
                const bool guard = mode == 1;
                auto run = [&] {
                    for (int n = 0; n < kBlock; ++n) {
                        const float x = (b == 0 && n == 0) ? 1.0f : 0.0f;
                        float sum = 0.0f;
                        for (dsp::Svf& f : bank)
                            sum += f.tick(x).low;
                        out[static_cast<std::size_t>(n)] = sum;
                    }
                };
                if (guard) {
                    const util::DenormalGuard ftz;
                    run();
                } else {
                    run();
                }
            }
            const double us = std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
            (mode ? timeOn : timeOff)[static_cast<std::size_t>(b)] = us;
            if (mode == 0)
                peak[static_cast<std::size_t>(b)] = std::abs(out[0]);
        }
    }
    std::ofstream csv(dir / "l2.6_denormals.csv");
    csv << "t_s,us_without_ftz,us_with_ftz,abs_output\n";
    for (int b = 0; b < kBlocks; ++b)
        csv << b * kBlock / static_cast<double>(kSampleRate) << ',' << timeOff[static_cast<std::size_t>(b)] << ','
            << timeOn[static_cast<std::size_t>(b)] << ',' << peak[static_cast<std::size_t>(b)] << '\n';
    std::printf("  4. denormals: 64 resonant SVFs after one impulse (us per %d-sample block, median of each 0.25 s):\n", kBlock);
    std::printf("     %8s %14s %14s %16s\n", "t (s)", "no FTZ/DAZ", "FTZ/DAZ", "|output| (no FTZ)");
    const int per = kSampleRate / 4 / kBlock;
    for (int w = 0; w < kBlocks / per; ++w) {
        auto median = [&](std::vector<double> v) {
            std::vector<double> s(v.begin() + w * per, v.begin() + (w + 1) * per);
            std::sort(s.begin(), s.end());
            return s[s.size() / 2];
        };
        std::printf("     %8.2f %14.1f %14.1f %16.3g\n", w * 0.25, median(timeOff), median(timeOn),
                    static_cast<double>(peak[static_cast<std::size_t>(w * per)]));
    }
    std::printf("     DenormalGuard supported on this build: %s\n", util::DenormalGuard::supported() ? "yes" : "no");
}

} // namespace

void renderL2_6(const std::filesystem::path& dir)
{
    perVoiceCost();
    ablation();
    microBenchmarks();
    denormals(dir);
}

} // namespace fss::render
