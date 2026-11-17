// params.cpp: the mapping between parameter ids and Patch fields (L2.4). One switch per direction; the
// test `params: get(set(x)) == x for every parameter` keeps the two in sync.
#include <fss/synth/params.h>

#include <fss/util/math.h>

namespace fss::synth {

float getParam(const Patch& patch, const ParamInfo& p) noexcept
{
    if (p.op < 0) {
        switch (p.target) {
        case ParamTarget::Algorithm: return static_cast<float>(patch.algorithmNumber);
        case ParamTarget::Output: return util::gainToDb(patch.outputGain);
        default: return p.id == param_id::kTuningA4 ? static_cast<float>(patch.tuningA4.value)
                                                    : static_cast<float>(patch.bendRange.value);
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
        switch (p.target) {
        case ParamTarget::Algorithm: {
            // Swap in the preset matrix but keep each operator's feedback (the diagonal is per-op state).
            patch.algorithmNumber = static_cast<std::uint8_t>(v);
            Algorithm next = kAlgorithms[static_cast<std::size_t>(patch.algorithmNumber - 1)];
            for (std::size_t k = 0; k < kNumOps; ++k)
                next.mod[k][k] = patch.algorithm.mod[k][k];
            patch.algorithm = next;
            return;
        }
        case ParamTarget::Output: patch.outputGain = util::dbToGain(v); return;
        default:
            if (p.id == param_id::kTuningA4)
                patch.tuningA4 = util::Hz{v};
            else
                patch.bendRange = util::Semitones{v};
            return;
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
