// engine.cpp: fss_core's first .cpp. The Engine is big enough, and stable enough as an API, that its
// implementation belongs out of the header (faster builds for every host that includes engine.h).
#include <fss/engine.h>

#include <algorithm>
#include <cmath>

namespace fss {

void Engine::prepare(double sampleRate, int /*maxBlockSize*/, float stealFadeMs) noexcept
{
    // maxBlockSize is part of the contract (hosts promise never to exceed it) for buffers that later
    // lessons will preallocate here. Today's per-sample voices need none.
    for (synth::Voice& v : voices_)
        v.prepare(sampleRate, stealFadeMs);
    reset();
}

void Engine::reset() noexcept
{
    for (synth::Voice& v : voices_)
        v.reset();
    bend_ = 0.0f;
    noteCounter_ = steals_ = 0;
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
    std::fill(outL + from, outL + to, 0.0f);
    for (synth::Voice& v : voices_) {
        if (!v.busy())
            continue;
        for (int i = from; i < to && v.busy(); ++i)
            outL[i] += v.tick();
    }
    const float gain = patch_.outputGain;
    for (int i = from; i < to; ++i) {
        const float y = softClip(gain * outL[i]);
        outL[i] = y;
        outR[i] = y; // mono voices for now; stereo spread comes with unison (L4.4)
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
