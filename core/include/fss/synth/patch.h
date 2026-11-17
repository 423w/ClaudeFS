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
#pragma once

#include <fss/dsp/adsr.h>
#include <fss/synth/algorithm.h>
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
};

struct Patch {
    Algorithm algorithm = kAlgorithms[0]; // a copy: feedback (diagonal) is part of the patch
    std::array<OperatorParams, kNumOps> ops{};
    util::Hz tuningA4{440.0};
    util::Semitones bendRange{2.0};       // full pitch-bend wheel deflection
    float outputGain = 1.0f;
};

/// What the player did: which key, how hard, and the current bend (−1..1 of bendRange).
struct NoteContext {
    std::uint8_t key = 60;
    float velocity = 1.0f; // 0..1 (MIDI velocity / 127)
    float bend = 0.0f;     // −1..1
};

} // namespace fss::synth
