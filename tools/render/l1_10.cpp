// L1.10 scenarios: the TPT SVF vs the biquad under modulation, and the per-voice filter in the synth.
//
//   l1.10_torture_{svf,df1,tdf2}.wav   white noise (0.1) through a Q-10 low-pass whose cutoff a 50 Hz sine sweeps
//                                      from 100 Hz to 15 kHz, coefficients updated every sample
//   (console)                           fuzz: random cutoff (10 Hz..30 kHz) and Q (0.5..50) every sample
//   l1.10_pluck_keys.wav                the "Reso pluck" preset, C2 C3 C4 C5 C6, one per second
//   l1.10_pluck_keys_notrack.wav        the same with key tracking 0 (high notes get dull, low notes buzzy)
//   l1.10_pluck_velocity.wav            C3 at velocities 20, 50, 80, 110, 127: velocity opens the filter
//   l1.10_pluck_riff.wav                a short riff (the L1.7 bass line) through the patch
//   l1.10_cutoff_{smooth,zipper}.wav    a held note under a Q-8 low-pass whose cutoff is swept by a 60 Hz
//                                      staircase of ParamChange events (a knob), with and without smoothing
#include "engine_render.h"
#include "render_util.h"
#include "scenarios.h"
#include "score.h"

#include <fss/dsp/biquad.h>
#include <fss/dsp/svf.h>
#include <fss/synth/params.h>
#include <fss/synth/presets.h>

#include <cmath>
#include <cstdio>
#include <numbers>
#include <random>
#include <vector>

