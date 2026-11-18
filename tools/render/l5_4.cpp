// L5.4 scenarios: chorus, flanger, phaser.
//
//   l5.4_epiano_dry.wav / l5.4_epiano_chorus.wav   the "E-piano" and "E-piano chorus" presets, same chords
//   l5.4_flanger_noise.wav / l5.4_phaser_noise.wav pink-ish noise through a slow sweep of each (listen)
//   l5.4_flanger_ir.wav / l5.4_phaser_ir.wav       impulse responses with the LFO frozen (depth 0), mix 50 %:
//                                                  python tools/analyze/plot_fx_response.py renders/l5.4/l5.4_*_ir.wav
//                                                  shows the comb (flanger) vs the few notches (phaser)
//   (console)  prediction 2: a 5 kHz sine through a delay swept like a chorus voice, read with linear and with
//              cubic interpolation, against the exact (analytic) delayed sine: level ripple and error in dB
//   (console)  prediction 6: the chorused e-piano's L/R difference and its mono-sum level vs the dry one
#include "engine_render.h"
#include "render_util.h"
#include "scenarios.h"

#include <fss/dsp/delay_line.h>
#include <fss/fx/chorus.h>
#include <fss/fx/flanger.h>
#include <fss/fx/phaser.h>
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

std::vector<ScoreNote> chords()
{
    std::vector<ScoreNote> s;
    const std::uint8_t voicings[4][4] = {{52, 59, 64, 68}, {50, 57, 62, 65}, {48, 55, 60, 64}, {47, 55, 59, 62}};
    for (int bar = 0; bar < 4; ++bar)
        for (std::uint8_t k : voicings[bar])
            s.push_back({.startMs = bar * 1200.0, .key = k, .velocity = 90, .durationMs = 1100.0});
    return s;
}

double rmsDb(const std::vector<float>& x)
{
    double s = 0.0;
    for (float v : x)
        s += static_cast<double>(v) * v;
    return 10.0 * std::log10(std::max(s / static_cast<double>(x.size()), 1e-24));
}

void epiano(const std::filesystem::path& out)
{
    const StereoBuffer dry = renderWithEngine(synth::presets::epiano(), chords(), {.tailMs = 1500.0});
    const StereoBuffer wet = renderWithEngine(synth::presets::epianoChorus(), chords(), {.tailMs = 1500.0});
    writeStereo(out / "l5.4_epiano_dry.wav", dry);
    writeStereo(out / "l5.4_epiano_chorus.wav", wet);
    std::vector<float> monoDry(dry.left.size()), monoWet(wet.left.size()), side(wet.left.size());
    for (std::size_t i = 0; i < monoDry.size(); ++i)
        monoDry[i] = 0.5f * (dry.left[i] + dry.right[i]);
    for (std::size_t i = 0; i < monoWet.size(); ++i) {
        monoWet[i] = 0.5f * (wet.left[i] + wet.right[i]);
        side[i] = 0.5f * (wet.left[i] - wet.right[i]);
    }
    std::printf("prediction 6: e-piano mono sum: dry %.2f dB, chorus %.2f dB (%+.2f dB); chorus side/mid %.1f dB\n",
                rmsDb(monoDry), rmsDb(monoWet), rmsDb(monoWet) - rmsDb(monoDry), rmsDb(side) - rmsDb(monoWet));
}

/// White noise, lightly low-passed (more pleasant, and still flat enough to show the filtering).
std::vector<float> noise(std::size_t n)
{
    util::XorShift32 rng(12345);
    std::vector<float> x(n);
    float lp = 0.0f;
    for (float& v : x) {
        lp += 0.5f * (rng.bipolar() - lp);
        v = 0.3f * lp;
    }
    return x;
}

template <class Fx, class P>
void sweepNoise(const std::filesystem::path& path, const P& params)
{
    Fx fx;
    fx.prepare(kSampleRate, 256);
    fx.setParams(params);
    fx.reset();
    StereoBuffer b{noise(8 * kSampleRate), {}};
    b.right = b.left;
    for (std::size_t s = 0; s < b.left.size(); s += 256)
        fx.process({b.left.data() + s, b.right.data() + s, static_cast<int>(std::min<std::size_t>(256, b.left.size() - s))});
    writeStereo(path, b);
}

template <class Fx, class P>
void impulseResponse(const std::filesystem::path& path, const P& params)
{
    Fx fx;
    fx.prepare(kSampleRate, 256);
    fx.setParams(params);
    fx.reset();
    StereoBuffer b{std::vector<float>(16384, 0.0f), std::vector<float>(16384, 0.0f)};
    b.left[0] = b.right[0] = 1.0f;
    fx.process({b.left.data(), b.right.data(), 16384});
    writeStereo(path, b);
}

