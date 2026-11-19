// params.cpp: the mapping between parameter ids and Patch fields (L2.4). One switch per direction; the
// test `params: get(set(x)) == x for every parameter` keeps the two in sync.
#include <fss/synth/params.h>

#include <fss/util/math.h>

#include <initializer_list>

namespace fss::synth {

namespace {
/// Mod matrix slot ids (L4.1): 300 + 10·slot + field. Returns false for any other id.
bool modSlotOf(std::uint32_t id, std::size_t& slot, std::uint32_t& field) noexcept
{
    if (id < 300u || id >= 300u + 10u * static_cast<std::uint32_t>(kNumModSlots) || id % 10u > 3u)
        return false;
    slot = (id - 300u) / 10u;
    field = id % 10u;
    return true;
}
} // namespace

float getParam(const Patch& patch, const ParamInfo& p) noexcept
{
    if (std::size_t slot = 0; p.op < 0) {
        if (std::uint32_t field = 0; modSlotOf(p.id, slot, field)) {
            const ModSlotParams& m = patch.mod.slots[slot];
            switch (field) {
            case 0: return static_cast<float>(m.src);
            case 1: return static_cast<float>(m.via);
            case 2: return static_cast<float>(m.dst);
            default: return m.amountPct;
            }
        }
        const FilterParams& f = patch.filter;
        switch (p.id) {
        case param_id::kAlgorithm: return static_cast<float>(patch.algorithmNumber);
        case param_id::kOutputDb: return patch.outputDb;
        case param_id::kTuningA4: return static_cast<float>(patch.tuningA4.value);
        case param_id::kBendRange: return static_cast<float>(patch.bendRange.value);
        case param_id::kFilterMode: return static_cast<float>(f.mode);
        case param_id::kCutoff: return f.cutoffHz;
        case param_id::kResonance: return f.q;
        case param_id::kFilterEnvAmount: return f.envOctaves;
        case param_id::kKeyTrack: return f.keyTrack;
        case param_id::kFilterVel: return f.velOctaves;
        case param_id::kFilterAttack: return f.env.attackMs;
        case param_id::kFilterDecay: return f.env.decayMs;
        case param_id::kFilterSustain: return f.env.sustain;
        case param_id::kFilterRelease: return f.env.releaseMs;
        case param_id::kLfo1Rate: return patch.lfo1.rateHz;
        case param_id::kLfo1Shape: return static_cast<float>(patch.lfo1.shape);
        case param_id::kLfo1Delay: return patch.lfo1.delayMs;
        case param_id::kLfo1Fade: return patch.lfo1.fadeMs;
        case param_id::kLfo1KeySync: return patch.lfo1.keySync ? 1.0f : 0.0f;
        case param_id::kVibrato: return patch.routes.vibratoSemitones;
        case param_id::kWheelVibrato: return patch.routes.wheelVibratoSemitones;
        case param_id::kLfo2Rate: return patch.lfo2.rateHz;
        case param_id::kLfo2Shape: return static_cast<float>(patch.lfo2.shape);
        case param_id::kLfo2Retrigger: return patch.lfo2.keySync ? 1.0f : 0.0f;
        case param_id::kLfo2Cutoff: return patch.routes.lfo2CutoffOctaves;
        case param_id::kLfo2Index: return patch.routes.lfo2IndexAmount;
        case param_id::kVoiceMode: return static_cast<float>(patch.voice.mode);
        case param_id::kGlide: return patch.voice.glideMs;
        case param_id::kGlideLegato: return patch.voice.glideLegatoOnly ? 1.0f : 0.0f;
        case param_id::kPriority: return static_cast<float>(patch.voice.priority);
        case param_id::kModEnvAttack: return patch.mod.env.attackMs;
        case param_id::kModEnvDecay: return patch.mod.env.decayMs;
        case param_id::kModEnvSustain: return patch.mod.env.sustain;
        case param_id::kModEnvRelease: return patch.mod.env.releaseMs;
        case param_id::kWidthOn: return patch.fx.width.enabled ? 1.0f : 0.0f;
        case param_id::kChorusOn: return patch.fx.chorus.enabled ? 1.0f : 0.0f;
        case param_id::kChorusRate: return patch.fx.chorus.rateHz;
        case param_id::kChorusDepth: return patch.fx.chorus.depthMs;
        case param_id::kChorusDelay: return patch.fx.chorus.delayMs;
        case param_id::kChorusVoices: return static_cast<float>(patch.fx.chorus.voices);
        case param_id::kChorusMix: return patch.fx.chorus.mixPct;
        case param_id::kFlangerOn: return patch.fx.flanger.enabled ? 1.0f : 0.0f;
        case param_id::kFlangerRate: return patch.fx.flanger.rateHz;
        case param_id::kFlangerDelay: return patch.fx.flanger.delayMs;
        case param_id::kFlangerDepth: return patch.fx.flanger.depthMs;
        case param_id::kFlangerFeedback: return patch.fx.flanger.feedbackPct;
        case param_id::kFlangerMix: return patch.fx.flanger.mixPct;
        case param_id::kPhaserOn: return patch.fx.phaser.enabled ? 1.0f : 0.0f;
        case param_id::kPhaserRate: return patch.fx.phaser.rateHz;
        case param_id::kPhaserCenter: return patch.fx.phaser.centerHz;
        case param_id::kPhaserDepth: return patch.fx.phaser.depthOct;
        case param_id::kPhaserFeedback: return patch.fx.phaser.feedbackPct;
        case param_id::kPhaserStages: return static_cast<float>(patch.fx.phaser.stages);
        case param_id::kPhaserMix: return patch.fx.phaser.mixPct;
        case param_id::kDelayOn: return patch.fx.delay.enabled ? 1.0f : 0.0f;
        case param_id::kDelayTime: return patch.fx.delay.timeMs;
        case param_id::kDelaySync: return patch.fx.delay.sync ? 1.0f : 0.0f;
        case param_id::kDelayDivision: return static_cast<float>(patch.fx.delay.division);
        case param_id::kDelayFeedback: return patch.fx.delay.feedbackPct;
        case param_id::kDelayPingPong: return patch.fx.delay.pingPong ? 1.0f : 0.0f;
        case param_id::kDelayDamping: return patch.fx.delay.dampingHz;
        case param_id::kDelayMix: return patch.fx.delay.mixPct;
        case param_id::kDelayTimeMode: return static_cast<float>(patch.fx.delay.timeMode);
        case param_id::kWidth: return patch.fx.width.widthPct;
        default: return p.def;
        }
    }
    const auto i = static_cast<std::size_t>(p.op);
    const OperatorParams& o = patch.ops[i];
    switch (p.field) {
    case OpField::Ratio: return static_cast<float>(o.ratio.value);
    case OpField::Detune: return static_cast<float>(o.detune.value);
    case OpField::Fixed: return o.fixed ? 1.0f : 0.0f;
    case OpField::FixedHz: return static_cast<float>(o.fixedHz.value);
    case OpField::Level: return o.level;
    case OpField::VelSens: return o.velSens;
    case OpField::KeyLevel: return o.keyLevelDbPerOct;
    case OpField::KeyRate: return o.keyRateScale;
    case OpField::Attack: return o.env.attackMs;
    case OpField::Decay: return o.env.decayMs;
    case OpField::Sustain: return o.env.sustain;
    case OpField::Release: return o.env.releaseMs;
    case OpField::Feedback: return patch.algorithm.mod[i][i];
    }
    return p.def;
}

void setParam(Patch& patch, const ParamInfo& p, float plain) noexcept
{
    const float v = clampToRange(p, plain);
    if (std::size_t slot = 0; p.op < 0) {
        if (std::uint32_t field = 0; modSlotOf(p.id, slot, field)) {
            ModSlotParams& m = patch.mod.slots[slot];
            switch (field) {
            case 0: m.src = static_cast<ModSrc>(static_cast<int>(v)); return;
            case 1: m.via = static_cast<ModSrc>(static_cast<int>(v)); return;
            case 2: m.dst = static_cast<ModDst>(static_cast<int>(v)); return;
            default: m.amountPct = v; return;
            }
        }
        FilterParams& f = patch.filter;
        switch (p.id) {
        case param_id::kAlgorithm: {
            // Swap in the preset matrix but keep each operator's feedback (the diagonal is per-op state).
            patch.algorithmNumber = static_cast<std::uint8_t>(v);
            Algorithm next = kAlgorithms[static_cast<std::size_t>(patch.algorithmNumber - 1)];
            for (std::size_t k = 0; k < kNumOps; ++k)
                next.mod[k][k] = patch.algorithm.mod[k][k];
            patch.algorithm = next;
            return;
        }
        case param_id::kOutputDb: patch.outputDb = v; return; // stored as dB: exact (L3.4)
        case param_id::kTuningA4: patch.tuningA4 = util::Hz{v}; return;
        case param_id::kBendRange: patch.bendRange = util::Semitones{v}; return;
        case param_id::kFilterMode: f.mode = static_cast<FilterMode>(static_cast<int>(v)); return;
        case param_id::kCutoff: f.cutoffHz = v; return;
        case param_id::kResonance: f.q = v; return;
        case param_id::kFilterEnvAmount: f.envOctaves = v; return;
        case param_id::kKeyTrack: f.keyTrack = v; return;
        case param_id::kFilterVel: f.velOctaves = v; return;
        case param_id::kFilterAttack: f.env.attackMs = v; return;
        case param_id::kFilterDecay: f.env.decayMs = v; return;
        case param_id::kFilterSustain: f.env.sustain = v; return;
        case param_id::kFilterRelease: f.env.releaseMs = v; return;
        case param_id::kLfo1Rate: patch.lfo1.rateHz = v; return;
        case param_id::kLfo1Shape: patch.lfo1.shape = static_cast<dsp::LfoShape>(static_cast<int>(v)); return;
        case param_id::kLfo1Delay: patch.lfo1.delayMs = v; return;
        case param_id::kLfo1Fade: patch.lfo1.fadeMs = v; return;
        case param_id::kLfo1KeySync: patch.lfo1.keySync = v >= 0.5f; return;
        case param_id::kVibrato: patch.routes.vibratoSemitones = v; return;
        case param_id::kWheelVibrato: patch.routes.wheelVibratoSemitones = v; return;
        case param_id::kLfo2Rate: patch.lfo2.rateHz = v; return;
        case param_id::kLfo2Shape: patch.lfo2.shape = static_cast<dsp::LfoShape>(static_cast<int>(v)); return;
        case param_id::kLfo2Retrigger: patch.lfo2.keySync = v >= 0.5f; return;
        case param_id::kLfo2Cutoff: patch.routes.lfo2CutoffOctaves = v; return;
        case param_id::kLfo2Index: patch.routes.lfo2IndexAmount = v; return;
        case param_id::kVoiceMode: patch.voice.mode = static_cast<VoiceMode>(static_cast<int>(v)); return;
        case param_id::kGlide: patch.voice.glideMs = v; return;
        case param_id::kGlideLegato: patch.voice.glideLegatoOnly = v >= 0.5f; return;
        case param_id::kPriority: patch.voice.priority = static_cast<NotePriority>(static_cast<int>(v)); return;
        case param_id::kModEnvAttack: patch.mod.env.attackMs = v; return;
        case param_id::kModEnvDecay: patch.mod.env.decayMs = v; return;
        case param_id::kModEnvSustain: patch.mod.env.sustain = v; return;
        case param_id::kModEnvRelease: patch.mod.env.releaseMs = v; return;
        case param_id::kWidthOn: patch.fx.width.enabled = v >= 0.5f; return;
        case param_id::kChorusOn: patch.fx.chorus.enabled = v >= 0.5f; return;
        case param_id::kChorusRate: patch.fx.chorus.rateHz = v; return;
        case param_id::kChorusDepth: patch.fx.chorus.depthMs = v; return;
        case param_id::kChorusDelay: patch.fx.chorus.delayMs = v; return;
        case param_id::kChorusVoices: patch.fx.chorus.voices = static_cast<std::uint8_t>(v); return;
        case param_id::kChorusMix: patch.fx.chorus.mixPct = v; return;
        case param_id::kFlangerOn: patch.fx.flanger.enabled = v >= 0.5f; return;
        case param_id::kFlangerRate: patch.fx.flanger.rateHz = v; return;
        case param_id::kFlangerDelay: patch.fx.flanger.delayMs = v; return;
        case param_id::kFlangerDepth: patch.fx.flanger.depthMs = v; return;
        case param_id::kFlangerFeedback: patch.fx.flanger.feedbackPct = v; return;
        case param_id::kFlangerMix: patch.fx.flanger.mixPct = v; return;
        case param_id::kPhaserOn: patch.fx.phaser.enabled = v >= 0.5f; return;
        case param_id::kPhaserRate: patch.fx.phaser.rateHz = v; return;
        case param_id::kPhaserCenter: patch.fx.phaser.centerHz = v; return;
        case param_id::kPhaserDepth: patch.fx.phaser.depthOct = v; return;
        case param_id::kPhaserFeedback: patch.fx.phaser.feedbackPct = v; return;
        case param_id::kPhaserStages: patch.fx.phaser.stages = static_cast<std::uint8_t>(v); return;
        case param_id::kPhaserMix: patch.fx.phaser.mixPct = v; return;
        case param_id::kDelayOn: patch.fx.delay.enabled = v >= 0.5f; return;
        case param_id::kDelayTime: patch.fx.delay.timeMs = v; return;
        case param_id::kDelaySync: patch.fx.delay.sync = v >= 0.5f; return;
        case param_id::kDelayDivision: patch.fx.delay.division = static_cast<std::uint8_t>(v); return;
        case param_id::kDelayFeedback: patch.fx.delay.feedbackPct = v; return;
        case param_id::kDelayPingPong: patch.fx.delay.pingPong = v >= 0.5f; return;
        case param_id::kDelayDamping: patch.fx.delay.dampingHz = v; return;
        case param_id::kDelayMix: patch.fx.delay.mixPct = v; return;
        case param_id::kDelayTimeMode: patch.fx.delay.timeMode = static_cast<fx::DelayTimeMode>(static_cast<int>(v)); return;
        case param_id::kWidth: patch.fx.width.widthPct = v; return;
        default: return;
        }
    }
    const auto i = static_cast<std::size_t>(p.op);
    OperatorParams& o = patch.ops[i];
    switch (p.field) {
    case OpField::Ratio: o.ratio = util::Ratio{v}; break;
    case OpField::Detune: o.detune = util::Cents{v}; break;
    case OpField::Fixed: o.fixed = v >= 0.5f; break;
    case OpField::FixedHz: o.fixedHz = util::Hz{v}; break;
    case OpField::Level: o.level = v; break;
    case OpField::VelSens: o.velSens = v; break;
    case OpField::KeyLevel: o.keyLevelDbPerOct = v; break;
    case OpField::KeyRate: o.keyRateScale = v; break;
    case OpField::Attack: o.env.attackMs = v; break;
    case OpField::Decay: o.env.decayMs = v; break;
    case OpField::Sustain: o.env.sustain = v; break;
    case OpField::Release: o.env.releaseMs = v; break;
    case OpField::Feedback: patch.algorithm.mod[i][i] = v; break;
    }
}

std::string_view valueLabel(const ParamInfo& p, int value) noexcept
{
    // Index into a fixed list, or "" if out of range (a corrupt value must not read past the end).
    auto pick = [value](std::initializer_list<std::string_view> names) -> std::string_view {
        return value >= 0 && static_cast<std::size_t>(value) < names.size() ? names.begin()[value] : std::string_view{};
    };
    if (p.skew != Skew::Stepped)
        return {};
    if (std::size_t slot = 0; p.op < 0) {
        if (std::uint32_t field = 0; modSlotOf(p.id, slot, field) && field <= 2) {
            if (field == 2)
                return value >= 0 && value < kNumModDests ? kModDests[static_cast<std::size_t>(value)].name : std::string_view{};
            return value >= 0 && value < kNumModSources ? kModSources[static_cast<std::size_t>(value)].name : std::string_view{};
        }
    }
    switch (p.id) {
    case param_id::kAlgorithm:
        return value >= 1 && value <= static_cast<int>(kAlgorithms.size())
                   ? std::string_view(kAlgorithms[static_cast<std::size_t>(value - 1)].name)
                   : std::string_view{};
    case param_id::kFilterMode: return pick({"Off", "Low-pass", "Band-pass", "High-pass", "Notch"});
    case param_id::kLfo1Shape:
    case param_id::kLfo2Shape:
        return pick({"Sine", "Triangle", "Saw up", "Saw down", "Square", "Sample & hold", "Smooth random"});
    case param_id::kVoiceMode: return pick({"Poly", "Mono", "Legato"});
    case param_id::kPriority: return pick({"Last", "Low", "High"});
    case param_id::kDelayDivision:
        return value >= 0 && value < fx::kNumDelayDivisions ? fx::kDelayDivisions[static_cast<std::size_t>(value)].name
                                                             : std::string_view{};
    case param_id::kDelayTimeMode: return pick({"Clean", "Tape"});
    default: break;
    }
    if (p.unit == "on/off")
        return pick({"Off", "On"});
    return {};
}

} // namespace fss::synth
