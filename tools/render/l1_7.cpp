// L1.7 scenarios: polyphony through fss::Engine. Milestone M2.
//
//   l1.7_piece.wav           8 bars, 96 BPM, A minor: e-piano chords + melody (one Engine, 16 voices)
//                            plus an FM bass line (a second Engine). Rendered in 256-sample blocks.
//   l1.7_stress_fade.wav     48 overlapping long notes, 25 ms apart, into 8 voices: constant stealing, 5 ms fades
//   l1.7_stress_cut.wav      the same with a 0 ms "fade" (instant cut): the clicks the fade prevents
// The stress patch is a pure sine pad below 1.1 kHz, so energy above 3 kHz can only be clicks.
#include "engine_render.h"
#include "render_util.h"
#include "scenarios.h"

#include <fss/synth/presets.h>
#include <fss/util/math.h>

#include <cstdio>

namespace fss::render {

namespace {

synth::Patch sinePad()
{
    synth::Patch p;
    p.algorithm = synth::kAlgorithms[7]; // all carriers; only A has a level: a pure sine
    p.algorithm.carrier = {1.0f, 0.0f, 0.0f, 0.0f};
    p.ops[0] = {.level = 1.0f, .env = {.attackMs = 5.0f, .decayMs = 1.0f, .sustain = 1.0f, .releaseMs = 1500.0f}};
    for (int i : {1, 2, 3})
        p.ops[static_cast<std::size_t>(i)].level = 0.0f;
    p.outputDb = util::gainToDb(0.12f);
    return p;
}

} // namespace

void renderL1_7(const std::filesystem::path& dir)
{
    const std::filesystem::path scores = FSS_SCORES_DIR;

    // The piece. Chords and melody share one Engine (so polyphony is really exercised); bass has its own.
    auto keysScore = loadScore(scores / "l1_7_keys.txt");
    for (const ScoreNote& n : loadScore(scores / "l1_7_melody.txt"))
        keysScore.push_back(n);
    EngineRenderStats keyStats{};
    StereoBuffer piece = renderWithEngine(synth::presets::epiano(), keysScore, {.tailMs = 2000.0}, &keyStats);
    mixInto(piece, renderWithEngine(synth::presets::fmBass(), loadScore(scores / "l1_7_bass.txt"), {.tailMs = 2000.0}));
    writeStereo(dir / "l1.7_piece.wav", piece);
    std::printf("  keys engine: max %d busy voices, %llu steals\n", keyStats.maxBusyVoices,
                static_cast<unsigned long long>(keyStats.steals));

    // Stealing stress.
    const auto stress = loadScore(scores / "l1_7_stress.txt");
    for (float fade : {5.0f, 0.0f}) {
        EngineRenderStats st{};
        const auto buf = renderWithEngine(sinePad(), stress, {.polyphony = 8, .stealFadeMs = fade}, &st);
        writeStereo(dir / (fade > 0.0f ? "l1.7_stress_fade.wav" : "l1.7_stress_cut.wav"), buf);
        std::printf("  stress (fade %.0f ms): %llu steals, max %d busy voices\n", static_cast<double>(fade),
                    static_cast<unsigned long long>(st.steals), st.maxBusyVoices);
    }
}

} // namespace fss::render
