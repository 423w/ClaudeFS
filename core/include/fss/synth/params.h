// fss/synth/params.h: THE parameter table (L2.4). The only place parameters are defined.
//
// Everything that shows, sends, stores or maps a parameter reads this table: the GUI builds its sliders
// from it, MIDI CCs are mapped through it, ParamChange events carry its IDs, presets (L3.4) and the CLAP
// plugin's parameter list (L3.2) will be generated from it.
//
// STABLE IDS: presets and DAW automation refer to parameters by `id`, forever. Never renumber or reuse
// an id; add new parameters with new ids (append new OpField values at the END of the enum). Array order
// is free to change (it's only display order).
//   globals:             1..99
//   operator parameters: 1000 + 100·op + field   (op 0..3 = A..D)
//
// VALUES are always *plain* (the unit shown in `unit`: Hz, ms, dB...). The normalized 0..1 form, which is
// what a knob or a CLAP host deals in, comes from toNormalized/fromNormalized, using the parameter's skew.
// Since L3.2 the CLAP host sees continuous parameters as that 0..1 knob position, so a continuous
// parameter's min, max and skew are part of the stable contract too: changing them changes the meaning of
// every saved DAW automation clip. (Presets store plain values and are unaffected.)
#pragma once

#include <fss/synth/patch.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

