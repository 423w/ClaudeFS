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
    lfo2_.prepare(sampleRate);
    lfo1Reference_.prepare(sampleRate);
    // A 7-bit mod wheel moves in steps of 1/127; 10 ms smoothing turns them into a glide (like L2.4's knobs).
    modWheel_.prepare(sampleRate, smoothing_ ? 10.0f : 0.0f);
    // Pitch bend: 3 ms is short enough to feel instant under the thumb, long enough to round a 7-bit
    // staircase (a cheap controller sends 128 steps; at ±12 st each step is a 0.19 st jump).
    bendSmoothed_.prepare(sampleRate, smoothing_ ? 3.0f : 0.0f);
    sampleRate_ = sampleRate;
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
    restartLfos();
}

void Engine::restartLfos() noexcept
{
    lfo2_.setRate(patch_.lfo2.rateHz);
    lfo2_.restart(0, 0x5EED1234u);
    lfo1Reference_.setFrequency(patch_.lfo1.rateHz);
    lfo1Reference_.reset(0);
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
    modWheel_.reset(0.0f);
    bendSmoothed_.reset(0.0f);
    held_.clear();
    sustainPedal_ = false;
    monoVoice_ = nullptr;
    hasLastPitch_ = false;
    restartLfos();
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
        // Since L2.5 the bend is a per-sample pitch OFFSET (smoothed here, applied in every voice with glide
        // and vibrato) instead of being baked into each voice's base frequency on every bend message.
        bend_ = std::clamp(e.value, -1.0f, 1.0f);
        bendSmoothed_.setTarget(bend_);
        break;
    case Event::Type::AllNotesOff:
        allNotesOff();
        break;
    case Event::Type::ParamChange:
        paramChange(e);
        break;
    case Event::Type::Controller:
        controller(e);
        break;
    }
}

void Engine::controller(const Event& e) noexcept
{
    if (e.key == 1) { // CC 1: mod wheel → vibrato depth (patch.routes.wheelVibratoSemitones)
        modWheel_.setTarget(std::clamp(e.value, 0.0f, 1.0f));
    } else if (e.key == 64) { // CC 64: sustain pedal, down at ≥ 64/127 (the MIDI convention)
        const bool down = e.value >= 0.5f;
        if (down == sustainPedal_)
            return;
        sustainPedal_ = down;
        if (!down) // pedal up: every note the pedal was holding is released now
            for (synth::Voice& v : voices_)
                if (v.busy() && v.sustained())
                    v.release();
    }
}

void Engine::allNotesOff() noexcept
{
    // Panic: release everything, pedal-held notes included, and forget which keys were down.
    for (synth::Voice& v : voices_)
        v.release();
    held_.clear();
}

int Engine::glideSamples() const noexcept
{
    return static_cast<int>(patch_.voice.glideMs * 0.001 * sampleRate_);
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
    case synth::ParamTarget::Lfo:
        break; // voices and render() read these from the patch every sample (or at the next note)
    case synth::ParamTarget::VoiceMode:
        // Switching poly ↔ mono mid-performance: release what's sounding and start clean, rather than
        // guess how 5 held poly voices should map onto one mono voice.
        allNotesOff();
        monoVoice_ = nullptr;
        break;
    case synth::ParamTarget::Play:
        break; // glide time/mode and note priority are read at the next note event
    case synth::ParamTarget::LfoRate:
        lfo2_.setRate(patch_.lfo2.rateHz);
        lfo1Reference_.setFrequency(patch_.lfo1.rateHz);
        for (synth::Voice& v : voices_)
            if (v.busy())
                v.refreshLfo();
        break;
    }
}

void Engine::noteOn(const Event& e) noexcept
{
    const float velocity = std::clamp(e.value, 0.0f, 1.0f);
    held_.press(e.key, velocity);
    if (patch_.voice.mode != synth::VoiceMode::Poly) {
        monoUpdate(true, e.key, e.noteId);
        return;
    }
    // Poly: every key gets its own voice. Glide (if on) starts from the previous note's pitch; in
    // "legato only" glide mode only when another key is still held (a connected phrase).
    const bool otherKeyHeld = held_.size() > 1;
    synth::Voice* v = startVoice(e.key, velocity, e.noteId);
    const int glide = glideSamples();
    if (glide > 0 && hasLastPitch_ && (!patch_.voice.glideLegatoOnly || otherKeyHeld))
        v->glideFrom(lastPitch_ - static_cast<float>(e.key), glide);
    lastPitch_ = static_cast<float>(e.key);
    hasLastPitch_ = true;
}

