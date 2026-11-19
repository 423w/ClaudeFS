// fss/synth/mod_matrix.h: the modulation matrix (L4.1). Any source × via → any destination, per sample.
//
// A SLOT says "this much of source S, scaled by source V, goes to destination D":
//     contribution = value(S) · (value(V) · amount)          amount in D's natural unit (semitones, octaves...)
// Each destination is the sum of its slots' contributions, added to the destination's normal value:
//     pitch += Σ (semitones)    cutoff += Σ (octaves)    index ×= max(0, 1 + Σ)    amp ×= 2^(Σ/6.02 dB)  ...
// "Via" is how a mod wheel controls an LFO's depth (LFO 1 × mod wheel → pitch): set it to None (= 1) for a
// plain slot.
//
// GLOBAL VS PER VOICE. LFO 2, the mod wheel and pitch bend are the same for every voice; the others (LFO 1, the
// envelopes, velocity, key, random) exist per voice. A slot whose source and via are both global is evaluated
// once per sample in the Engine; any other slot in every voice. All destinations live in the voice, so every
// source can reach every destination. (The opposite case, a per-voice source on a post-mix destination like
// the delay's mix, can't work: there's one delay for all voices, so whose envelope should it follow? That's why
// the matrix has no FX destinations.)
//
// THE L1.11 ROUTES are the matrix's first four slots ("fixed slots"), built from their own parameters (ids 35,
// 36, 43, 44 stay, so presets and DAW automation of them keep working), followed by the eight user slots.
// Bit-exactness with the pre-matrix engine (the golden test) follows from two facts: a legacy expression alone
// on its destination is the same float operations as before (x·(1·a) = x·a; x·(f·v) = x·(v·f)), and adding an
// exact 0.0f (a destination nothing modulates) changes no bit.
//
// STABLE VALUES: ModSrc and ModDst numbers are stored in presets (the slot parameters). APPEND ONLY.
#pragma once

#include <array>
#include <cstdint>
#include <iterator>
#include <string_view>

