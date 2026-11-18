// L1.11 scenarios: LFOs and the hard-wired modulation routes.
//
//   l1.11_shapes.wav                 every LFO shape at 2 Hz, 1.5 s each (sine, triangle, saw up, saw down,
//                                    square, S&H, smooth random): plot it with plot_wave.py
//   l1.11_vibrato_delay.wav          a held lead note: vibrato ±0.35 st at 5.5 Hz, 350 ms delay + 600 ms fade
//   l1.11_wobble_bass.wav            the "Wobble bass" preset playing the L1.7 bass line (LFO 2 → cutoff)
//   l1.11_pad.wav                    the "FM pad" preset, a 7 s chord (LFO 2 smooth random → index and cutoff)
//   l1.11_chord_{keysync,free}.wav   a strummed chord with ±0.5 st vibrato, LFO 1 key-synced vs free-running
#include "engine_render.h"
#include "render_util.h"
#include "scenarios.h"
#include "score.h"

#include <fss/dsp/lfo.h>
#include <fss/util/math.h>
#include <fss/synth/presets.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace fss::render {

namespace {

/// Instantaneous frequency (Hz) from upward zero crossings, as (time s, Hz) pairs.
std::vector<std::pair<double, double>> pitchTrack(const std::vector<float>& x)
{
    std::vector<std::pair<double, double>> out;
    double last = -1.0;
    for (std::size_t n = 1; n < x.size(); ++n)
        if (x[n - 1] < 0.0f && x[n] >= 0.0f) {
            const double t = static_cast<double>(n - 1) + x[n - 1] / (x[n - 1] - x[n]);
            if (last > 0.0)
                out.emplace_back(t / kSampleRate, kSampleRate / (t - last));
            last = t;
        }
    return out;
}

} // namespace

void renderL1_11(const std::filesystem::path& dir)
{
    // 1. The shapes, as audio-rate data (for plotting, not listening).
    {
        std::vector<float> shapes;
        for (int s = 0; s < dsp::kNumLfoShapes; ++s) {
            dsp::Lfo l;
            l.prepare(kSampleRate);
            l.setRate(2.0);
            l.setShape(static_cast<dsp::LfoShape>(s));
            l.setDelayFade(0.0f, 0.0f);
            l.restart(0, 1234u + static_cast<std::uint32_t>(s));
            for (int n = 0; n < kSampleRate * 3 / 2; ++n)
                shapes.push_back(0.9f * l.tick());
        }
        renderMono(dir / "l1.11_shapes.wav", shapes);
    }

    // 2. Vibrato with delay and fade-in on a sine-ish lead (a 1:1 FM pair, low index), one held note.
    {
        synth::Patch lead;
        lead.algorithm = synth::kAlgorithms[0];
        lead.ops[0].level = lead.ops[1].level = 0.0f;
        lead.ops[2] = {.level = 0.6f, .env = {.attackMs = 40.0f, .decayMs = 500.0f, .sustain = 0.8f, .releaseMs = 300.0f}};
        lead.ops[3] = {.level = 1.0f, .env = {.attackMs = 30.0f, .decayMs = 500.0f, .sustain = 0.9f, .releaseMs = 300.0f}};
        lead.lfo1 = {.rateHz = 5.5f, .shape = dsp::LfoShape::Sine, .delayMs = 350.0f, .fadeMs = 600.0f, .keySync = true};
        lead.routes.vibratoSemitones = 0.35f;
        lead.outputDb = util::gainToDb(0.4f);
        const auto buf = renderWithEngine(lead, {{0, 69, 100, 3000}}, {.tailMs = 400.0});
        writeStereo(dir / "l1.11_vibrato_delay.wav", buf);
        // Measured depth (peak-to-peak / 2, in cents) in windows: should be 0 until 0.35 s, then grow to 35.
        const auto track = pitchTrack(buf.left);
        std::printf("  vibrato depth (cents, half peak-to-peak) by window:");
        for (double w0 : {0.05, 0.25, 0.45, 0.65, 0.85, 1.05, 1.5, 2.5}) {
            double lo = 1e9, hi = 0;
            for (const auto& [t, f] : track)
                if (t >= w0 && t < w0 + 0.2) {
                    lo = std::min(lo, f);
                    hi = std::max(hi, f);
                }
            std::printf(" %.2fs:%.1f", w0, 0.5 * 1200.0 * std::log2(hi / lo));
        }
        std::printf("\n");
    }

    // 3. Wobble bass, 4. evolving pad.
    writeStereo(dir / "l1.11_wobble_bass.wav",
                renderWithEngine(synth::presets::wobbleBass(),
                                 loadScore(std::filesystem::path(FSS_SCORES_DIR) / "l1_7_bass.txt"), {.tailMs = 800.0}));
    writeStereo(dir / "l1.11_pad.wav",
                renderWithEngine(synth::presets::evolvingPad(),
                                 {{0, 57, 90, 7000}, {0, 60, 90, 7000}, {0, 64, 90, 7000}, {0, 67, 90, 7000}},
                                 {.tailMs = 2500.0}));

    // 5. Key sync vs free running on a strummed chord (notes 120 ms apart).
    synth::Patch chord = synth::presets::evolvingPad();
    chord.routes = {.vibratoSemitones = 0.5f};
    chord.lfo1.delayMs = chord.lfo1.fadeMs = 0.0f;
    chord.lfo1.rateHz = 5.0f;
    const std::vector<ScoreNote> strum = {{0, 57, 90, 4000}, {120, 60, 90, 3880}, {240, 64, 90, 3760}, {360, 67, 90, 3640}};
    for (bool sync : {true, false}) {
        chord.lfo1.keySync = sync;
        writeStereo(dir / (sync ? "l1.11_chord_keysync.wav" : "l1.11_chord_free.wav"),
                    renderWithEngine(chord, strum, {.tailMs = 1500.0}));
    }
}

} // namespace fss::render
