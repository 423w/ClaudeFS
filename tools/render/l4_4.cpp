// L4.4 scenarios: unison.
//
//   l4.4_pad_unison1.wav / _unison4.wav / _unison8.wav   the FM pad's chords with 1, 4 and 8 copies (18 cents, 100 %)
//   l4.4_count_change.wav     one held chord while the unison count goes 1 → 4 → 8 → 2 → 1 (no clicks)
//   (console)  mono compatibility of "Wide pad" at widths 0 / 50 / 100 %: per-channel, mono-sum, side levels
//   (console)  CPU: 16 voices of the FM pad at unison 1, 2, 4, 8 (median µs per 256-sample block)
#include "engine_render.h"
#include "render_util.h"
#include "scenarios.h"

#include <fss/synth/params.h>
#include <fss/synth/presets.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

namespace fss::render {

namespace {

std::vector<ScoreNote> chords()
{
    std::vector<ScoreNote> s;
    const std::uint8_t c[2][4] = {{48, 55, 64, 71}, {45, 52, 60, 67}};
    for (int k = 0; k < 2; ++k)
        for (std::uint8_t key : c[k])
            s.push_back({.startMs = k * 4000.0, .key = key, .velocity = 85, .durationMs = 3600.0});
    return s;
}

double rmsDb(const std::vector<float>& x, std::size_t from, std::size_t to)
{
    double s = 0.0;
    for (std::size_t i = from; i < to; ++i)
        s += static_cast<double>(x[i]) * x[i];
    return 10.0 * std::log10(std::max(s / static_cast<double>(to - from), 1e-24));
}

void monoCheck()
{
    std::printf("mono compatibility of \"Wide pad\" (4 copies, 18 cents), seconds 1-3 of a held chord:\n");
    std::printf("  width   per channel   mono sum   side     mono - channel\n");
    for (float width : {0.0f, 50.0f, 100.0f}) {
        synth::Patch p = synth::presets::widePad();
        p.unison.widthPct = width;
        const StereoBuffer b = renderWithEngine(p, chords(), {.tailMs = 500.0});
        std::vector<float> mono(b.left.size()), side(b.left.size());
        for (std::size_t i = 0; i < mono.size(); ++i) {
            mono[i] = 0.5f * (b.left[i] + b.right[i]);
            side[i] = 0.5f * (b.left[i] - b.right[i]);
        }
        const std::size_t a = static_cast<std::size_t>(kSampleRate), z = 3 * static_cast<std::size_t>(kSampleRate);
        const double ch = 0.5 * (rmsDb(b.left, a, z) + rmsDb(b.right, a, z));
        const double mo = rmsDb(mono, a, z);
        std::printf("  %4.0f %%   %8.2f dB   %7.2f dB  %7.2f dB   %+.2f dB\n", width, ch, mo, rmsDb(side, a, z), mo - ch);
    }
}

void cpu()
{
    std::printf("CPU, 16 voices of the FM pad, 256-sample blocks (budget 5333 us):\n");
    for (int n : {1, 2, 4, 8}) {
        synth::Patch p = synth::presets::evolvingPad();
        p.unison = {.voices = static_cast<std::uint8_t>(n), .detuneCents = 18.0f, .widthPct = 100.0f};
        Engine eng;
        eng.prepare(kSampleRate, 256);
        eng.setPatch(p);
        std::vector<float> L(256), R(256);
        std::vector<Event> on;
        for (int k = 0; k < 16; ++k)
            on.push_back(Event::noteOn(0, static_cast<std::uint8_t>(40 + 2 * k), 0.8f));
        eng.process(on, L.data(), R.data(), 256);
        for (int w = 0; w < 200; ++w)
            eng.process({}, L.data(), R.data(), 256); // past the attacks
        std::vector<double> t;
        for (int b = 0; b < 400; ++b) {
            const auto t0 = std::chrono::steady_clock::now();
            eng.process({}, L.data(), R.data(), 256);
            t.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count());
        }
        std::sort(t.begin(), t.end());
        std::printf("  unison %d: %7.1f us per block (%.1f %% of one core), %d busy voices\n", n, t[t.size() / 2],
                    100.0 * t[t.size() / 2] / 5333.3, eng.busyVoices());
    }
}

} // namespace

void renderL4_4(const std::filesystem::path& outDir)
{
    for (int n : {1, 4, 8}) {
        synth::Patch p = synth::presets::evolvingPad();
        p.unison = {.voices = static_cast<std::uint8_t>(n), .detuneCents = 18.0f, .widthPct = 100.0f};
        char name[64];
        std::snprintf(name, sizeof name, "l4.4_pad_unison%d.wav", n);
        writeStereo(outDir / name, renderWithEngine(p, chords(), {.tailMs = 2000.0}));
    }
    // The count changing under a held chord: the score holds the chord; the changes come as parameter events.
    {
        synth::Patch p = synth::presets::evolvingPad();
        Engine eng;
        eng.prepare(kSampleRate, 256);
        eng.setPatch(p);
        const std::size_t total = 10 * static_cast<std::size_t>(kSampleRate);
        StereoBuffer b{std::vector<float>(total), std::vector<float>(total)};
        const float counts[] = {4, 8, 2, 1};
        for (std::size_t s = 0; s < total; s += 256) {
            std::vector<Event> ev;
            if (s == 0)
                for (std::uint8_t k : {std::uint8_t{48}, std::uint8_t{55}, std::uint8_t{64}})
                    ev.push_back(Event::noteOn(0, k, 0.8f));
            for (int c = 0; c < 4; ++c)
                if (s == static_cast<std::size_t>((c + 1) * 2 * kSampleRate) / 256 * 256)
                    ev.push_back(Event::paramChange(0, synth::param_id::kUnisonVoices, counts[c]));
            eng.process(ev, b.left.data() + s, b.right.data() + s, 256);
        }
        writeStereo(outDir / "l4.4_count_change.wav", b);
    }
    monoCheck();
    cpu();
}

} // namespace fss::render
