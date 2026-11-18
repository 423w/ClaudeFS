// L1.6 scenarios: the note (pitch, velocity, key scaling), driven by score files.
//
//   l1.6_arpeggio_flat.wav     C2..C6..C2 arpeggio, key scaling OFF: high notes get as bright (and as
//                              long) as low ones, which reads as harsh and ringing up high
//   l1.6_arpeggio_scaled.wav   the same score, key scaling ON: modulator −6 dB/octave above C4, envelopes
//                              faster up high
//   l1.6_velocity.wav          one note at velocities 20, 50, 80, 110, 127: velocity on the modulator =
//                              brightness, a little on the carrier = loudness
#include "render_util.h"
#include "scenarios.h"
#include "score.h"

#include <fss/synth/fm_voice.h>
#include <fss/util/math.h>

#include <cmath>
#include <vector>

namespace fss::render {

namespace {

using namespace util::literals;

/// Every note gets its own FmVoice, summed. (Real polyphony with a voice pool is L1.7.)
std::vector<float> renderScore(const synth::Patch& patch, const std::vector<ScoreNote>& score, double tailMs)
{
    const auto n = static_cast<std::size_t>(std::lround((scoreEndMs(score) + tailMs) * 0.001 * kSampleRate));
    std::vector<float> out(n, 0.0f);
    for (const ScoreNote& note : score) {
        synth::FmVoice v;
        v.prepare(kSampleRate);
        v.start({.key = note.key, .velocity = static_cast<float>(note.velocity) / 127.0f, .bend = 0.0f}, patch);
        const auto start = static_cast<std::size_t>(std::lround(note.startMs * 0.001 * kSampleRate));
        const auto off = start + static_cast<std::size_t>(std::lround(note.durationMs * 0.001 * kSampleRate));
        for (std::size_t i = start; i < n; ++i) {
            if (i == off)
                v.noteOff();
            out[i] += util::dbToGain(patch.outputDb) * v.tick(patch.algorithm);
        }
    }
    return out;
}

/// A bright 2-op keys patch on the C > D pair of the stack (A and B silent).
synth::Patch keysPatch(bool keyScaling)
{
    synth::Patch p;
    p.algorithm = synth::kAlgorithms[0];
    for (int i : {0, 1})
        p.ops[static_cast<std::size_t>(i)].level = 0.0f;
    auto& mod = p.ops[2];
    mod.ratio = util::Ratio{1.0};
    mod.level = 3.0f; // index 3: bright
    mod.velSens = 1.0f;
    mod.env = {.attackMs = 1.0f, .decayMs = 900.0f, .sustain = 0.3f, .releaseMs = 150.0f};
    auto& car = p.ops[3];
    car.level = 1.0f;
    car.velSens = 0.3f;
    car.env = {.attackMs = 2.0f, .decayMs = 2000.0f, .sustain = 0.4f, .releaseMs = 250.0f};
    if (keyScaling) {
        mod.keyLevelDbPerOct = 6.0f; // the modulator (= brightness) drops 6 dB per octave above C4
        mod.keyRateScale = 0.5f;     // and both envelopes run faster up high (×0.71 per octave)
        car.keyRateScale = 0.5f;
    }
    p.outputDb = util::gainToDb(0.4f);
    return p;
}

} // namespace

void renderL1_6(const std::filesystem::path& dir)
{
    const std::filesystem::path scores = FSS_SCORES_DIR;
    const auto arp = loadScore(scores / "l1_6_arpeggio.txt");
    renderMono(dir / "l1.6_arpeggio_flat.wav", renderScore(keysPatch(false), arp, 400.0));
    renderMono(dir / "l1.6_arpeggio_scaled.wav", renderScore(keysPatch(true), arp, 400.0));
    renderMono(dir / "l1.6_velocity.wav", renderScore(keysPatch(false), loadScore(scores / "l1_6_velocity.txt"), 400.0));
}

} // namespace fss::render
