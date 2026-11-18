// params.cpp: the mapping between parameter ids and Patch fields (L2.4). One switch per direction; the
// test `params: get(set(x)) == x for every parameter` keeps the two in sync.
#include <fss/synth/params.h>

#include <fss/util/math.h>

namespace fss::synth {

float getParam(const Patch& patch, const ParamInfo& p) noexcept
{
    if (p.op < 0) {
        const FilterParams& f = patch.filter;
        switch (p.id) {
        case param_id::kAlgorithm: return static_cast<float>(patch.algorithmNumber);
        case param_id::kOutputDb: return util::gainToDb(patch.outputGain);
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
    if (p.op < 0) {
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
        case param_id::kOutputDb: patch.outputGain = util::dbToGain(v); return;
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

} // namespace fss::synth
