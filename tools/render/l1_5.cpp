// L1.5 scenarios: the 4-operator FM voice.
//
//   l1.5_epiano.wav    algorithm 5 (two stacks): tine stack A(×14, fast decay) > B, body stack C > D. A three-note
//                      arpeggio, each note its own FmVoice summed by hand (real polyphony arrives in L1.7)
//   l1.5_bass.wav      algorithm 1 with only C > D active and feedback on C: a growly FM bass line
//   l1.5_organ.wav     algorithm 8 (all carriers) at ratios 0.5, 1, 2, 3: drawbar-style additive organ
//   l1.5_fb_{05,10,15}.wav   one operator with self-feedback 0.5, 1.0, 1.5 (prediction check)
//   l1.5_fb_sweep.wav  feedback 0 → 4 over 8 s: sine → saw-like → noise
#include "render_util.h"
#include "scenarios.h"

#include <fss/synth/fm_voice.h>
#include <fss/util/math.h>

#include <array>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace fss::render {

using synth::Algorithm;

namespace {

struct OpSetup {
    double ratio;          // operator frequency = note frequency × ratio
    float level;           // carrier: amplitude; modulator: index (radians)
    dsp::AdsrParams env;
};

struct Note {
    double startSeconds, gateSeconds;
    float midiNote;
};

/// Render notes (each on its own FmVoice, summed) with one algorithm and one set of operator settings.
/// `algAt(t)` lets a render change the algorithm over time (for the feedback sweep).
std::vector<float> renderNotes(const Algorithm& alg, const std::array<OpSetup, synth::kNumOps>& ops,
                               const std::vector<Note>& notes, double totalSeconds, float gain,
                               const std::function<void(Algorithm&, double)>& modulate = nullptr)
{
    const auto n = static_cast<std::size_t>(std::lround(totalSeconds * kSampleRate));
    std::vector<float> out(n, 0.0f);
    for (const Note& note : notes) {
        synth::FmVoice v;
        v.prepare(kSampleRate);
        const double hz = util::midiToHz(note.midiNote);
        for (int i = 0; i < synth::kNumOps; ++i) {
            const OpSetup& s = ops[static_cast<std::size_t>(i)];
            v.op(i).setEnvelope(s.env);
            v.op(i).setFrequency(hz * s.ratio);
            v.op(i).setLevel(s.level);
        }
        Algorithm a = alg;
        const auto start = static_cast<std::size_t>(std::lround(note.startSeconds * kSampleRate));
        const auto off = start + static_cast<std::size_t>(std::lround(note.gateSeconds * kSampleRate));
        v.noteOn();
        for (std::size_t i = start; i < n; ++i) {
            if (i == off)
                v.noteOff();
            if (modulate)
                modulate(a, static_cast<double>(i) / kSampleRate);
            out[i] += gain * v.tick(a);
        }
    }
    return out;
}

constexpr dsp::AdsrParams kOff{.attackMs = 1.0f, .decayMs = 1.0f, .sustain = 0.0f, .releaseMs = 1.0f};
constexpr dsp::AdsrParams kHold{.attackMs = 1.0f, .decayMs = 1.0f, .sustain = 1.0f, .releaseMs = 30.0f};

} // namespace

