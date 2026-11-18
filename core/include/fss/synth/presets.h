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

/// The factory list hosts show as buttons (fss_play). A real preset system with files is L3.4.
struct NamedPreset {
    const char* name;
    Patch (*make)();
};
inline constexpr NamedPreset kFactoryPresets[] = {
    {"E-piano", epiano},
    {"FM bass", fmBass},
    {"Reso pluck", resoPluck},
    {"Init", [] { return Patch{}; }},
};

} // namespace fss::synth::presets