namespace fss::render {

namespace {

constexpr double kFs = kSampleRate;

struct Stats {
    double peak = 0.0, rmsDb = -300.0;
    std::size_t nonFinite = 0;
};

Stats stats(const std::vector<float>& x)
{
    Stats s;
    double sum = 0.0;
    for (float v : x) {
        if (!std::isfinite(v)) {
            ++s.nonFinite;
            continue;
        }
        s.peak = std::fmax(s.peak, std::abs(static_cast<double>(v)));
        sum += static_cast<double>(v) * v;
    }
    s.rmsDb = 10.0 * std::log10(sum / static_cast<double>(x.size()) + 1e-30);
    return s;
}

/// Run `in` through one of the three filters with per-sample cutoff/Q from `at(n, cutoff, q)`.
template <typename Fn>
std::vector<float> modulated(const std::vector<float>& in, int which, Fn at)
{
    dsp::Svf svf;
    svf.prepare(kFs);
    dsp::BiquadDf1<float> df1;
    dsp::BiquadTdf2<float> tdf2;
    std::vector<float> out(in.size());
    for (std::size_t n = 0; n < in.size(); ++n) {
        double fc = 1000.0, q = 0.7071;
        at(n, fc, q);
        if (which == 0) {
            svf.set(static_cast<float>(fc), static_cast<float>(q));
            out[n] = svf.tick(in[n]).low;
        } else {
            const auto c = dsp::designBiquad(dsp::BiquadType::LowPass, kFs, fc, q);
            if (which == 1) {
                df1.setCoeffs(c);
                out[n] = df1.tick(in[n]);
            } else {
                tdf2.setCoeffs(c);
                out[n] = tdf2.tick(in[n]);
            }
        }
    }
    return out;
}

void torture(const std::filesystem::path& dir)
{
    std::mt19937 rng(11);
    std::uniform_real_distribution<float> noise(-0.1f, 0.1f);
    std::vector<float> in(3 * kSampleRate);
    for (float& v : in)
        v = noise(rng);

    // Reference: the same noise through a *static* Q-10 LP at the sweep's geometric centre (~1.2 kHz).
    const auto ref = modulated(in, 0, [](std::size_t, double& fc, double& q) { fc = 100.0 * std::sqrt(150.0); q = 10.0; });
    std::printf("  torture (50 Hz sweep 100 Hz..15 kHz, Q 10, white noise rms %.1f dB):\n", stats(in).rmsDb);
    std::printf("    static SVF at 1.2 kHz for scale: peak %.3f, rms %.1f dB\n", stats(ref).peak, stats(ref).rmsDb);
    const char* names[] = {"svf", "df1", "tdf2"};
    for (int which = 0; which < 3; ++which) {
        auto out = modulated(in, which, [](std::size_t n, double& fc, double& q) {
            fc = 100.0 * std::pow(150.0, 0.5 + 0.5 * std::sin(2.0 * std::numbers::pi * 50.0 * static_cast<double>(n) / kFs));
            q = 10.0;
        });
        const Stats s = stats(out);
        std::printf("    %-5s peak %8.3f   rms %6.1f dB   non-finite %zu\n", names[which], s.peak, s.rmsDb, s.nonFinite);
        renderMono(dir / (std::string("l1.10_torture_") + names[which] + ".wav"), out);
    }

    // Fuzz: the cutoff and Q jump to random values on every sample (nothing musical does this; it's a stress test).
    std::printf("  fuzz (random cutoff 10 Hz..30 kHz and Q 0.5..50 every sample, 10 s of noise):\n");
    std::vector<float> longIn(10 * kSampleRate);
    for (float& v : longIn)
        v = noise(rng);
    for (int which = 0; which < 3; ++which) {
        std::mt19937 r(5);
        std::uniform_real_distribution<double> u(0.0, 1.0);
        const auto out = modulated(longIn, which, [&](std::size_t, double& fc, double& q) {
            fc = 10.0 * std::pow(3000.0, u(r));
            q = 0.5 * std::pow(100.0, u(r));
        });
        const Stats s = stats(out);
        std::printf("    %-5s peak %12.4g   non-finite %zu\n", names[which], s.peak, s.nonFinite);
    }
}

std::vector<ScoreNote> notes(std::initializer_list<ScoreNote> l) { return l; }

/// Render `seconds` of a held note while a 60 Hz "GUI" staircase moves the cutoff 150 Hz → 5 kHz → 150 Hz.
StereoBuffer cutoffKnob(bool smoothing)
{
    synth::Patch p = synth::presets::resoPluck();
    p.filter.envOctaves = 0.0f; // the knob alone moves the cutoff
    p.filter.q = 8.0f;
    p.ops[3].env.sustain = 1.0f;
    Engine eng;
    eng.setSmoothingEnabled(smoothing);
    eng.prepare(kSampleRate, 256);
    eng.setPatch(p);
    const std::size_t total = 4 * kSampleRate;
    StereoBuffer out;
    out.left.resize(total);
    out.right.resize(total);
    constexpr int kGui = kSampleRate / 60;
    const synth::ParamInfo& cutoff = *synth::findParam(synth::param_id::kCutoff);
    std::vector<Event> ev;
    for (std::size_t start = 0, frame = 0; start < total; start += kGui, ++frame) {
        ev.clear();
        if (frame == 0)
            ev.push_back(Event::noteOn(0, 36, 1.0f));
        const double x = static_cast<double>(start) / static_cast<double>(total);
        const double tri = 1.0 - std::abs(2.0 * x - 1.0);
        // A MIDI-CC-like knob: 128 steps of normalized position (the Exp skew maps them to Hz).
        const float norm = std::round(static_cast<float>(synth::toNormalized(cutoff, 150.0f) +
                                                         tri * (synth::toNormalized(cutoff, 5000.0f) -
                                                                synth::toNormalized(cutoff, 150.0f))) * 127.0f) / 127.0f;
        ev.push_back(Event::paramChange(0, cutoff.id, synth::fromNormalized(cutoff, norm)));
        const auto n = static_cast<int>(std::min<std::size_t>(kGui, total - start));
        eng.process(ev, out.left.data() + start, out.right.data() + start, n);
    }
    return out;
}

} // namespace

void renderL1_10(const std::filesystem::path& dir)
{
    torture(dir);

    const synth::Patch pluck = synth::presets::resoPluck();
    const auto keys = notes({{0, 36, 110, 700}, {1000, 48, 110, 700}, {2000, 60, 110, 700}, {3000, 72, 110, 700},
                             {4000, 84, 110, 700}});
    writeStereo(dir / "l1.10_pluck_keys.wav", renderWithEngine(pluck, keys, {.tailMs = 800.0}));
    synth::Patch noTrack = pluck;
    noTrack.filter.keyTrack = 0.0f;
    writeStereo(dir / "l1.10_pluck_keys_notrack.wav", renderWithEngine(noTrack, keys, {.tailMs = 800.0}));
    writeStereo(dir / "l1.10_pluck_velocity.wav",
                renderWithEngine(pluck,
                                 notes({{0, 48, 20, 700}, {1000, 48, 50, 700}, {2000, 48, 80, 700}, {3000, 48, 110, 700},
                                        {4000, 48, 127, 700}}),
                                 {.tailMs = 800.0}));
    writeStereo(dir / "l1.10_pluck_riff.wav",
                renderWithEngine(pluck, loadScore(std::filesystem::path(FSS_SCORES_DIR) / "l1_7_bass.txt"), {.tailMs = 800.0}));

    const StereoBuffer smooth = cutoffKnob(true), zipper = cutoffKnob(false);
    writeStereo(dir / "l1.10_cutoff_smooth.wav", smooth);
    writeStereo(dir / "l1.10_cutoff_zipper.wav", zipper);
    // Zipper shows up as broadband clicks; the filtered note itself has little energy above 8 kHz (the cutoff
    // never exceeds 5 kHz, 12 dB/oct beyond). So compare the energy above 8 kHz (a 2nd-order high-pass).
    auto above8k = [](const std::vector<float>& x) {
        dsp::Biquad hp;
        hp.setCoeffs(dsp::designBiquad(dsp::BiquadType::HighPass, kFs, 8000.0, 0.7071));
        std::vector<float> y(x.size());
        for (std::size_t n = 0; n < x.size(); ++n)
            y[n] = hp.tick(x[n]);
        return stats(y).rmsDb;
    };
    std::printf("  cutoff knob (60 Hz staircase): output rms %.1f dB; above 8 kHz: smoothed %.1f dB, unsmoothed %.1f dB\n",
                stats(smooth.left).rmsDb, above8k(smooth.left), above8k(zipper.left));
}

} // namespace fss::render