namespace fss::synth {

enum class ModSrc : std::uint8_t { // APPEND ONLY
    None,      // the constant 1: "no via"; as a source, the slot is off
    Lfo1,      // −1..1, per voice
    Lfo1Fade,  // 0..1, LFO 1's delay/fade ramp (the patch Vibrato route waits for it)
    Lfo2,      // −1..1, global
    ModEnv,    // 0..1, the mod envelope (L4.1), per voice
    FilterEnv, // 0..1, per voice
    Velocity,  // 0..1
    Key,       // (key − 60)/64: about −1..1 across a full keyboard, 0 at middle C
    ModWheel,  // 0..1, global
    PitchBend, // −1..1, global
    Random,    // −1..1, a new value for each note, fixed for that note
};
inline constexpr int kNumModSources = 11;

enum class ModDst : std::uint8_t { // APPEND ONLY
    None,
    Pitch,     // semitones, every non-fixed operator
    Cutoff,    // octaves
    Resonance, // octaves of Q (×2 per unit)
    Index,     // every modulation amount (feedback too) × max(0, 1 + x)
    Amp,       // dB
    Lfo1Rate,  // octaves
    OpALevel, OpBLevel, OpCLevel, OpDLevel, // × max(0, 1 + x): a modulator's index or a carrier's loudness
    OpAPitch, OpBPitch, OpCPitch, OpDPitch, // semitones, that operator only
};
inline constexpr int kNumModDests = 15;

struct ModSourceInfo {
    std::string_view name;
    bool global; // the same value for every voice
};
inline constexpr ModSourceInfo kModSources[kNumModSources] = {
    {"None", true},       {"LFO 1", false},     {"LFO 1 fade", false}, {"LFO 2", true},
    {"Mod env", false},   {"Filter env", false}, {"Velocity", false},   {"Key", false},
    {"Mod wheel", true},  {"Pitch bend", true},  {"Random", false},
};

struct ModDestInfo {
    std::string_view name;
    std::string_view unit;
    float range; // what a slot amount of 100 % means, in `unit`
};
inline constexpr ModDestInfo kModDests[kNumModDests] = {
    {"None", "", 0.0f},          {"Pitch", "st", 24.0f},      {"Cutoff", "oct", 8.0f},
    {"Resonance", "oct", 2.0f},  {"FM index", "x", 1.0f},     {"Amp", "dB", 24.0f},
    {"LFO 1 rate", "oct", 4.0f}, {"Op A level", "x", 1.0f},   {"Op B level", "x", 1.0f},
    {"Op C level", "x", 1.0f},   {"Op D level", "x", 1.0f},   {"Op A pitch", "st", 24.0f},
    {"Op B pitch", "st", 24.0f}, {"Op C pitch", "st", 24.0f}, {"Op D pitch", "st", 24.0f},
};

/// One user slot, as the patch stores it (param table units: the amount is a percentage of the range).
struct ModSlotParams {
    ModSrc src = ModSrc::None;
    ModSrc via = ModSrc::None;
    ModDst dst = ModDst::None;
    float amountPct = 0.0f; // −100..100 % of kModDests[dst].range
};
inline constexpr int kNumModSlots = 8;

/// A slot ready to evaluate: amount already in the destination's unit.
struct ActiveModSlot {
    ModSrc src;
    ModSrc via;
    ModDst dst;
    float amount;
};

using ModValues = std::array<float, kNumModSources>; // indexed by ModSrc; [None] = 1
using ModSums = std::array<float, kNumModDests>;     // indexed by ModDst

[[nodiscard]] constexpr std::size_t idx(ModSrc s) noexcept { return static_cast<std::size_t>(s); }
[[nodiscard]] constexpr std::size_t idx(ModDst d) noexcept { return static_cast<std::size_t>(d); }
[[nodiscard]] constexpr std::uint32_t bit(ModDst d) noexcept { return 1u << idx(d); }
inline constexpr std::uint32_t kOpDestBits = bit(ModDst::OpALevel) | bit(ModDst::OpBLevel) | bit(ModDst::OpCLevel) |
                                             bit(ModDst::OpDLevel) | bit(ModDst::OpAPitch) | bit(ModDst::OpBPitch) |
                                             bit(ModDst::OpCPitch) | bit(ModDst::OpDPitch);

/// The patch's routing, split by where it's evaluated. Rebuilt when a route or slot parameter changes.
struct ModRouting {
    static constexpr int kMax = 4 + kNumModSlots;
    std::array<ActiveModSlot, kMax> global{}, voice{};
    int numGlobal = 0, numVoice = 0;
    std::uint32_t touched = 0;      // bit per ModDst that any slot reaches: the voice skips the rest (L4.1)
    std::uint32_t voiceSources = 0; // bit per ModSrc the per-voice slots read: the voice fills in only these

    void add(ModSrc src, ModSrc via, ModDst dst, float amount) noexcept
    {
        if (src == ModSrc::None || dst == ModDst::None || amount == 0.0f)
            return; // off: costs nothing
        touched |= 1u << idx(dst);
        const bool isGlobal = kModSources[idx(src)].global && kModSources[idx(via)].global;
        if (!isGlobal)
            voiceSources |= (1u << idx(src)) | (1u << idx(via));
        (isGlobal ? global[static_cast<std::size_t>(numGlobal++)] : voice[static_cast<std::size_t>(numVoice++)]) = {src, via, dst, amount};
    }
};

/// Safe defaults for code that has no matrix: only [None] = 1, no slots, nothing modulated.
inline constexpr ModValues kNoModSources = [] {
    ModValues v{};
    v[idx(ModSrc::None)] = 1.0f;
    return v;
}();
inline constexpr ModSums kNoModSums{};
inline constexpr ModRouting kNoModRouting{};

/// Add every slot's contribution to `sums`, in slot order.
inline void accumulate(const ActiveModSlot* slots, int n, const ModValues& v, ModSums& sums) noexcept
{
    for (int k = 0; k < n; ++k) {
        const ActiveModSlot& s = slots[k];
        sums[idx(s.dst)] += v[idx(s.src)] * (v[idx(s.via)] * s.amount);
    }
}

} // namespace fss::synth
