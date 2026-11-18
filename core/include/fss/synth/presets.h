// fss/synth/presets.h: a few factory patches shared by every host (offline renders, standalone, later
// the plugin). Introduced in L2.1 when a second host needed the same sounds; values are the ones designed
// and measured in L1.5–L1.7 (see those journals). A real preset system with files is L3.4.
#pragma once

#include <fss/synth/patch.h>

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
    p.outputGain = 0.22f;
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
    p.outputGain = 0.45f;
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
    p.outputGain = 0.22f;
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
    p.outputGain = 0.3f;
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
    p.outputGain = 0.25f;
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
    p.outputGain = 0.3f;
    return p;
}

/// The factory list hosts show as buttons (fss_play). A real preset system with files is L3.4.
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
    {"Init", [] { return Patch{}; }},
};

} // namespace fss::synth::presets
