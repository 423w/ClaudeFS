// fss/synth/presets.h: a few factory patches shared by every host (offline renders, standalone, later
// the plugin). Introduced in L2.1 when a second host needed the same sounds; values are the ones designed
// and measured in L1.5–L2.5 (see those journals), plus four from L3.4 (bell, brass, organ, marimba).
#pragma once

#include <fss/synth/patch.h>
#include <fss/util/math.h>

namespace fss::synth::presets {

/// Two-stack e-piano (algorithm 5): tine A(×14, fast decay) > B, body C > D. Velocity = brightness.
inline Patch epiano()
{
    using util::Ratio;
    Patch p;
    p.algorithm = kAlgorithms[4];
    p.algorithmNumber = 5;
    p.ops[0] = {.ratio = Ratio{14.0}, .level = 1.6f, .velSens = 0.9f, .keyLevelDbPerOct = 3.0f, .keyRateScale = 0.5f,
                .env = {.attackMs = 1.0f, .decayMs = 180.0f, .sustain = 0.0f, .releaseMs = 100.0f}};
    p.ops[1] = {.level = 0.6f, .velSens = 0.5f, .keyRateScale = 0.4f,
                .env = {.attackMs = 1.0f, .decayMs = 1200.0f, .sustain = 0.0f, .releaseMs = 300.0f}};
    p.ops[2] = {.level = 1.1f, .velSens = 0.7f, .keyLevelDbPerOct = 3.0f, .keyRateScale = 0.3f,
                .env = {.attackMs = 1.0f, .decayMs = 1500.0f, .sustain = 0.25f, .releaseMs = 300.0f}};
    p.ops[3] = {.level = 1.0f, .velSens = 0.4f, .keyRateScale = 0.3f,
                .env = {.attackMs = 2.0f, .decayMs = 3000.0f, .sustain = 0.3f, .releaseMs = 400.0f}};
    p.outputDb = util::gainToDb(0.22f);
    return p;
}

/// FM bass: only C > D of the stack, C with self-feedback and a fast-decaying index.
inline Patch fmBass()
{
    Patch p;
    p.algorithm = kAlgorithms[0];
    p.algorithm.mod[2][2] = 0.9f;
    p.ops[0].level = p.ops[1].level = 0.0f;
    p.ops[2] = {.level = 2.2f, .velSens = 0.6f, .env = {.attackMs = 1.0f, .decayMs = 350.0f, .sustain = 0.25f, .releaseMs = 80.0f}};
    p.ops[3] = {.level = 1.0f, .velSens = 0.3f, .env = {.attackMs = 2.0f, .decayMs = 800.0f, .sustain = 0.7f, .releaseMs = 60.0f}};
    p.outputDb = util::gainToDb(0.45f);
    return p;
}

/// Resonant FM pluck (L1.10): a feedback-saw-like C>D pair through a resonant low-pass whose envelope
/// opens it 5 octaves and closes it again in ~0.3 s. Key tracking 0.5 keeps high notes from going dull;
/// velocity opens the filter (soft notes are 2 octaves darker).
inline Patch resoPluck()
{
    Patch p;
    p.algorithm = kAlgorithms[0]; // stack: only C>D sound (A and B silent)
    p.algorithm.mod[2][2] = 1.0f; // C feedback: C's sine becomes saw-like, so D's spectrum is rich
    p.ops[0].level = p.ops[1].level = 0.0f;
    p.ops[2] = {.level = 2.0f, .env = {.attackMs = 1.0f, .decayMs = 2000.0f, .sustain = 0.8f, .releaseMs = 200.0f}};
    p.ops[3] = {.level = 1.0f, .env = {.attackMs = 1.0f, .decayMs = 1500.0f, .sustain = 0.5f, .releaseMs = 250.0f}};
    p.filter = {.mode = FilterMode::LowPass, .cutoffHz = 250.0f, .q = 5.0f, .envOctaves = 5.0f, .keyTrack = 0.5f,
                .velOctaves = 2.0f, .env = {.attackMs = 1.0f, .decayMs = 300.0f, .sustain = 0.0f, .releaseMs = 300.0f}};
    p.outputDb = util::gainToDb(0.22f);
    return p;
}

/// Wobble bass (L1.11): the FM bass through a resonant low-pass that the global LFO 2 sweeps ±2.5 octaves
/// at 3 Hz (triangle, restarted on each note so every note wobbles the same way).
inline Patch wobbleBass()
{
    Patch p = fmBass();
    p.ops[2].level = 3.0f; // brighter source: the filter needs harmonics to sweep through
    p.ops[2].env.sustain = 0.6f;
    p.filter = {.mode = FilterMode::LowPass, .cutoffHz = 400.0f, .q = 6.0f, .keyTrack = 0.5f,
                .env = {.attackMs = 1.0f, .decayMs = 300.0f, .sustain = 0.0f, .releaseMs = 200.0f}};
    p.lfo2 = {.rateHz = 3.0f, .shape = dsp::LfoShape::Triangle, .keySync = true};
    p.routes.lfo2CutoffOctaves = 2.5f;
    p.outputDb = util::gainToDb(0.3f);
    return p;
}

/// Evolving FM pad (L1.11): two slow stacks (algorithm 5) whose brightness drifts with a slow smooth-random
/// LFO 2 on the FM index, plus a gentle free-running vibrato that fades in.
inline Patch evolvingPad()
{
    using util::Ratio;
    Patch p;
    p.algorithm = kAlgorithms[4];
    p.algorithmNumber = 5;
    p.ops[0] = {.ratio = Ratio{2.0}, .detune = util::Cents{4.0}, .level = 1.4f,
                .env = {.attackMs = 900.0f, .decayMs = 3000.0f, .sustain = 0.7f, .releaseMs = 1500.0f}};
    p.ops[1] = {.level = 0.5f, .env = {.attackMs = 600.0f, .decayMs = 2000.0f, .sustain = 0.9f, .releaseMs = 1800.0f}};
    p.ops[2] = {.ratio = Ratio{3.0}, .detune = util::Cents{-5.0}, .level = 1.0f,
                .env = {.attackMs = 1500.0f, .decayMs = 4000.0f, .sustain = 0.6f, .releaseMs = 1500.0f}};
    p.ops[3] = {.ratio = Ratio{1.0}, .detune = util::Cents{3.0}, .level = 0.5f,
                .env = {.attackMs = 800.0f, .decayMs = 3000.0f, .sustain = 0.9f, .releaseMs = 2000.0f}};
    p.filter = {.mode = FilterMode::LowPass, .cutoffHz = 3500.0f, .q = 0.9f, .keyTrack = 0.3f};
    p.lfo1 = {.rateHz = 4.8f, .shape = dsp::LfoShape::Sine, .delayMs = 400.0f, .fadeMs = 1200.0f, .keySync = false};
    p.lfo2 = {.rateHz = 0.2f, .shape = dsp::LfoShape::SmoothRandom, .keySync = false};
    p.routes = {.vibratoSemitones = 0.12f, .wheelVibratoSemitones = 0.5f, .lfo2CutoffOctaves = 0.7f, .lfo2IndexAmount = 0.8f};
    p.outputDb = util::gainToDb(0.25f);
    return p;
}

/// Legato bass (L2.5): the Reso pluck's sound as a mono legato synth with an 80 ms glide between
/// overlapping notes (lowest-note priority would suit a pedal-tone bass line; this one uses last-note).
inline Patch legatoBass()
{
    Patch p = resoPluck();
    p.ops[2].env.sustain = 0.9f;
    p.ops[3].env = {.attackMs = 2.0f, .decayMs = 800.0f, .sustain = 0.8f, .releaseMs = 120.0f};
    p.filter.env = {.attackMs = 1.0f, .decayMs = 400.0f, .sustain = 0.25f, .releaseMs = 200.0f};
    p.filter.envOctaves = 4.0f;
    p.voice = {.mode = VoiceMode::Legato, .glideMs = 80.0f, .glideLegatoOnly = true};
    p.outputDb = util::gainToDb(0.3f);
    return p;
}

/// Tubular bell (L3.4): Chowning's inharmonic recipe. Stack 1: A(×3.5) > B, a long-decaying index, so
/// the clang mellows into a near-sine as it rings. Stack 2: C(×7, 60 ms) > D(×3) adds the strike.
inline Patch bell()
{
    using util::Ratio;
    Patch p;
    p.algorithm = kAlgorithms[4];
    p.algorithmNumber = 5;
    p.ops[0] = {.ratio = Ratio{3.5}, .level = 2.5f, .velSens = 0.6f, .keyRateScale = 0.4f,
                .env = {.attackMs = 1.0f, .decayMs = 4000.0f, .sustain = 0.0f, .releaseMs = 2500.0f}};
    p.ops[1] = {.level = 1.0f, .velSens = 0.3f, .keyRateScale = 0.4f,
                .env = {.attackMs = 1.0f, .decayMs = 7000.0f, .sustain = 0.0f, .releaseMs = 3000.0f}};
    p.ops[2] = {.ratio = Ratio{7.0}, .level = 1.8f, .velSens = 0.8f,
                .env = {.attackMs = 1.0f, .decayMs = 60.0f, .sustain = 0.0f, .releaseMs = 60.0f}};
    p.ops[3] = {.ratio = Ratio{3.0}, .level = 0.35f, .velSens = 0.5f, .keyRateScale = 0.4f,
                .env = {.attackMs = 1.0f, .decayMs = 2500.0f, .sustain = 0.0f, .releaseMs = 1500.0f}};
    p.outputDb = util::gainToDb(0.3f);
    return p;
}

/// FM brass (L3.4): Chowning's other classic. One 1:1 pair whose INDEX envelope follows the loudness
/// envelope, so the note gets brighter as it swells (the "blat"), with feedback for extra harmonics, a delayed
/// vibrato, and a key-tracked low-pass to keep the top end from fizzing.
inline Patch brass()
{
    Patch p;
    p.algorithm = kAlgorithms[0];
    p.algorithm.mod[2][2] = 0.35f;
    p.ops[0].level = p.ops[1].level = 0.0f;
    p.ops[2] = {.level = 3.2f, .velSens = 0.7f,
                .env = {.attackMs = 70.0f, .decayMs = 400.0f, .sustain = 0.65f, .releaseMs = 140.0f}};
    p.ops[3] = {.level = 1.0f, .velSens = 0.4f,
                .env = {.attackMs = 45.0f, .decayMs = 300.0f, .sustain = 0.85f, .releaseMs = 160.0f}};
    p.filter = {.mode = FilterMode::LowPass, .cutoffHz = 2800.0f, .q = 0.8f, .keyTrack = 0.6f};
    p.lfo1 = {.rateHz = 5.5f, .shape = dsp::LfoShape::Sine, .delayMs = 350.0f, .fadeMs = 500.0f, .keySync = true};
    p.routes.vibratoSemitones = 0.1f;
    p.outputDb = util::gainToDb(0.25f); // measured (l3.4): 0.35 peaked at -1.7 dBFS, into the soft clip
    return p;
}

/// Drawbar organ (L3.4): algorithm 8, four carriers at 16', 8', 4' and 2 2/3' (ratios 0.5, 1, 2, 3), organ
/// envelopes (instant on, full sustain, quick off), a touch of feedback on the top drawbar for grit, and a
/// free-running vibrato standing in for a scanner chorus.
inline Patch organ()
{
    using util::Ratio;
    Patch p;
    p.algorithm = kAlgorithms[7];
    p.algorithmNumber = 8;
    const dsp::AdsrParams env{.attackMs = 3.0f, .decayMs = 200.0f, .sustain = 1.0f, .releaseMs = 50.0f};
    p.ops[0] = {.ratio = Ratio{0.5}, .level = 0.8f, .env = env};
    p.ops[1] = {.ratio = Ratio{1.0}, .level = 1.0f, .env = env};
    p.ops[2] = {.ratio = Ratio{2.0}, .level = 0.7f, .env = env};
    p.ops[3] = {.ratio = Ratio{3.0}, .level = 0.5f, .env = env};
    p.algorithm.mod[3][3] = 0.25f;
    p.lfo1 = {.rateHz = 6.7f, .shape = dsp::LfoShape::Triangle, .keySync = false};
    p.routes.vibratoSemitones = 0.06f;
    p.outputDb = util::gainToDb(0.28f); // measured (l3.4): 0.4 peaked at -2.3 dBFS
    return p;
}

/// Marimba (L3.4): two short stacks (algorithm 5). A(×4) > B is the wooden bar: the index dies in 70 ms, so
/// the mallet's brightness is only at the start. C(×10) > D(×4) is the knock of the mallet. Higher keys
/// decay faster (key rate scaling), like real bars.
inline Patch marimba()
{
    using util::Ratio;
    Patch p;
    p.algorithm = kAlgorithms[4];
    p.algorithmNumber = 5;
    p.ops[0] = {.ratio = Ratio{4.0}, .level = 1.8f, .velSens = 0.9f, .keyRateScale = 0.5f,
                .env = {.attackMs = 1.0f, .decayMs = 70.0f, .sustain = 0.0f, .releaseMs = 70.0f}};
    p.ops[1] = {.level = 1.0f, .velSens = 0.5f, .keyRateScale = 0.6f,
                .env = {.attackMs = 1.0f, .decayMs = 900.0f, .sustain = 0.0f, .releaseMs = 400.0f}};
    p.ops[2] = {.ratio = Ratio{10.0}, .level = 1.2f, .velSens = 0.9f,
                .env = {.attackMs = 1.0f, .decayMs = 25.0f, .sustain = 0.0f, .releaseMs = 25.0f}};
    p.ops[3] = {.ratio = Ratio{4.0}, .level = 0.3f, .velSens = 0.7f, .keyRateScale = 0.6f,
                .env = {.attackMs = 1.0f, .decayMs = 220.0f, .sustain = 0.0f, .releaseMs = 150.0f}};
    p.outputDb = util::gainToDb(0.6f); // measured (l3.4): 0.4 peaked at only -9.3 dBFS
    return p;
}

/// E-piano + chorus (L5.4): the classic pairing. The E-piano patch untouched (so `l1.7_piece.wav`, which uses
/// it, keeps its hash), with a slow two-voice chorus at 50 % and the width opened a little, because the
/// chorus is what makes the mono voice stereo. A new preset rather than a change to "E-piano": players can
/// A/B the two.
inline Patch epianoChorus()
{
    Patch p = epiano();
    p.fx.chorus = {.enabled = true, .rateHz = 0.5f, .depthMs = 3.5f, .delayMs = 8.0f, .voices = 2, .mixPct = 50.0f};
    p.fx.width = {.enabled = true, .widthPct = 130.0f};
    return p;
}

/// The factory list hosts show (fss_play, the plugin editor). Since L3.4 the factory bank also exists as
/// text files in presets/factory/ (written from these functions by `fss_render l3.4`; a test checks that
/// the files and these functions agree), so the files are what users read, copy and edit.
struct NamedPreset {
    const char* name;
    Patch (*make)();
};
inline constexpr NamedPreset kFactoryPresets[] = {
    {"E-piano", epiano},
    {"FM bass", fmBass},
    {"Reso pluck", resoPluck},
    {"Wobble bass", wobbleBass},
    {"FM pad", evolvingPad},
    {"Legato bass", legatoBass},
    {"Bell", bell},
    {"Brass", brass},
    {"Organ", organ},
    {"Marimba", marimba},
    {"E-piano chorus", epianoChorus}, // L5.4. Append only: the list position is the file number ("11 ...")
    {"Init", [] { return Patch{}; }},
};

} // namespace fss::synth::presets
