// fss/fx/fx_params.h: the settings of the global FX chain, as plain data inside the Patch (L5.1).
//
// Like every other part of the Patch, each field is stored in the parameter table's own unit (L3.4 rule:
// percent stays percent, no lossy conversions), and a default-constructed FxParams is "every effect off", so
// presets made before an effect existed sound exactly as they did.
#pragma once

#include <cstdint>
#include <iterator>
#include <string_view>

namespace fss::fx {

/// Stereo width (L5.1): mid/side scaling. 100 % = unchanged, 0 % = mono, 200 % = the side doubled.
struct WidthParams {
    bool enabled = false;
    float widthPct = 100.0f;
};

/// Tempo-synced delay times (L5.3): name and length in quarter notes (beats). Order = the menu's order,
/// grouped by note value (triplet, straight, dotted), which is how players think, not sorted by length.
/// APPEND ONLY: the index is what presets and automation store.
struct DelayDivision {
    std::string_view name;
    double beats;
};
inline constexpr DelayDivision kDelayDivisions[] = {
    {"1/32", 0.125},       {"1/16 T", 0.25 * 2.0 / 3.0}, {"1/16", 0.25},  {"1/16 D", 0.375},
    {"1/8 T", 1.0 / 3.0},  {"1/8", 0.5},                 {"1/8 D", 0.75}, {"1/4 T", 2.0 / 3.0},
    {"1/4", 1.0},          {"1/4 D", 1.5},               {"1/2 T", 4.0 / 3.0}, {"1/2", 2.0},
    {"1/2 D", 3.0},        {"1/1", 4.0},
};
inline constexpr int kNumDelayDivisions = static_cast<int>(std::size(kDelayDivisions));

/// What happens when the delay time changes while echoes are sounding (L5.3 journal):
///   Clean  the old and the new time are both read and crossfaded over 50 ms: no pitch change, no click.
///   Tape   the read position glides to the new time (~150 ms): the echoes bend in pitch, like a tape delay's
///          motor speeding up or slowing down.
enum class DelayTimeMode : std::uint8_t { Clean, Tape };

/// Stereo delay (L5.3). Time from timeMs, or from the host tempo when sync is on.
struct DelayParams {
    bool enabled = false;
    float timeMs = 375.0f;
    bool sync = true;
    std::uint8_t division = 6;       // index into kDelayDivisions: 1/8 D, the classic dotted eighth
    float feedbackPct = 35.0f;
    bool pingPong = true;
    float dampingHz = 6000.0f;       // low-pass in the feedback path: each repeat a little darker
    float mixPct = 25.0f;            // equal-power dry/wet (echoes are uncorrelated with the dry sound)
    DelayTimeMode timeMode = DelayTimeMode::Clean;
};

/// Everything the FX chain needs. One member per effect, in chain order (fx_chain.h).
struct FxParams {
    DelayParams delay{};
    WidthParams width{};
};

} // namespace fss::fx
