// engine.cpp: fss_core's first .cpp. The Engine is big enough, and stable enough as an API, that its
// implementation belongs out of the header (faster builds for every host that includes engine.h).
#include <fss/engine.h>

#include <fss/synth/params.h>
#include <fss/util/realtime_guard.h>

#include <algorithm>
#include <cmath>

namespace fss {

void Engine::prepare(double sampleRate, int /*maxBlockSize*/, float stealFadeMs) noexcept
{
    // maxBlockSize is part of the contract (hosts promise never to exceed it) for buffers that later
    // lessons will preallocate here. Today's per-sample voices need none.
    // Smoothing times come from the parameter table, the single source of truth (L2.4).
    using synth::param_id::op;
    const float levelMs = smoothing_ ? synth::findParam(op(0, synth::OpField::Level))->smoothingMs : 0.0f;
    const float matrixMs = smoothing_ ? synth::findParam(synth::param_id::kAlgorithm)->smoothingMs : 0.0f;
    const float gainMs = smoothing_ ? synth::findParam(synth::param_id::kOutputDb)->smoothingMs : 0.0f;
    const float cutoffMs = smoothing_ ? synth::findParam(synth::param_id::kCutoff)->smoothingMs : 0.0f;
    const float qMs = smoothing_ ? synth::findParam(synth::param_id::kResonance)->smoothingMs : 0.0f;
    for (synth::Voice& v : voices_)
        v.prepare(sampleRate, stealFadeMs, levelMs);
    matrixK_ = matrixMs > 0.0f ? static_cast<float>(1.0 - std::exp(-1.0 / (matrixMs * 0.001 * sampleRate))) : 1.0f;
    gain_.prepare(sampleRate, gainMs);
    cutoffOctaves_.prepare(sampleRate, cutoffMs);
    q_.prepare(sampleRate, qMs);
    reset();
}

void Engine::setPatch(const synth::Patch& patch) noexcept
{
    patch_ = patch;
    live_ = patch.algorithm;
    liveSettling_ = false;
    gain_.reset(patch.outputGain);
    cutoffOctaves_.reset(std::log2(patch.filter.cutoffHz));
    q_.reset(patch.filter.q);
}

void Engine::reset() noexcept
{
    for (synth::Voice& v : voices_)
        v.reset();
    bend_ = 0.0f;
    noteCounter_ = steals_ = 0;
    live_ = patch_.algorithm;
    liveSettling_ = false;
    gain_.reset(patch_.outputGain);
    cutoffOctaves_.reset(std::log2(patch_.filter.cutoffHz));
    q_.reset(patch_.filter.q);
}

void Engine::setPolyphony(int voices) noexcept
{
    polyphony_ = std::clamp(voices, 1, kMaxPolyphony);
}

int Engine::busyVoices() const noexcept
{
    return static_cast<int>(std::count_if(voices_.begin(), voices_.end(), [](const synth::Voice& v) { return v.busy(); }));
}

void Engine::process(std::span<const Event> events, float* outL, float* outR, int numFrames) noexcept
{
    // Everything below must obey docs/RT-RULES.md. The guard lets the allocation trap (L2.2) check it.
    const util::RealtimeGuard rt;
    // The block-splitting loop: render up to each event, apply it, continue. Events at the same frame are
    // applied in list order with nothing rendered between them.
    int pos = 0;
    for (const Event& e : events) {
        const int at = std::clamp(static_cast<int>(e.frame), pos, numFrames);
        render(outL, outR, pos, at);
        handle(e);
        pos = at;
    }
    render(outL, outR, pos, numFrames);
}

void Engine::handle(const Event& e) noexcept
{
    switch (e.type) {
    case Event::Type::NoteOn:
        // MIDI convention: a note-on with velocity 0 is a note-off.
        if (e.value > 0.0f)
            noteOn(e);
        else
            noteOff(e);
        break;
    case Event::Type::NoteOff:
        noteOff(e);
        break;
    case Event::Type::PitchBend:
        bend_ = std::clamp(e.value, -1.0f, 1.0f);
        for (synth::Voice& v : voices_)
            if (v.busy())
                v.setBend(bend_);
        break;
    case Event::Type::AllNotesOff:
        for (synth::Voice& v : voices_)
            v.release();
        break;
    case Event::Type::ParamChange:
        paramChange(e);
        break;
    }
}

void Engine::paramChange(const Event& e) noexcept
{
    const synth::ParamInfo* info = synth::findParam(e.paramId);
    if (!info)
        return; // unknown id (e.g. from a newer preset): ignore rather than guess
    synth::setParam(patch_, *info, e.value);
    // The patch now holds the new target. Who has to react, and how (see ParamTarget):
    switch (info->target) {
    case synth::ParamTarget::OpLevel:
        for (synth::Voice& v : voices_)
            if (v.busy())
                v.refreshLevels(); // glides via each operator's smoother
        break;
    case synth::ParamTarget::OpFreq:
    case synth::ParamTarget::Tuning:
        for (synth::Voice& v : voices_)
            if (v.busy())
                v.refreshFrequencies(); // phase is continuous, so a pitch change never clicks
        break;
    case synth::ParamTarget::OpEnv:
    case synth::ParamTarget::FilterEnv:
        for (synth::Voice& v : voices_)
            if (v.busy())
                v.refreshEnvelopes();
        break;
    case synth::ParamTarget::Feedback:
    case synth::ParamTarget::Algorithm:
        liveSettling_ = true; // render() glides live_ toward patch_.algorithm
        break;
    case synth::ParamTarget::Output:
        gain_.setTarget(patch_.outputGain);
        break;
    case synth::ParamTarget::FilterCutoff:
        // Smoothed in octaves, not Hz: a glide from 100 Hz to 10 kHz then moves evenly *as heard*,
        // instead of spending almost all of its time in the top octave.
        cutoffOctaves_.setTarget(std::log2(patch_.filter.cutoffHz));
        break;
    case synth::ParamTarget::FilterQ:
        q_.setTarget(patch_.filter.q);
        break;
    case synth::ParamTarget::Filter:
        break; // voices read mode / amounts from the patch every sample
    }
}

void Engine::noteOn(const Event& e) noexcept
{
    // 1. Same key already sounding (held or releasing): fade it out and start a fresh voice, like a piano
    //    hammer re-striking. Restarting it in place would reset its phases mid-waveform: a click.
    for (synth::Voice& v : voices_)
        if (v.busy() && !v.fading() && v.key() == e.key)
            v.kill();

    // 2. Over the polyphony limit (counting voices that aren't already fading)? Steal one.
    const int sounding = static_cast<int>(
        std::count_if(voices_.begin(), voices_.end(), [](const synth::Voice& v) { return v.busy() && !v.fading(); }));
    if (sounding >= polyphony_) {
        if (synth::Voice* victim = chooseVictim()) {
            victim->kill();
            ++steals_;
        }
    }

    // 3. A free slot. With 4 slots of headroom one almost always exists; if more than 4 steals happen
    //    within one fade time, cut the quietest fading voice (a rare, deliberate last resort).
    synth::Voice* slot = findFreeVoice();
    if (!slot) {
        auto quietest = std::min_element(voices_.begin(), voices_.end(), [](const synth::Voice& a, const synth::Voice& b) {
            return a.fadeGain() < b.fadeGain();
        });
        slot = &*quietest;
        slot->reset();
    }
    const synth::NoteContext ctx{.key = e.key, .velocity = std::clamp(e.value, 0.0f, 1.0f), .bend = bend_};
    slot->start(ctx, patch_, ++noteCounter_, e.noteId);
}

void Engine::noteOff(const Event& e) noexcept
{
    for (synth::Voice& v : voices_)
        if (v.busy() && !v.released() && !v.fading() && v.key() == e.key && (e.noteId < 0 || v.noteId() == e.noteId))
            v.release();
}

synth::Voice* Engine::findFreeVoice() noexcept
{
    for (synth::Voice& v : voices_)
        if (!v.busy())
            return &v;
    return nullptr;
}

synth::Voice* Engine::chooseVictim() noexcept
{
    // Steal policy (D8): prefer a RELEASED voice (the player already let go; it's only a tail), oldest
    // first; if every voice is held, take the OLDEST held note (the one the ear has had longest to accept).
    synth::Voice* best = nullptr;
    for (synth::Voice& v : voices_) {
        if (!v.busy() || v.fading())
            continue;
        if (!best || (v.released() && !best->released()) ||
            (v.released() == best->released() && v.age() < best->age()))
            best = &v;
    }
    return best;
}

void Engine::render(float* outL, float* outR, int from, int to) noexcept
{
    if (from >= to)
        return;
    // Sample by sample (since L2.4), because the modulation matrix can be gliding: every voice must see
    // the same, current live_ matrix on each sample. Voices are summed in a fixed order, so the result
    // does not depend on where blocks start (block-size invariance).
    for (int i = from; i < to; ++i) {
        if (liveSettling_)
            settleMatrix();
        const synth::VoiceControl ctl{.alg = &live_, .cutoffOctaves = cutoffOctaves_.tick(), .q = q_.tick()};
        float sum = 0.0f;
        for (synth::Voice& v : voices_)
            if (v.busy())
                sum += v.tick(ctl);
        const float y = softClip(gain_.tick() * sum);
        outL[i] = y;
        outR[i] = y; // mono voices for now; stereo spread comes with unison (L4.4)
    }
}

void Engine::settleMatrix() noexcept
{
    // One-pole glide of all 20 matrix entries toward the patch. Switching algorithm therefore MORPHS
    // the routing over ~50 ms instead of jumping (a jump in a modulation amount is a jump in phase:
    // a click). When everything is within 1e-6, snap exactly and stop paying for it.
    float maxDiff = 0.0f;
    for (std::size_t s = 0; s < synth::kNumOps; ++s) {
        for (std::size_t d = 0; d < synth::kNumOps; ++d) {
            float& m = live_.mod[s][d];
            const float diff = patch_.algorithm.mod[s][d] - m;
            m += diff * matrixK_;
            maxDiff = std::max(maxDiff, std::abs(diff));
        }
        float& c = live_.carrier[s];
        const float diff = patch_.algorithm.carrier[s] - c;
        c += diff * matrixK_;
        maxDiff = std::max(maxDiff, std::abs(diff));
    }
    if (maxDiff < 1e-6f) {
        live_ = patch_.algorithm;
        liveSettling_ = false;
    }
}

float softClip(float x) noexcept
{
    constexpr float knee = 0.8f;
    const float a = std::abs(x);
    if (a <= knee)
        return x;
    // Above the knee: tanh maps [0, ∞) → [0, 1) with slope 1 at 0, so the curve joins the straight line
    // smoothly and approaches ±1 (mathematically never reaching it; in float, large inputs round to exactly ±1).
    const float y = knee + (1.0f - knee) * std::tanh((a - knee) / (1.0f - knee));
    return std::copysign(y, x);
}

} // namespace fss
