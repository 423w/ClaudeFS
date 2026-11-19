// L5.2 scenarios: drive, oversampling, bitcrush.
//
//   (console)  prediction 1: alias power of a 2.6 kHz sine through each shaper at +12/+24 dB, at 1x/2x/4x
//              (alias_meter.h: an exactly periodic tone, so everything off the harmonics is aliasing)
//   l5.2_sweep_<type>_<n>x.wav   a 100 Hz → 12 kHz sine sweep through Hard/Soft/Fold +24 dB at 1x/2x/4x:
//              python tools/analyze/spectrogram.py renders/l5.2/l5.2_sweep_hard_1x.wav  (aliases = falling lines)
//   (console)  prediction 2: latency (the impulse's peak) at 44.1/48/96 kHz
//   (console)  prediction 3: Hard below the clip point vs the input, 1x and 4x; passband droop at 19/20 kHz
//   (console)  prediction 4: loudness with and without auto gain, E-piano and Brass, drive 0..36 dB
//   (console)  prediction 5: a 50 % mix at 4x on the would-be comb notch (fs / 74)
//   l5.2_bell_dry.wav / l5.2_bell_crush8.wav / l5.2_bell_crush_rate.wav   prediction 6: the bitcrusher on a bell
//   l5.2_brass_drive_1x.wav / _4x.wav           the Brass preset driven hard (Fold), 1x vs 4x: hear the aliasing
//   (console)  prediction 7: CPU per stereo sample
#include "alias_meter.h"
#include "engine_render.h"
#include "render_util.h"
#include "scenarios.h"

#include <fss/fx/crush.h>
#include <fss/fx/drive.h>
#include <fss/synth/presets.h>
#include <fss/util/random.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