namespace fss::synth {

enum class Skew : std::uint8_t {
    Linear,  // equal knob travel = equal value change (sustain, detune, dB)
    Exp,     // equal knob travel = equal *ratio* (times, frequencies, ratios): min must be > 0
    Power2,  // value = range·x²: fine control near 0 (operator levels: carriers live in 0..1, indices up to 8)
    Stepped, // integers (algorithm, switches): never smoothed, never interpolated
};

/// What a change affects, so the Engine knows what to recompute (and what may be smoothed).
enum class ParamTarget : std::uint8_t {
    OpLevel,   // operator level / velocity / key-level scaling: smoothed per sample in the operator
    OpFreq,    // ratio, detune, fixed mode/frequency: frequencies recomputed (phase stays continuous)
    OpEnv,     // envelope times/sustain, rate scaling: envelopes updated (sustain glides by itself)
    Feedback,  // modulation matrix diagonal: smoothed per sample in the Engine
    Algorithm, // which preset matrix: the Engine morphs the matrix over the smoothing time
    Output,    // output gain: smoothed per sample
    Tuning,    // A4 and bend range: frequencies recomputed
    FilterCutoff, // base cutoff: smoothed per sample in the Engine, in octaves (L1.10)
    FilterQ,      // resonance: smoothed per sample in the Engine
    Filter,       // mode, env amount, key track, velocity: voices read them every sample, nothing to do
    FilterEnv,    // filter envelope times/sustain: envelopes updated (like OpEnv)
    LfoRate,      // an LFO's rate: the Engine re-tunes its LFOs and every voice's LFO 1 (L1.11)
    Lfo,          // shapes, depths, delay/fade, sync: read every sample or at the next note, nothing to do
    VoiceMode,    // poly/mono/legato: the Engine releases what's sounding and starts clean (L2.5)
    Play,         // glide, glide mode, note priority: read at the next note event, nothing to do
};

enum class OpField : std::uint8_t { // APPEND ONLY: values are part of the stable parameter ids
    Ratio, Detune, Fixed, FixedHz, Level, VelSens, KeyLevel, KeyRate, Attack, Decay, Sustain, Release, Feedback,
};
inline constexpr int kNumOpFields = 13;

struct ParamInfo {
    std::uint32_t id;
    std::string_view name;   // ASCII (shown in consoles and DAWs)
    std::string_view unit;
    float min, max, def;
    Skew skew;
    float smoothingMs;       // 0 = not smoothed
    std::uint8_t midiCC;     // 0 = not mapped
    ParamTarget target;
    std::int8_t op;          // operator index 0..3, or −1 for globals
    OpField field;           // meaningful when op ≥ 0
    std::string_view group = "Global"; // GUI section (and later the plugin's parameter "module")
};

namespace param_id {
// Globals are numbered in groups of ten so related ids stay together: 1-9 global, 10-19 filter (L1.10),
// 30-39 LFO 1 + vibrato, 40-49 LFO 2 (L1.11), 50-59 voice / playing (L2.5).
inline constexpr std::uint32_t kAlgorithm = 1, kOutputDb = 2, kTuningA4 = 3, kBendRange = 4;
inline constexpr std::uint32_t kFilterMode = 10, kCutoff = 11, kResonance = 12, kFilterEnvAmount = 13,
                               kKeyTrack = 14, kFilterVel = 15, kFilterAttack = 16, kFilterDecay = 17,
                               kFilterSustain = 18, kFilterRelease = 19;
inline constexpr std::uint32_t kLfo1Rate = 30, kLfo1Shape = 31, kLfo1Delay = 32, kLfo1Fade = 33, kLfo1KeySync = 34,
                               kVibrato = 35, kWheelVibrato = 36;
inline constexpr std::uint32_t kLfo2Rate = 40, kLfo2Shape = 41, kLfo2Retrigger = 42, kLfo2Cutoff = 43, kLfo2Index = 44;
inline constexpr std::uint32_t kVoiceMode = 50, kGlide = 51, kGlideLegato = 52, kPriority = 53;
constexpr std::uint32_t op(int opIndex, OpField f) { return 1000u + 100u * static_cast<std::uint32_t>(opIndex) + static_cast<std::uint32_t>(f); }
} // namespace param_id

namespace detail {

struct OpFieldInfo {
    OpField field;
    std::string_view name, unit;
    float min, max, def;
    Skew skew;
    float smoothingMs;
    ParamTarget target;
};

// Ranges and defaults for one operator's parameters. Defaults equal OperatorParams{} (tested).
inline constexpr OpFieldInfo kOpFields[kNumOpFields] = {
    {OpField::Ratio, "Ratio", "x", 0.125f, 32.0f, 1.0f, Skew::Exp, 0.0f, ParamTarget::OpFreq},
    {OpField::Detune, "Detune", "cents", -100.0f, 100.0f, 0.0f, Skew::Linear, 0.0f, ParamTarget::OpFreq},
    {OpField::Fixed, "Fixed freq", "on/off", 0.0f, 1.0f, 0.0f, Skew::Stepped, 0.0f, ParamTarget::OpFreq},
    {OpField::FixedHz, "Fixed Hz", "Hz", 1.0f, 20000.0f, 440.0f, Skew::Exp, 0.0f, ParamTarget::OpFreq},
    {OpField::Level, "Level", "", 0.0f, 8.0f, 1.0f, Skew::Power2, 5.0f, ParamTarget::OpLevel},
    {OpField::VelSens, "Vel sens", "", 0.0f, 1.0f, 0.0f, Skew::Linear, 5.0f, ParamTarget::OpLevel},
    {OpField::KeyLevel, "Key level", "dB/oct", -12.0f, 24.0f, 0.0f, Skew::Linear, 5.0f, ParamTarget::OpLevel},
    {OpField::KeyRate, "Key rate", "", -1.0f, 2.0f, 0.0f, Skew::Linear, 0.0f, ParamTarget::OpEnv},
    {OpField::Attack, "Attack", "ms", 1.0f, 10000.0f, 5.0f, Skew::Exp, 0.0f, ParamTarget::OpEnv},
    {OpField::Decay, "Decay", "ms", 1.0f, 30000.0f, 300.0f, Skew::Exp, 0.0f, ParamTarget::OpEnv},
    {OpField::Sustain, "Sustain", "", 0.0f, 1.0f, 0.7f, Skew::Linear, 0.0f, ParamTarget::OpEnv}, // glides in Adsr
    {OpField::Release, "Release", "ms", 1.0f, 30000.0f, 300.0f, Skew::Exp, 0.0f, ParamTarget::OpEnv},
    {OpField::Feedback, "Feedback", "", 0.0f, 4.0f, 0.0f, Skew::Linear, 10.0f, ParamTarget::Feedback},
};

// MIDI CC defaults for operators: none since L1.11. (Until L1.10, CC 74 and CC 71 drove op A / op C levels;
// they now drive the filter, General MIDI 2's meaning. Until L1.11, CC 1, the mod wheel, drove op A
// feedback; it is now a performance controller the Engine receives directly, for vibrato.)
constexpr std::uint8_t defaultCC(int /*op*/, OpField /*f*/) { return 0; }

// Global parameters in display order. Each row: id, name, unit, min, max, default, skew, smoothing ms,
// MIDI CC, target, op (−1), field (unused), group. Defaults equal Patch{} (tested).
inline constexpr ParamInfo kGlobalParams[] = {
    {param_id::kAlgorithm, "Algorithm", "", 1.0f, 8.0f, 1.0f, Skew::Stepped, 10.0f, 0, ParamTarget::Algorithm, -1, {}, "Global"},
    {param_id::kOutputDb, "Output", "dB", -60.0f, 12.0f, 0.0f, Skew::Linear, 10.0f, 7, ParamTarget::Output, -1, {}, "Global"},
    {param_id::kTuningA4, "Tuning A4", "Hz", 400.0f, 480.0f, 440.0f, Skew::Linear, 0.0f, 0, ParamTarget::Tuning, -1, {}, "Global"},
    {param_id::kBendRange, "Bend range", "st", 0.0f, 24.0f, 2.0f, Skew::Stepped, 0.0f, 0, ParamTarget::Tuning, -1, {}, "Global"},
    // ---- filter (L1.10) ----
    // Mode: 0 Off, 1 Low-pass, 2 Band-pass, 3 High-pass, 4 Notch (FilterMode).
    {param_id::kFilterMode, "Filter", "mode", 0.0f, 4.0f, 0.0f, Skew::Stepped, 0.0f, 0, ParamTarget::Filter, -1, {}, "Filter"},
    // Cutoff smoothed over 10 ms *in octaves* (Engine), so a CC sweep glides evenly across the range.
    {param_id::kCutoff, "Cutoff", "Hz", 20.0f, 20000.0f, 2000.0f, Skew::Exp, 10.0f, 74, ParamTarget::FilterCutoff, -1, {}, "Filter"},
    {param_id::kResonance, "Resonance", "Q", 0.5f, 25.0f, 0.7071f, Skew::Exp, 10.0f, 71, ParamTarget::FilterQ, -1, {}, "Filter"},
    {param_id::kFilterEnvAmount, "Env amount", "oct", -8.0f, 8.0f, 0.0f, Skew::Linear, 0.0f, 0, ParamTarget::Filter, -1, {}, "Filter"},
    {param_id::kKeyTrack, "Key track", "", 0.0f, 1.0f, 0.0f, Skew::Linear, 0.0f, 0, ParamTarget::Filter, -1, {}, "Filter"},
    {param_id::kFilterVel, "Vel > cutoff", "oct", 0.0f, 6.0f, 0.0f, Skew::Linear, 0.0f, 0, ParamTarget::Filter, -1, {}, "Filter"},
    {param_id::kFilterAttack, "F attack", "ms", 1.0f, 10000.0f, 1.0f, Skew::Exp, 0.0f, 0, ParamTarget::FilterEnv, -1, {}, "Filter"},
    {param_id::kFilterDecay, "F decay", "ms", 1.0f, 30000.0f, 400.0f, Skew::Exp, 0.0f, 0, ParamTarget::FilterEnv, -1, {}, "Filter"},
    {param_id::kFilterSustain, "F sustain", "", 0.0f, 1.0f, 0.0f, Skew::Linear, 0.0f, 0, ParamTarget::FilterEnv, -1, {}, "Filter"},
    {param_id::kFilterRelease, "F release", "ms", 1.0f, 30000.0f, 300.0f, Skew::Exp, 0.0f, 0, ParamTarget::FilterEnv, -1, {}, "Filter"},
    // ---- LFO 1: per voice, drives vibrato (L1.11) ----
    // Shapes: 0 sine, 1 triangle, 2 saw up, 3 saw down, 4 square, 5 sample & hold, 6 smooth random.
    {param_id::kLfo1Rate, "LFO1 rate", "Hz", 0.05f, 20.0f, 5.0f, Skew::Exp, 0.0f, 0, ParamTarget::LfoRate, -1, {}, "LFO 1 (vibrato)"},
    {param_id::kLfo1Shape, "LFO1 shape", "", 0.0f, 6.0f, 0.0f, Skew::Stepped, 0.0f, 0, ParamTarget::Lfo, -1, {}, "LFO 1 (vibrato)"},
    {param_id::kLfo1Delay, "LFO1 delay", "ms", 0.0f, 5000.0f, 0.0f, Skew::Power2, 0.0f, 0, ParamTarget::Lfo, -1, {}, "LFO 1 (vibrato)"},
    {param_id::kLfo1Fade, "LFO1 fade", "ms", 0.0f, 5000.0f, 0.0f, Skew::Power2, 0.0f, 0, ParamTarget::Lfo, -1, {}, "LFO 1 (vibrato)"},
    {param_id::kLfo1KeySync, "LFO1 key sync", "on/off", 0.0f, 1.0f, 1.0f, Skew::Stepped, 0.0f, 0, ParamTarget::Lfo, -1, {}, "LFO 1 (vibrato)"},
    {param_id::kVibrato, "Vibrato", "st", 0.0f, 12.0f, 0.0f, Skew::Power2, 0.0f, 0, ParamTarget::Lfo, -1, {}, "LFO 1 (vibrato)"},
    {param_id::kWheelVibrato, "Wheel > vib", "st", 0.0f, 2.0f, 0.5f, Skew::Power2, 0.0f, 0, ParamTarget::Lfo, -1, {}, "LFO 1 (vibrato)"},
    // ---- LFO 2: global (one for all voices), drives cutoff and FM index ----
    {param_id::kLfo2Rate, "LFO2 rate", "Hz", 0.05f, 20.0f, 2.0f, Skew::Exp, 0.0f, 0, ParamTarget::LfoRate, -1, {}, "LFO 2 (filter, index)"},
    {param_id::kLfo2Shape, "LFO2 shape", "", 0.0f, 6.0f, 0.0f, Skew::Stepped, 0.0f, 0, ParamTarget::Lfo, -1, {}, "LFO 2 (filter, index)"},
    {param_id::kLfo2Retrigger, "LFO2 retrig", "on/off", 0.0f, 1.0f, 0.0f, Skew::Stepped, 0.0f, 0, ParamTarget::Lfo, -1, {}, "LFO 2 (filter, index)"},
    {param_id::kLfo2Cutoff, "LFO2 > cutoff", "oct", 0.0f, 6.0f, 0.0f, Skew::Power2, 0.0f, 0, ParamTarget::Lfo, -1, {}, "LFO 2 (filter, index)"},
    {param_id::kLfo2Index, "LFO2 > index", "", 0.0f, 1.0f, 0.0f, Skew::Linear, 0.0f, 0, ParamTarget::Lfo, -1, {}, "LFO 2 (filter, index)"},
    // ---- voice / playing (L2.5) ----
    // Mode: 0 poly, 1 mono (retrigger), 2 legato. Priority: 0 last, 1 low, 2 high.
    {param_id::kVoiceMode, "Voice mode", "", 0.0f, 2.0f, 0.0f, Skew::Stepped, 0.0f, 0, ParamTarget::VoiceMode, -1, {}, "Voice"},
    {param_id::kGlide, "Glide", "ms", 0.0f, 2000.0f, 0.0f, Skew::Power2, 0.0f, 5, ParamTarget::Play, -1, {}, "Voice"},
    {param_id::kGlideLegato, "Glide legato", "on/off", 0.0f, 1.0f, 1.0f, Skew::Stepped, 0.0f, 0, ParamTarget::Play, -1, {}, "Voice"},
    {param_id::kPriority, "Priority", "", 0.0f, 2.0f, 0.0f, Skew::Stepped, 0.0f, 0, ParamTarget::Play, -1, {}, "Voice"},
};
inline constexpr std::size_t kNumGlobalParams = std::size(kGlobalParams);

constexpr auto makeTable()
{
    std::array<ParamInfo, kNumGlobalParams + kNumOps * kNumOpFields> t{};
    std::size_t i = 0;
    for (const ParamInfo& g : kGlobalParams)
        t[i++] = g;
    for (int op = 0; op < kNumOps; ++op)
        for (const OpFieldInfo& f : kOpFields)
            t[i++] = {param_id::op(op, f.field), f.name, f.unit, f.min, f.max, f.def, f.skew, f.smoothingMs,
                      defaultCC(op, f.field), f.target, static_cast<std::int8_t>(op), f.field, "Operators"};
    return t;
}

} // namespace detail

inline constexpr auto kParams = detail::makeTable();

/// Look a parameter up by id (linear scan of ~56 entries: fine off the hot path; the Engine calls it once per event).
[[nodiscard]] constexpr const ParamInfo* findParam(std::uint32_t id) noexcept
{
    for (const ParamInfo& p : kParams)
        if (p.id == id)
            return &p;
    return nullptr;
}

inline constexpr std::string_view kOpNames[kNumOps] = {"A", "B", "C", "D"};

// ---- plain <-> normalized ------------------------------------------------------------------------

[[nodiscard]] inline float clampToRange(const ParamInfo& p, float v) noexcept
{
    if (std::isnan(v))
        return p.def;
    v = std::clamp(v, p.min, p.max);
    return p.skew == Skew::Stepped ? std::round(v) : v;
}

[[nodiscard]] inline float toNormalized(const ParamInfo& p, float plain) noexcept
{
    const float v = clampToRange(p, plain);
    switch (p.skew) {
    case Skew::Exp: return std::log(v / p.min) / std::log(p.max / p.min);
    case Skew::Power2: return std::sqrt((v - p.min) / (p.max - p.min));
    default: return (v - p.min) / (p.max - p.min);
    }
}

[[nodiscard]] inline float fromNormalized(const ParamInfo& p, float norm) noexcept
{
    const float x = std::clamp(std::isnan(norm) ? 0.0f : norm, 0.0f, 1.0f);
    float v = 0.0f;
    switch (p.skew) {
    case Skew::Exp: v = p.min * std::pow(p.max / p.min, x); break;
    case Skew::Power2: v = p.min + (p.max - p.min) * x * x; break;
    default: v = p.min + (p.max - p.min) * x; break;
    }
    return clampToRange(p, v);
}

// ---- Patch <-> parameter values --------------------------------------------------------------------

/// Read parameter `p` from a patch, in plain units.
[[nodiscard]] float getParam(const Patch& patch, const ParamInfo& p) noexcept;
/// Write parameter `p` (plain units; clamped to range) into a patch.
void setParam(Patch& patch, const ParamInfo& p, float plain) noexcept;

/// The name of a stepped parameter's value ("Low-pass", "Saw up", "On", an algorithm's diagram), or an
/// empty view if the parameter has no names (continuous parameters, plain counts like bend range).
/// One list for every host: the dev UI, the CLAP plugin's value_to_text (L3.2) and its editor (L3.5).
[[nodiscard]] std::string_view valueLabel(const ParamInfo& p, int value) noexcept;

} // namespace fss::synth
