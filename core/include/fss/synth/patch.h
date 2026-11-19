// fss/synth/patch.h: everything that defines one sound, and the context of one played note (L1.6).
//
// A Patch is plain data (copyable, comparable field by field, later serializable in L3.4). A voice reads it
// once at note start (FmVoice::start) and turns it into operator frequencies, levels and envelope times.
//
// How a note becomes operator settings (FmVoice::start):
//   noteHz      = tuningA4 · 2^((key + bend − 69)/12)
//   op freq     = fixed ? fixedHz : noteHz · ratio · 2^(detune/1200)
//   op level    = level · velocityGain · keyLevelGain
//       velocityGain = 10^( velSens · kVelocityRangeDb · (velocity − 1) / 20 )   (0 dB at full velocity)
//       keyLevelGain = 10^( −keyLevelDbPerOct · max(0, octaves above the breakpoint) / 20 )
//   env times   = times · 2^( −keyRateScale · octaves from the breakpoint )      (higher notes = faster)
// Because a modulator's level is its index (operator.h), velocity and key scaling on a MODULATOR change
// brightness, and on a CARRIER change loudness. One mechanism, two musical effects.
//
// Signal path of one voice (L1.10):  operators (FmVoice) → filter (Svf, FilterParams) → steal fade → mix.
// Then, for the sum of all voices (L5.1): the FX chain (fx/fx_chain.h, FxParams) → output level → soft clip.
// There is no separate "amp" stage: the carriers' envelopes ARE the amplitude envelope.
#pragma once

#include <fss/dsp/adsr.h>
#include <fss/dsp/lfo.h>
#include <fss/fx/fx_params.h>
#include <fss/synth/algorithm.h>
#include <fss/synth/held_notes.h>
#include <fss/synth/mod_matrix.h>
#include <fss/util/units.h>

#include <array>
#include <cstdint>