namespace fss::render {

namespace {

constexpr const char* kTypeNames[] = {"Soft", "Hard", "Fold", "Tube"};

fx::DriveParams drive(dsp::ShaperType t, float db, int osIndex)
{
    return {.enabled = true, .type = static_cast<std::uint8_t>(t), .driveDb = db,
            .oversampling = static_cast<std::uint8_t>(osIndex)};
}

fx::Drive makeDrive(const fx::DriveParams& p, double fs = kSampleRate)
{
    fx::Drive d;
    d.prepare(fs, 256);
    d.setParams(p);
    d.commitLatency();
    return d;
}

/// Through the effect in 256-sample blocks, both channels the same; returns the left one.
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

std::vector<float> sine(std::size_t n, double hz, float amp, double fs = kSampleRate)
{
    std::vector<float> x(n);
    for (std::size_t i = 0; i < n; ++i)
        x[i] = amp * static_cast<float>(std::sin(2.0 * std::numbers::pi * hz * static_cast<double>(i) / fs));
    return x;
}

double rmsDb(const std::vector<float>& x, std::size_t from = 0)
{
    double s = 0.0;
    for (std::size_t i = from; i < x.size(); ++i)
        s += static_cast<double>(x[i]) * x[i];
    return 10.0 * std::log10(std::max(s / static_cast<double>(x.size() - from), 1e-24));
}

void aliasing()
{
    constexpr std::size_t kN = 65536, kK = 3549; // f0 = 3549·48000/65536 = 2599.4 Hz, k odd
    const double f0 = static_cast<double>(kK) * kSampleRate / static_cast<double>(kN);
    const std::size_t lowBins = static_cast<std::size_t>(16000.0 * kN / kSampleRate);
    std::printf("prediction 1: alias power of a %.1f Hz sine (amp 0.25), re the whole signal; [ ] = only below 16 kHz\n", f0);
    for (int t = 0; t < dsp::kNumShaperTypes; ++t)
        for (float db : {12.0f, 24.0f}) {
            std::printf("  %-4s +%2.0f dB:", kTypeNames[t], db);
            for (int os = 0; os < 3; ++os) {
                auto params = drive(static_cast<dsp::ShaperType>(t), db, os);
                params.autoGain = false; // a gain still settling would smear the harmonics into other bins
                auto d = makeDrive(params);
                auto y = run(d, sine(kN + 8192, f0, 0.25f));
                y.erase(y.begin(), y.begin() + 8192);
                const AliasMeasurement m = measureAliasing(y, kK, lowBins);
                std::printf("   %dx %7.1f dB [%7.1f]", 1 << os, m.aliasDb(), m.lowAliasDb());
            }
            std::printf("\n");
        }
}

void sweeps(const std::filesystem::path& out)
{
    const std::size_t n = 6 * kSampleRate;
    std::vector<float> x(n);
    double phase = 0.0;
    for (std::size_t i = 0; i < n; ++i) { // exponential sweep 100 Hz → 12 kHz
        const double f = 100.0 * std::pow(120.0, static_cast<double>(i) / static_cast<double>(n));
        phase += f / kSampleRate;
        x[i] = 0.25f * static_cast<float>(std::sin(2.0 * std::numbers::pi * phase));
    }
    const char* names[] = {"soft", "hard", "fold"};
    for (int t = 0; t < 3; ++t)
        for (int os = 0; os < 3; ++os) {
            auto d = makeDrive(drive(static_cast<dsp::ShaperType>(t), 24.0f, os));
            char file[64];
            std::snprintf(file, sizeof file, "l5.2_sweep_%s_%dx.wav", names[t], 1 << os);
            renderMono(out / file, run(d, x));
        }
}

void latency()
{
    for (double fs : {44100.0, 48000.0, 96000.0}) {
        std::printf("prediction 2: latency at %.1f kHz:", fs * 0.001);
        for (int os = 0; os < 3; ++os) {
            auto d = makeDrive(drive(dsp::ShaperType::Hard, 0.0f, os), fs);
            std::vector<float> x(512, 0.0f);
            x[100] = 0.01f;
            const auto y = run(d, x);
            const auto peak = std::max_element(y.begin(), y.end(), [](float a, float b) { return std::abs(a) < std::abs(b); }) - y.begin();
            std::printf("  %dx: peak at +%lld (reported %d)", 1 << os, static_cast<long long>(peak - 100), d.latencySamples());
        }
        std::printf("\n");
    }
}

void transparency()
{
    for (int os : {0, 2}) {
        auto d = makeDrive(drive(dsp::ShaperType::Hard, 6.0f, os));
        const int lat = d.latencySamples();
        const auto x = sine(48000, 1000.0, 0.05f);
        const auto y = run(d, x);
        double err = 0.0;
        for (std::size_t i = 1000; i < x.size(); ++i)
            err = std::max(err, static_cast<double>(std::abs(y[i] - x[i - static_cast<std::size_t>(lat)])));
        std::printf("prediction 3: Hard +6 dB on a 0.05 sine at %dx: max error vs the (delayed) input %.1f dB re signal\n",
                    1 << os, 20.0 * std::log10(err / 0.05 + 1e-30));
    }
    for (double fs : {44100.0, 48000.0})
        for (double hz : {10000.0, 19000.0, 20000.0}) {
            auto params = drive(dsp::ShaperType::Hard, 6.0f, 1);
            params.autoGain = false; // the auto gain would make up for the droop we want to see
            params.outputDb = -6.0f;  // undo the +6 dB drive instead
            auto d = makeDrive(params, fs);
            const auto y = run(d, sine(static_cast<std::size_t>(fs), hz, 0.05f, fs));
            float peak = 0.0f;
            for (std::size_t i = y.size() / 2; i < y.size(); ++i)
                peak = std::max(peak, std::abs(y[i]));
            std::printf("prediction 3: 2x passband at %.1f kHz, %5.0f Hz: %+.3f dB\n", fs * 0.001, hz, 20.0 * std::log10(peak / 0.05));
        }
}

void loudness()
{
    std::vector<ScoreNote> notes;
    for (int k : {48, 55, 60, 64, 67})
        notes.push_back({.startMs = 0.0, .key = static_cast<std::uint8_t>(k), .velocity = 100, .durationMs = 1500.0});
    for (auto [name, make] : {std::pair{"E-piano", synth::presets::epiano}, std::pair{"Brass", synth::presets::brass}}) {
        const StereoBuffer dry = renderWithEngine(make(), notes, {.tailMs = 300.0});
        const double ref = rmsDb(dry.left);
        std::printf("prediction 4: %-7s dry %.1f dBFS RMS; Soft drive, 2x, level change vs dry:\n", name, ref);
        for (bool autoGain : {false, true}) {
            std::printf("    auto gain %-3s:", autoGain ? "on" : "off");
            for (float db : {0.0f, 12.0f, 24.0f, 36.0f}) {
                synth::Patch p = make();
                p.fx.drive = drive(dsp::ShaperType::Soft, db, 1);
                p.fx.drive.autoGain = autoGain;
                const StereoBuffer wet = renderWithEngine(p, notes, {.tailMs = 300.0});
                std::printf("  %2.0f dB: %+5.1f", db, rmsDb(wet.left) - ref);
            }
            std::printf("\n");
        }
    }
}

void mixAlignment()
{
    auto p = drive(dsp::ShaperType::Hard, 6.0f, 2);
    p.mixPct = 50.0f;
    auto d = makeDrive(p);
    const double hz = kSampleRate / (2.0 * d.latencySamples());
    const auto y = run(d, sine(48000, hz, 0.05f));
    float peak = 0.0f;
    for (std::size_t i = 24000; i < y.size(); ++i)
        peak = std::max(peak, std::abs(y[i]));
    std::printf("prediction 5: 50 %% mix at 4x, %.1f Hz (where an unaligned dry would notch): %+.3f dB\n", hz,
                20.0 * std::log10(peak / 0.05));
}

void bell(const std::filesystem::path& out)
{
    std::vector<ScoreNote> notes;
    const std::uint8_t keys[] = {72, 79, 76, 84};
    for (int i = 0; i < 4; ++i)
        notes.push_back({.startMs = i * 900.0, .key = keys[i], .velocity = 110, .durationMs = 600.0});
    synth::Patch p = synth::presets::bell();
    writeStereo(out / "l5.2_bell_dry.wav", renderWithEngine(p, notes, {.tailMs = 3000.0}));
    p.fx.crush = {.enabled = true, .bits = 8.0f, .rateHz = 48000.0f, .mixPct = 100.0f};
    writeStereo(out / "l5.2_bell_crush8.wav", renderWithEngine(p, notes, {.tailMs = 3000.0}));
    p.fx.crush = {.enabled = true, .bits = 16.0f, .rateHz = 6000.0f, .mixPct = 100.0f};
    writeStereo(out / "l5.2_bell_crush_rate.wav", renderWithEngine(p, notes, {.tailMs = 3000.0}));
    p.fx.crush = {.enabled = true, .bits = 6.0f, .rateHz = 8000.0f, .mixPct = 100.0f};
    writeStereo(out / "l5.2_bell_crush_both.wav", renderWithEngine(p, notes, {.tailMs = 3000.0}));
}

void brass(const std::filesystem::path& out)
{
    std::vector<ScoreNote> notes;
    const std::uint8_t keys[] = {60, 64, 67, 72, 76, 79, 84, 88};
    for (int i = 0; i < 8; ++i)
        notes.push_back({.startMs = i * 400.0, .key = keys[i], .velocity = 110, .durationMs = 350.0});
    for (int os : {0, 2}) {
        synth::Patch p = synth::presets::brass();
        p.fx.drive = drive(dsp::ShaperType::Fold, 24.0f, os);
        char file[64];
        std::snprintf(file, sizeof file, "l5.2_brass_fold_%dx.wav", 1 << os);
        writeStereo(out / file, renderWithEngine(p, notes, {.tailMs = 500.0}));
    }
}

template <class Fx, class P>
void cost(const char* name, const P& params)
{
    Fx fx;
    fx.prepare(kSampleRate, 256);
    fx.setParams(params);
    if constexpr (requires { fx.commitLatency(); })
        fx.commitLatency();
    fx.reset();
    util::XorShift32 rng(7);
    std::vector<float> L(10 * kSampleRate), R;
    for (float& v : L)
        v = 0.3f * rng.bipolar();
    R = L;
    const auto t0 = std::chrono::steady_clock::now();
    for (std::size_t s = 0; s + 256 <= L.size(); s += 256)
        fx.process({L.data() + s, R.data() + s, 256});
    const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
    std::printf("prediction 7: CPU %-18s %6.1f ns per stereo sample (%.2f %% of one core at 48 kHz)\n", name,
                ns / static_cast<double>(L.size()), ns / static_cast<double>(L.size()) * 48000.0 * 1e-7);
}

} // namespace

void renderL5_2(const std::filesystem::path& outDir)
{
    aliasing();
    latency();
    transparency();
    mixAlignment();
    loudness();
    cost<fx::Drive>("drive Soft 1x", drive(dsp::ShaperType::Soft, 24.0f, 0));
    cost<fx::Drive>("drive Soft 2x", drive(dsp::ShaperType::Soft, 24.0f, 1));
    cost<fx::Drive>("drive Soft 4x", drive(dsp::ShaperType::Soft, 24.0f, 2));
    cost<fx::Drive>("drive Hard 4x", drive(dsp::ShaperType::Hard, 24.0f, 2));
    cost<fx::Crush>("crush", fx::CrushParams{.enabled = true});
    sweeps(outDir);
    bell(outDir);
    brass(outDir);
}

} // namespace fss::render
