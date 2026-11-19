// L4.1 scenarios: the modulation matrix.
//
//   l4.1_fm_pad.wav            the "FM pad" preset (its L1.11 routes, now fixed matrix slots): three long chords
//   l4.1_evolving_matrix.wav   the same chords on "Evolving matrix" (five matrix routes: mod env → index,
//                              LFO 1 → op A pitch, random → cutoff, LFO 2 → amp, key × mod env → op C level)
//   l4.1_polymod.wav           two held notes; the host (as CLAP PARAM_MOD would) sweeps the cutoff of the
//                              upper note only, down and back up over 4 s: the lower note never changes
//   (console)                  brightness (spectral centroid) per second of both pad renders: how much they move
#include "engine_render.h"
#include "render_util.h"
#include "scenarios.h"

#include <fss/synth/params.h>
#include <fss/synth/presets.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdio>
#include <numbers>
#include <vector>

namespace fss::render {

namespace {

std::vector<ScoreNote> longChords()
{
    std::vector<ScoreNote> s;
    const std::uint8_t chords[3][4] = {{48, 55, 64, 71}, {45, 52, 60, 67}, {41, 48, 57, 64}};
    for (int c = 0; c < 3; ++c)
        for (std::uint8_t k : chords[c])
            s.push_back({.startMs = c * 5000.0, .key = k, .velocity = 85, .durationMs = 4600.0});
    return s;
}

/// Spectral centroid (Hz) of 1 s windows of the mono sum: a rough "how bright" over time.
std::vector<double> centroids(const StereoBuffer& b)
{
    std::vector<double> out;
    const std::size_t win = 4096;
    for (std::size_t start = 0; start + win <= b.left.size(); start += static_cast<std::size_t>(kSampleRate)) {
        double num = 0.0, den = 0.0;
        for (std::size_t k = 1; k < win / 4; k += 2) { // every other bin up to 12 kHz is plenty for a centroid
            std::complex<double> x{};
            for (std::size_t n = 0; n < win; ++n) {
                const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * n / win);
                x += w * 0.5 * (b.left[start + n] + b.right[start + n]) *
                     std::polar(1.0, -2.0 * std::numbers::pi * static_cast<double>(k * n) / win);
            }
            const double f = static_cast<double>(k) * kSampleRate / win, m = std::norm(x); // power-weighted
            num += f * m;
            den += m;
        }
        out.push_back(den > 0.0 ? num / den : 0.0);
    }
    return out;
}

void polymod(const std::filesystem::path& out)
{
    synth::Patch p = synth::presets::evolvingPad();
    p.routes = {}; // no LFO wobble: only the host's modulation moves
    p.filter = {.mode = synth::FilterMode::LowPass, .cutoffHz = 3000.0f, .q = 2.0f};
    Engine eng;
    eng.prepare(kSampleRate, 256);
    eng.setPatch(p);
    const std::size_t total = 6 * kSampleRate;
    StereoBuffer b{std::vector<float>(total), std::vector<float>(total)};
    for (std::size_t s = 0; s < total; s += 256) {
        std::vector<Event> ev;
        if (s == 0) {
            ev.push_back(Event::noteOn(0, 48, 0.8f, 1));
            ev.push_back(Event::noteOn(0, 67, 0.8f, 2));
        }
        // From 1 s to 5 s, a triangle from 0 down to −0.35 knob units (−3.5 octaves) and back, on note id 2.
        const double t = static_cast<double>(s) / kSampleRate;
        if (t >= 1.0 && t <= 5.0) {
            const double x = 1.0 - std::abs((t - 1.0) / 2.0 - 1.0);
            ev.push_back(Event::paramMod(0, synth::param_id::kCutoff, static_cast<float>(-0.35 * x), 2));
        }
        eng.process(ev, b.left.data() + s, b.right.data() + s, 256);
    }
    writeStereo(out / "l4.1_polymod.wav", b);
}

} // namespace

void renderL4_1(const std::filesystem::path& outDir)
{
    const EngineRenderOptions opt{.tailMs = 3000.0};
    const StereoBuffer pad = renderWithEngine(synth::presets::evolvingPad(), longChords(), opt);
    const StereoBuffer evo = renderWithEngine(synth::presets::evolvingMatrix(), longChords(), opt);
    writeStereo(outDir / "l4.1_fm_pad.wav", pad);
    writeStereo(outDir / "l4.1_evolving_matrix.wav", evo);
    const auto a = centroids(pad), b = centroids(evo);
    std::printf("brightness (spectral centroid, Hz) per second:\n  second    FM pad   Evolving matrix\n");
    for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i)
        std::printf("  %6zu  %8.0f  %8.0f\n", i, a[i], b[i]);
    polymod(outDir);
}

} // namespace fss::render