namespace fss::synth {

inline constexpr float kVelocityRangeDb = 36.0f; // velSens = 1: velocity 0 is 36 dB below velocity 1
inline constexpr float kKeyBreakpoint = 60.0f;   // key scaling pivots on middle C

struct OperatorParams {
    util::Ratio ratio{1.0};       // frequency = note × ratio (ratio mode)
    util::Cents detune{0.0};      // fine offset on top of the ratio
    bool fixed = false;           // fixed-frequency mode: ignore the note (drums, inharmonic "body" partials)
    util::Hz fixedHz{440.0};
    float level = 1.0f;           // carrier: amplitude; modulator: index in radians
    float velSens = 0.0f;         // 0 = ignore velocity, 1 = full kVelocityRangeDb range
    float keyLevelDbPerOct = 0.0f;// attenuation per octave above the breakpoint (positive = quieter up high)
    float keyRateScale = 0.0f;    // 1 = envelope times halve per octave above (double per octave below)
    dsp::AdsrParams env{};
    std::uint8_t wave = 0;        // dsp::OpWave (L4.2): 0 = sine, the operator of before L4.2
};

/// The per-voice filter after the operators (L1.10). Off by default, so patches designed before L1.10
/// sound (and render) exactly as they did.
enum class FilterMode : std::uint8_t { Off, LowPass, BandPass, HighPass, Notch };

/// Where the cutoff ends up, per voice and per sample (all offsets in OCTAVES, because pitch is heard
/// logarithmically: +1 octave sounds like the same step whether the cutoff is at 200 Hz or 5 kHz):
///   cutoff = cutoffHz · 2^( envOctaves·env  +  keyTrack·(key − 60)/12  +  velOctaves·(velocity − 1) )
/// keyTrack 1 = the cutoff follows the keyboard exactly (same brightness relative to the note on every key);
/// velOctaves follows the operator-level convention: full velocity is the reference, softer is darker.
struct FilterParams {
    FilterMode mode = FilterMode::Off;
    float cutoffHz = 2000.0f;
    float q = 0.7071f;          // resonance as Q: 0.707 = no peak (Butterworth), 25 ≈ +28 dB peak
    float envOctaves = 0.0f;    // filter-envelope depth (negative = envelope closes the filter)
    float keyTrack = 0.0f;      // 0..1
    float velOctaves = 0.0f;    // ≥ 0: how many octaves darker a velocity-0 note is than a full-velocity one
    dsp::AdsrParams env{.attackMs = 1.0f, .decayMs = 400.0f, .sustain = 0.0f, .releaseMs = 300.0f};
};

/// One LFO's settings (L1.11). LFO 1 runs per voice (delay/fade per note, key sync = phase 0 at each note,
/// free = every voice follows one shared reference phase). LFO 2 is global: one LFO for the whole Engine,
/// so a chord wobbles together; keySync there means "restart on every note-on" (delay/fade unused).
struct LfoParams {
    float rateHz = 5.0f;
    dsp::LfoShape shape = dsp::LfoShape::Sine;
    float delayMs = 0.0f;
    float fadeMs = 0.0f;
    bool keySync = true;
};

/// Hard-wired modulation routes (L1.11), each depth in the unit the ear uses (patch.h, L1.11 journal):
///   pitch   += lfo1 · (vibratoSemitones · lfo1 fade + modWheel · wheelVibratoSemitones)   [semitones]
///   cutoff  += lfo2 · lfo2CutoffOctaves                                                    [octaves]
///   every modulation amount (the FM index, feedback included) × max(0, 1 + lfo2 · lfo2IndexAmount)
/// Since L4.1 these are the modulation matrix's four fixed slots (mod_matrix.h), ahead of the user slots.
struct ModRoutes {
    float vibratoSemitones = 0.0f;
    float wheelVibratoSemitones = 0.5f; // only acts when the mod wheel is up: 0 wheel = no vibrato
    float lfo2CutoffOctaves = 0.0f;
    float lfo2IndexAmount = 0.0f;       // 0..1 = ±0..100 % of the modulation index
};

/// How the keyboard maps onto voices (L2.5).
///   Poly    every key its own voice (up to the polyphony limit)
///   Mono    one voice; every new key restarts the envelopes ("retrigger"), with a 5 ms steal fade
///   Legato  one voice; while a key is held, a new key only changes the pitch (envelopes keep going)
enum class VoiceMode : std::uint8_t { Poly, Mono, Legato };

/// The modulation matrix's user slots and its own envelope (L4.1).
struct ModMatrixParams {
    std::array<ModSlotParams, kNumModSlots> slots{};
    dsp::AdsrParams env{.attackMs = 1.0f, .decayMs = 500.0f, .sustain = 0.0f, .releaseMs = 300.0f}; // "Mod env"
};

/// Unison (L4.4): each voice runs `voices` copies of its FM core, at evenly spread positions −1..1 (inner
/// positions first): copy detune = position × detuneCents/2, copy pan = position × width.
struct UnisonParams {
    std::uint8_t voices = 1;   // 1..8; 1 = the plain mono voice of before L4.4, bit for bit
    float detuneCents = 20.0f; // between the two outermost copies
    float widthPct = 80.0f;    // how far the outermost copies are panned
};

struct VoiceParams {
    VoiceMode mode = VoiceMode::Poly;
    float glideMs = 0.0f;          // portamento time, constant per glide (0 = off)
    bool glideLegatoOnly = true;   // glide only into notes played while another is held (connected phrases)
    NotePriority priority = NotePriority::Last; // mono/legato: which held key sounds
    std::uint8_t fmOversampling = 0; // L4.2: 0 = the FM core at the sample rate, 1 = at twice it (see voice.h)
};

struct Patch {
    Algorithm algorithm = kAlgorithms[0]; // a copy: feedback (diagonal) is part of the patch
    std::uint8_t algorithmNumber = 1;      // which preset `algorithm` came from (1..8), for the param table
    std::array<OperatorParams, kNumOps> ops{};
    FilterParams filter{};
    LfoParams lfo1{};
    LfoParams lfo2{.rateHz = 2.0f, .keySync = false};
    ModRoutes routes{};
    ModMatrixParams mod{};                // L4.1: 8 slots + the mod envelope; all slots off by default
    VoiceParams voice{};
    UnisonParams unison{};                // L4.4
    util::Hz tuningA4{440.0};
    util::Semitones bendRange{2.0};       // full pitch-bend wheel deflection
    // Output level in dB, the param table's unit. Until L3.4 this was a linear gain (outputGain), and the
    // conversion dB → gain → dB in getParam/setParam made some saved values come back 1 ulp different: near
    // 0 dB there are more distinct dB floats than gain floats, so linear storage can't hold every dB value.
    // Rule since then: a Patch stores each parameter in the param table's own unit; conversions happen where
    // the DSP needs them (Engine: util::dbToGain).
    float outputDb = 0.0f;
    fx::FxParams fx{};                    // the global FX chain after the voices (L5.1); all off by default
};

/// What the player did: which key, how hard, and the current bend (−1..1 of bendRange).
struct NoteContext {
    std::uint8_t key = 60;
    float velocity = 1.0f; // 0..1 (MIDI velocity / 127)
    float bend = 0.0f;     // −1..1
};

} // namespace fss::synth