/// Prediction 2. One chorus-like voice by hand: d(n) = 10 ms + 1.5 ms·(1 + sin(2π·0.6·n/fs)), a 5 kHz sine,
/// read with each interpolation; the exact answer is sin(2π·f·(n − d(n))/fs).
template <class Interp>
void interpolationError(const char* name)
{
    const double f = 5000.0, fs = kSampleRate;
    dsp::DelayLine<Interp> line;
    line.prepare(2048);
    double err2 = 0.0, sig2 = 0.0;
    double lo = 1e9, hi = 0.0;
    const int n = 4 * kSampleRate;
    for (int i = 0; i < n; ++i) {
        line.push(static_cast<float>(std::sin(2.0 * std::numbers::pi * f * i / fs)));
        const double d = 0.001 * fs * (10.0 + 1.5 * (1.0 + std::sin(2.0 * std::numbers::pi * 0.6 * i / fs)));
        const float y = line.read(d);
        const double ideal = std::sin(2.0 * std::numbers::pi * f * (i - d) / fs);
        if (i < kSampleRate / 2)
            continue; // let it settle
        err2 += (y - ideal) * (y - ideal);
        sig2 += ideal * ideal;
        // Level: the gain at the sine's crests (|ideal| > 0.98, where a small timing error barely changes the
        // value), so what's left is the interpolation's amplitude error. Min and max over the run.
        if (std::abs(ideal) > 0.98) {
            const double gain = static_cast<double>(y) / ideal;
            lo = std::min(lo, gain);
            hi = std::max(hi, gain);
        }
    }
    std::printf("prediction 2, %-6s interpolation, 5 kHz under a chorus sweep: level ripple %.2f dB, error %.1f dB re tone\n",
                name, 20.0 * std::log10(hi / lo), 10.0 * std::log10(err2 / sig2));
}

/// CPU: ns per stereo sample for one effect, 10 s of noise in 256-sample blocks.
template <class Fx, class P>
void cost(const char* name, const P& params)
{
    Fx fx;
    fx.prepare(kSampleRate, 256);
    fx.setParams(params);
    fx.reset();
    std::vector<float> L = noise(10 * kSampleRate), R = L;
    const auto t0 = std::chrono::steady_clock::now();
    for (std::size_t s = 0; s + 256 <= L.size(); s += 256)
        fx.process({L.data() + s, R.data() + s, 256});
    const double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count();
    std::printf("CPU %-28s %6.1f ns per stereo sample (%.2f %% of one core at 48 kHz)\n", name,
                ns / static_cast<double>(L.size()), ns / static_cast<double>(L.size()) * 48000.0 * 1e-7);
}

} // namespace

void renderL5_4(const std::filesystem::path& outDir)
{
    cost<fx::Chorus>("chorus, 2 voices (cubic)", fx::ChorusParams{.enabled = true});
    cost<fx::Chorus>("chorus, 4 voices (cubic)", fx::ChorusParams{.enabled = true, .voices = 4});
    cost<fx::ChorusT<dsp::LinearInterp>>("chorus, 4 voices (linear)", fx::ChorusParams{.enabled = true, .voices = 4});
    cost<fx::Flanger>("flanger", fx::FlangerParams{.enabled = true});
    cost<fx::Phaser>("phaser (12 stages run)", fx::PhaserParams{.enabled = true});
    epiano(outDir);
    sweepNoise<fx::Flanger>(outDir / "l5.4_flanger_noise.wav",
                            fx::FlangerParams{.enabled = true, .rateHz = 0.15f, .delayMs = 0.3f, .depthMs = 4.0f,
                                              .feedbackPct = 70.0f, .mixPct = 50.0f});
    sweepNoise<fx::Phaser>(outDir / "l5.4_phaser_noise.wav",
                           fx::PhaserParams{.enabled = true, .rateHz = 0.15f, .centerHz = 800.0f, .depthOct = 2.5f,
                                            .feedbackPct = 50.0f, .stages = 6, .mixPct = 50.0f});
    impulseResponse<fx::Flanger>(outDir / "l5.4_flanger_ir.wav",
                                 fx::FlangerParams{.enabled = true, .rateHz = 0.1f, .delayMs = 1.0f, .depthMs = 0.0f,
                                                   .feedbackPct = 0.0f, .mixPct = 50.0f}); // fb 0: true notches
    impulseResponse<fx::Phaser>(outDir / "l5.4_phaser_ir.wav",
                                fx::PhaserParams{.enabled = true, .rateHz = 0.1f, .centerHz = 1000.0f, .depthOct = 0.0f,
                                                 .feedbackPct = 0.0f, .stages = 6, .mixPct = 50.0f});
    interpolationError<dsp::LinearInterp>("linear");
    interpolationError<dsp::CubicInterp>("cubic");
}

} // namespace fss::render