void Engine::monoUpdate(bool pressed, std::uint8_t pressedKey, std::int32_t noteId) noexcept
{
    // Mono / legato (L2.5). Called whenever the set of held keys changed. One voice plays the note the
    // priority rule picks from the held keys; this function makes the sounding note agree with that pick.
    synth::Voice* v = (monoVoice_ && monoVoice_->busy() && !monoVoice_->fading()) ? monoVoice_ : nullptr;
    if (held_.empty()) { // last key up: release, unless the pedal holds the note
        if (v) {
            if (sustainPedal_)
                v->sustain();
            else
                v->release();
        }
        return;
    }
    const synth::HeldNotes::Note target = held_.pick(patch_.voice.priority);
    const bool gateOn = v && !v->released(); // a note is sounding (held, or held by the pedal)
    const bool mono = patch_.voice.mode == synth::VoiceMode::Mono;
    // Mono (retrigger) restarts the note for every new key press that becomes the target, even the same key.
    const bool retriggerPress = mono && pressed && target.key == pressedKey;
    if (gateOn && v->key() == target.key && !retriggerPress) {
        v->hold(); // e.g. a key pressed that loses on priority: nothing to change
        return;
    }
    const int glideLen = glideSamples();
    const bool glide = glideLen > 0 && hasLastPitch_ && (!patch_.voice.glideLegatoOnly || gateOn);
    if (gateOn && !mono) { // legato: same voice, envelopes untouched, pitch slides or jumps
        v->legatoTo(target.key, glide ? glideLen : 0);
        v->hold();
        lastPitch_ = static_cast<float>(target.key);
        return;
    }
    // Retrigger: a fresh voice (envelopes from the start); the old one fades out in its slot (no click).
    const float from = v ? v->pitch() : lastPitch_;
    for (synth::Voice& other : voices_)
        if (other.busy() && !other.fading())
            other.kill(); // mono means one voice: also stop a previous note's release tail
    synth::Voice* s = startVoice(target.key, target.velocity, noteId);
    if (glide)
        s->glideFrom(from - static_cast<float>(target.key), glideLen);
    monoVoice_ = s;
    lastPitch_ = static_cast<float>(target.key);
    hasLastPitch_ = true;
}

synth::Voice* Engine::startVoice(std::uint8_t key, float velocity, std::int32_t noteId) noexcept
{
    // 1. Same key already sounding (held or releasing): fade it out and start a fresh voice, like a piano
    //    hammer re-striking. Restarting it in place would reset its phases mid-waveform: a click.
    for (synth::Voice& v : voices_)
        if (v.busy() && !v.fading() && v.key() == key)
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
    if (patch_.lfo2.keySync) // LFO 2 "retrigger": the global wobble restarts with each new note
        lfo2_.restart(0, static_cast<std::uint32_t>(noteCounter_ + 1));
    // bend = 0: the bend reaches the voice every sample as a pitch offset (VoiceControl), not at note start.
    const synth::NoteContext ctx{.key = key, .velocity = velocity, .bend = 0.0f};
    slot->start(ctx, patch_, ++noteCounter_, noteId, lfo1Reference_.phase());
    return slot;
}

void Engine::noteOff(const Event& e) noexcept
{
    const bool wasHeld = held_.release(e.key);
    if (patch_.voice.mode != synth::VoiceMode::Poly) {
        if (wasHeld)
            monoUpdate(false, e.key, e.noteId);
        return;
    }
    for (synth::Voice& v : voices_)
        if (v.busy() && !v.released() && !v.fading() && v.key() == e.key && (e.noteId < 0 || v.noteId() == e.noteId)) {
            if (sustainPedal_)
                v.sustain(); // the pedal holds it; released at pedal-up
            else
                v.release();
        }
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
    // Steal policy (D8, refined in L2.5): prefer a RELEASED voice (the player let go; it's only a tail), then
    // one held only by the SUSTAIN PEDAL (also let go, just still ringing), and only then a key still held
    // down. Within a rank, the OLDEST (the one the ear has had longest to accept).
    auto rank = [](const synth::Voice& v) { return v.released() ? 0 : v.sustained() ? 1 : 2; };
    synth::Voice* best = nullptr;
    for (synth::Voice& v : voices_) {
        if (!v.busy() || v.fading())
            continue;
        if (!best || rank(v) < rank(*best) || (rank(v) == rank(*best) && v.age() < best->age()))
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
        // Global modulation, computed once per sample for all voices (L1.11).
        (void)lfo1Reference_.tick();
        lfo2_.setShape(patch_.lfo2.shape);
        const float lfo2 = lfo2_.tick();
        const synth::ModRoutes& routes = patch_.routes;
        const synth::Algorithm* alg = &live_;
        if (routes.lfo2IndexAmount != 0.0f) {
            // LFO 2 → FM index: scale every modulation amount (feedback too). It's global, so one scaled
            // copy of the 16-entry matrix serves every voice. Never below 0 (that would invert the phase).
            const float k = std::max(0.0f, 1.0f + lfo2 * routes.lfo2IndexAmount);
            indexScaled_ = live_;
            for (auto& row : indexScaled_.mod)
                for (float& m : row)
                    m *= k;
            alg = &indexScaled_;
        }
        const synth::VoiceControl ctl{.alg = alg,
                                      .cutoffOctaves = cutoffOctaves_.tick() + lfo2 * routes.lfo2CutoffOctaves,
                                      .q = q_.tick(),
                                      .modWheel = modWheel_.tick(),
                                      .bendSemitones = bendSmoothed_.tick() * static_cast<float>(patch_.bendRange.value)};
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
