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

/// Drive (L5.2): pre-emphasis → gain → waveshaper (at 1×, 2× or 4×) → de-emphasis → loudness match → mix.
/// `type` is a dsp::ShaperType (0 Soft, 1 Hard, 2 Fold, 3 Tube); `oversampling` 0 = 1×, 1 = 2×, 2 = 4×.
/// The oversampling factor sets the plugin's latency, so a change takes effect at the host's next restart
/// (fx/drive.h). Default 1×: no latency, so patches from before L5.2 stay bit-identical and undelayed.
struct DriveParams {
    bool enabled = false;
    std::uint8_t type = 0;
    float driveDb = 12.0f;      // input gain into the shaper
    float toneDb = 0.0f;        // pre-emphasis of the highs into the shaper (exactly undone after it)
    std::uint8_t oversampling = 0;
    bool autoGain = true;       // match the loudness of a −12 dBFS sine at any drive
    float outputDb = 0.0f;      // trim after the auto gain
    float mixPct = 100.0f;      // linear mix with the (latency-aligned) dry signal
};

/// Crush (L5.2): sample-rate reduction (sample & hold) and amplitude quantization. Deliberate aliasing.
struct CrushParams {
    bool enabled = false;
    float bits = 8.0f;          // 1..16, fractional allowed: step = 2^(1 − bits)
    float rateHz = 12000.0f;    // hold rate; at or above the sample rate: no reduction
    float mixPct = 100.0f;
};

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

/// Chorus (L5.4): `voices` copies of the input, each read from a delay that an LFO sweeps between delayMs and
/// delayMs + depthMs. Each voice has a fixed LFO offset (0, ½, ¼, ¾ cycle); the right channel is 90° on.
struct ChorusParams {
    bool enabled = false;
    float rateHz = 0.6f;
    float depthMs = 3.0f;     // how far each voice's delay swings (peak to peak)
    float delayMs = 10.0f;    // the shortest delay of the swing
    std::uint8_t voices = 2;  // 1..4
    float mixPct = 50.0f;     // equal-power mix: copies 10 ms late and moving are uncorrelated with the dry sound
};

/// Flanger (L5.4): one short delay swept by an LFO, with feedback: a moving comb filter.
struct FlangerParams {
    bool enabled = false;
    float rateHz = 0.2f;
    float delayMs = 0.5f;      // the shortest delay of the sweep
    float depthMs = 3.0f;      // sweep width (peak to peak)
    float feedbackPct = 50.0f; // −95..95: negative flips which frequencies resonate
    float mixPct = 50.0f;      // 50 % = the deepest notches
};

/// Phaser (L5.4): first-order allpass stages swept by an LFO, mixed with the dry sound: N/2 moving notches.
struct PhaserParams {
    bool enabled = false;
    float rateHz = 0.3f;
    float centerHz = 800.0f;   // the stages' break frequency at the middle of the sweep
    float depthOct = 2.0f;     // ± octaves around the centre
    float feedbackPct = 40.0f; // 0..90
    std::uint8_t stages = 6;   // 2..12
    float mixPct = 50.0f;
};

/// Everything the FX chain needs. One member per effect, in chain order (fx_chain.h).
struct FxParams {
    DriveParams drive{};    // L5.2
    CrushParams crush{};    // L5.2
    ChorusParams chorus{};
    FlangerParams flanger{};
    PhaserParams phaser{};
    DelayParams delay{};
    WidthParams width{};
};

} // namespace fss::fx
