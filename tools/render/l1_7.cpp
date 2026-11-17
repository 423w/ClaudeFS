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

#include <cstdio>

namespace fss::render {

namespace {

using util::Ratio;

synth::Patch epiano()
{
    synth::Patch p;
    p.algorithm = synth::kAlgorithms[4]; // two stacks: tine (A>B) + body (C>D)
    p.ops[0] = {.ratio = Ratio{14.0}, .level = 1.6f, .velSens = 0.9f, .keyLevelDbPerOct = 3.0f, .keyRateScale = 0.5f,
                .env = {.attackMs = 1.0f, .decayMs = 180.0f, .sustain = 0.0f, .releaseMs = 100.0f}};
    p.ops[1] = {.level = 0.6f, .velSens = 0.5f, .keyRateScale = 0.4f,
                .env = {.attackMs = 1.0f, .decayMs = 1200.0f, .sustain = 0.0f, .releaseMs = 300.0f}};
    p.ops[2] = {.level = 1.1f, .velSens = 0.7f, .keyLevelDbPerOct = 3.0f, .keyRateScale = 0.3f,
                .env = {.attackMs = 1.0f, .decayMs = 1500.0f, .sustain = 0.25f, .releaseMs = 300.0f}};
    p.ops[3] = {.level = 1.0f, .velSens = 0.4f, .keyRateScale = 0.3f,
                .env = {.attackMs = 2.0f, .decayMs = 3000.0f, .sustain = 0.3f, .releaseMs = 400.0f}};
    p.outputGain = 0.22f;
    return p;
}

synth::Patch fmBass()
{
    synth::Patch p;
    p.algorithm = synth::kAlgorithms[0];
    p.algorithm.mod[2][2] = 0.9f; // C self-feedback: grit
    p.ops[0].level = p.ops[1].level = 0.0f;
    p.ops[2] = {.level = 2.2f, .velSens = 0.6f, .env = {.attackMs = 1.0f, .decayMs = 350.0f, .sustain = 0.25f, .releaseMs = 80.0f}};
    p.ops[3] = {.level = 1.0f, .velSens = 0.3f, .env = {.attackMs = 2.0f, .decayMs = 800.0f, .sustain = 0.7f, .releaseMs = 60.0f}};
    p.outputGain = 0.45f;
    return p;
}

synth::Patch sinePad()
{
    synth::Patch p;
    p.algorithm = synth::kAlgorithms[7]; // all carriers; only A has a level: a pure sine
    p.algorithm.carrier = {1.0f, 0.0f, 0.0f, 0.0f};
    p.ops[0] = {.level = 1.0f, .env = {.attackMs = 5.0f, .decayMs = 1.0f, .sustain = 1.0f, .releaseMs = 1500.0f}};
    for (int i : {1, 2, 3})
        p.ops[static_cast<std::size_t>(i)].level = 0.0f;
    p.outputGain = 0.12f;
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
    StereoBuffer piece = renderWithEngine(epiano(), keysScore, {.tailMs = 2000.0}, &keyStats);
    mixInto(piece, renderWithEngine(fmBass(), loadScore(scores / "l1_7_bass.txt"), {.tailMs = 2000.0}));
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