void renderL1_5(const std::filesystem::path& dir)
{
    using synth::kAlgorithms;

    // E-piano: the classic "tine + body" layout. A at ×14 gives the metallic tine "ping" (an inharmonic-
    // sounding cluster high above the note) and decays in ~0.15 s; C at ×1 with a slower decay gives the
    // warm, slightly bell-like body.
    {
        const std::array<OpSetup, 4> ops = {{
            {14.0, 1.6f, {.attackMs = 1.0f, .decayMs = 180.0f, .sustain = 0.0f, .releaseMs = 100.0f}},  // A tine mod
            {1.0, 0.6f, {.attackMs = 1.0f, .decayMs = 1200.0f, .sustain = 0.0f, .releaseMs = 300.0f}},  // B tine car
            {1.0, 1.1f, {.attackMs = 1.0f, .decayMs = 1500.0f, .sustain = 0.25f, .releaseMs = 300.0f}}, // C body mod
            {1.0, 1.0f, {.attackMs = 2.0f, .decayMs = 3000.0f, .sustain = 0.3f, .releaseMs = 400.0f}},  // D body car
        }};
        const std::vector<Note> notes = {{0.0, 1.6, 57}, {0.4, 1.2, 60}, {0.8, 0.8, 64}, {1.6, 1.4, 69}};
        renderMono(dir / "l1.5_epiano.wav", renderNotes(kAlgorithms[4], ops, notes, 3.5, 0.35f));
    }

    // FM bass: only the C > D pair of the stack is used (A and B silent), C has self-feedback for grit, and
    // C's index decays fast so each note has a bright "thump" then a round tail.
    {
        synth::Algorithm alg = kAlgorithms[0];
        alg.mod[2][2] = 0.9f; // C feeds back into itself
        const std::array<OpSetup, 4> ops = {{
            {1.0, 0.0f, kOff},
            {1.0, 0.0f, kOff},
            {1.0, 2.2f, {.attackMs = 1.0f, .decayMs = 350.0f, .sustain = 0.25f, .releaseMs = 80.0f}},
            {1.0, 1.0f, {.attackMs = 2.0f, .decayMs = 800.0f, .sustain = 0.7f, .releaseMs = 60.0f}},
        }};
        const std::vector<Note> notes = {{0.0, 0.2, 33}, {0.25, 0.2, 33}, {0.5, 0.2, 45},
                                         {0.75, 0.2, 33}, {1.0, 0.4, 36}, {1.5, 0.4, 31}};
        renderMono(dir / "l1.5_bass.wav", renderNotes(alg, ops, notes, 2.2, 0.6f));
    }

    // Organ: four carriers at 16', 8', 4' and 2 2/3' (ratios 0.5, 1, 2, 3), like drawbars; quick on/off.
    {
        const dsp::AdsrParams organ{.attackMs = 5.0f, .decayMs = 1.0f, .sustain = 1.0f, .releaseMs = 15.0f};
        const std::array<OpSetup, 4> ops = {{{0.5, 0.8f, organ}, {1.0, 1.0f, organ}, {2.0, 0.6f, organ}, {3.0, 0.5f, organ}}};
        const std::vector<Note> notes = {{0.0, 1.5, 60}, {0.0, 1.5, 64}, {0.0, 1.5, 67}};
        renderMono(dir / "l1.5_organ.wav", renderNotes(kAlgorithms[7], ops, notes, 1.8, 0.3f));
    }

    // Feedback on a single operator (A is the only carrier): level 1, so the feedback amount is β directly.
    synth::Algorithm solo{};
    solo.name = "A alone with self-feedback";
    solo.carrier[0] = 0.5f;
    const std::array<OpSetup, 4> soloOps = {{{1.0, 1.0f, kHold}, {1.0, 0.0f, kOff}, {1.0, 0.0f, kOff}, {1.0, 0.0f, kOff}}};
    for (float beta : {0.5f, 1.0f, 1.5f}) {
        synth::Algorithm a = solo;
        a.mod[0][0] = beta;
        const std::string name = "l1.5_fb_" + std::to_string(static_cast<int>(std::lround(beta * 10))) + ".wav";
        renderMono(dir / name, renderNotes(a, soloOps, {{0.0, 1.0, 45}}, 1.0, 1.0f)); // A2 = 110 Hz
    }
    renderMono(dir / "l1.5_fb_sweep.wav",
               renderNotes(solo, soloOps, {{0.0, 8.0, 45}}, 8.0, 1.0f,
                           [](synth::Algorithm& a, double t) { a.mod[0][0] = static_cast<float>(t / 2.0); })); // 0 → 4
}

} // namespace fss::render
