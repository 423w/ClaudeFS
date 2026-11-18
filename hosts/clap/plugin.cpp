// plugin.cpp: FssClap, CFS's CLAP plugin (L3.2). A thin adapter: it translates CLAP's world (C function
// tables, clap_event_* structs, host buffers, byte streams) into fss::Engine's (fss::Event, float arrays,
// synth::Patch) and back. No DSP here (ARCHITECTURE.md §2).
//
// Reading order: the trampolines at the bottom first (how a C host reaches a C++ object), then process()
// and runBlock() (the audio path), then the parameter sync (main thread side).
#include "plugin.h"

#include "plugin_instance.h"
#include "state_v0.h"

#include "midi_message.h"
#include "midi_stream.h"

#include <fss/synth/params.h>
#include <fss/synth/patch_io.h>
#include <fss/util/realtime_guard.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace fss::plugin {

namespace {

const char* const kFeatures[] = {CLAP_PLUGIN_FEATURE_INSTRUMENT, CLAP_PLUGIN_FEATURE_SYNTHESIZER,
                                 CLAP_PLUGIN_FEATURE_STEREO, nullptr};

// Output events: tiny helpers that fill the C structs. try_push may fail if the host's queue is full;
// there's nothing useful to do about that on the audio thread, so the result is ignored.
void pushParamValue(const clap_output_events* out, std::uint32_t time, std::size_t index, float plain,
                    std::uint32_t flags = 0) noexcept
{
    if (!out)
        return;
    const synth::ParamInfo& p = synth::kParams[index];
    clap_event_param_value ev{};
    ev.header = {sizeof ev, time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, flags};
    ev.param_id = p.id;
    ev.cookie = const_cast<synth::ParamInfo*>(&p);
    ev.note_id = ev.port_index = ev.channel = ev.key = -1; // -1 = "every note": a global parameter change
    ev.value = toHost(p, plain);
    (void)out->try_push(out, &ev.header);
}

void pushGesture(const clap_output_events* out, std::uint16_t type, std::size_t index) noexcept
{
    if (!out)
        return;
    clap_event_param_gesture ev{};
    ev.header = {sizeof ev, 0, CLAP_CORE_EVENT_SPACE_ID, type, 0};
    ev.param_id = synth::kParams[index].id;
    (void)out->try_push(out, &ev.header);
}

// Streams: the host's read/write may move fewer bytes than asked (clap-validator tests exactly that with
// "a small prime number of bytes at a time"), so both loop until done.
bool writeAll(const clap_ostream* s, const std::vector<std::byte>& bytes)
{
    std::size_t done = 0;
    while (done < bytes.size()) {
        const std::int64_t n = s->write(s, bytes.data() + done, bytes.size() - done);
        if (n <= 0)
            return false;
        done += static_cast<std::size_t>(n);
    }
    return true;
}

bool readAll(const clap_istream* s, std::vector<std::byte>& bytes, std::size_t limit)
{
    std::byte chunk[4096];
    for (;;) {
        const std::int64_t n = s->read(s, chunk, sizeof chunk);
        if (n == 0)
            return true; // end of stream
        if (n < 0)
            return false;
        bytes.insert(bytes.end(), chunk, chunk + n);
        if (bytes.size() > limit)
            return false; // far bigger than any CFS state: corrupt or not ours
    }
}

} // namespace

// =====================================================================================================
// Construction, lifecycle [main]
// =====================================================================================================

FssClap::FssClap(const clap_host* host) : host_(host)
{
    for (std::size_t i = 0; i < kNumParams; ++i)
        mainValues_[i].store(synth::getParam(mirror_, synth::kParams[i]), std::memory_order_relaxed);
    lastChannel_.fill(0);
}

bool FssClap::init()
{
    // Host extensions may only be queried from init() on (not in the constructor: create_plugin may run on
    // any thread, and the host isn't required to be ready yet).
    hostParams_ = static_cast<const clap_host_params*>(host_->get_extension(host_, CLAP_EXT_PARAMS));
    hostLog_ = static_cast<const clap_host_log*>(host_->get_extension(host_, CLAP_EXT_LOG));
    if (Trace::enabled())
        Trace::log(this, "init: host \"%s\" %s (CLAP %u.%u.%u), CFS %s", host_->name ? host_->name : "?",
                   host_->version ? host_->version : "?", host_->clap_version.major, host_->clap_version.minor,
                   host_->clap_version.revision, FSS_VERSION_STRING);
    return true;
}

void FssClap::destroy()
{
    Trace::log(this, "destroy");
    delete this;
}

bool FssClap::activate(double sampleRate, std::uint32_t minFrames, std::uint32_t maxFrames)
{
    Trace::log(this, "activate: %.1f Hz, frames %u..%u", sampleRate, minFrames, maxFrames);
    if (!(sampleRate > 0.0) || !std::isfinite(sampleRate))
        return false;
    pollAudio(); // the mirror is the truth while inactive: make it complete first
    engine_.prepare(sampleRate, static_cast<int>(std::min<std::uint32_t>(maxFrames, 1u << 20)));
    engine_.setPatch(mirror_);
    snap_.fill({});
    // toAudio_ is NOT drained: UI changes made while inactive are already in mirror_ (and so in the Engine),
    // and the first process() still turns them into out_events, so the host learns about them.
    active_ = true;
    return true;
}

void FssClap::deactivate()
{
    active_ = false;
    Trace::log(this, "deactivate");
    Trace::logStats(this, stats_);
    stats_ = {};
}

void FssClap::reset() noexcept
{
    engine_.reset();
    snap_.fill({}); // the host knows reset() ends every note: no NOTE_END needed
}

// =====================================================================================================
// The audio path [audio]
// =====================================================================================================

clap_process_status FssClap::process(const clap_process* p) noexcept
{
    // Everything from here to the end obeys docs/RT-RULES.md; in Debug builds the allocation trap checks it.
    const util::RealtimeGuard rt;
    const std::uint32_t frames = p->frames_count;
    ++stats_.processCalls;
    stats_.zeroFrameCalls += frames == 0 ? 1 : 0;
    stats_.minFrames = std::min(stats_.minFrames, frames);
    stats_.maxFrames = std::max(stats_.maxFrames, frames);

    // Find our stereo output. Hosts may hand us no outputs at all, or fewer channels than we declared
    // (never more, since we said 2): whatever is missing is rendered into scratch and thrown away.
    float* outL = nullptr;
    float* outR = nullptr;
    clap_audio_buffer* bus = (p->audio_outputs_count >= 1 && p->audio_outputs) ? &p->audio_outputs[0] : nullptr;
    if (bus && bus->data32 && bus->channel_count >= 1) {
        outL = bus->data32[0];
        outR = bus->channel_count >= 2 ? bus->data32[1] : nullptr;
    }
    if (!outL)
        ++stats_.noBuffers;

    runBlock(p->in_events, p->out_events, frames, outL, outR);

    // Silence detection, for the host: CLAP_PROCESS_SLEEP means "call me again only when there are events",
    // which saves CPU for every idle instance in a big project. Only when nothing sounds AND the block was
    // exactly zero: an ending release tail must finish first.
    float peak = 0.0f;
    if (outL)
        for (std::uint32_t i = 0; i < frames; ++i)
            peak = std::max(peak, std::abs(outL[i]));
    if (outR)
        for (std::uint32_t i = 0; i < frames; ++i)
            peak = std::max(peak, std::abs(outR[i]));
    const bool silent = peak == 0.0f && engine_.busyVoices() == 0;
    if (bus)
        bus->constant_mask = (silent && outL) ? 0x3u : 0u; // bit per channel: "every sample equals the first"

    peak_.store(std::max(peak, peak_.load(std::memory_order_relaxed) * 0.9f), std::memory_order_relaxed);
    voices_.store(engine_.busyVoices(), std::memory_order_relaxed);
    return silent ? CLAP_PROCESS_SLEEP : CLAP_PROCESS_CONTINUE;
}

void FssClap::paramsFlush(const clap_input_events* in, const clap_output_events* out) noexcept
{
    // [active ? audio-thread : main-thread]: the same code either way. While inactive the main thread owns
    // the Engine (there is no audio thread), so applying changes to it directly is safe.
    const util::RealtimeGuard rt;
    ++stats_.flushCalls;
    runBlock(in, out, 0, nullptr, nullptr);
}

void FssClap::runBlock(const clap_input_events* in, const clap_output_events* out, std::uint32_t frames,
                       float* outL, float* outR) noexcept
{
    numEvents_ = 0;
    touched_.reset();

    // 1. Changes from the editor (and state loads) go first, at frame 0. A pending resync waits until the
    //    queue is EMPTY: the queue holds older values than mainValues_, so applying the resync first and the
    //    queue's leftovers after it would end on a stale value. (A unit test caught exactly that.)
    if (drainUi(out) && resync_.exchange(false, std::memory_order_acquire))
        resyncFromMain(out);

    // 2. The host's events, already sorted by time. Each becomes 0..n fss::Events at the same frame. If our
    //    fixed event list fills up (thousands of events in one block), render up to that event and start
    //    a new list: block-size invariance (L1.7) makes the split inaudible, bit for bit.
    std::uint32_t start = 0; // first frame of the current sub-block
    const std::uint32_t count = in ? in->size(in) : 0;
    for (std::uint32_t i = 0; i < count; ++i) {
        const clap_event_header* h = in->get(in, i);
        if (!h)
            continue;
        // Defensive: a time past the block (a host bug) is applied at the last frame; never before `start`.
        const std::uint32_t t = std::max(start, frames ? std::min(h->time, frames - 1) : 0u);
        if (numEvents_ >= kSplitAt) {
            renderEngine(outL ? outL + start : nullptr, outR ? outR + start : nullptr, t - start);
            start = t;
            ++stats_.eventChunks;
        }
        translate(h, t - start, t, out);
    }
    renderEngine(outL ? outL + start : nullptr, outR ? outR + start : nullptr, frames - start);

    // 3. Tell the main thread what changed here (host automation and editor changes alike: the audio side
    //    is the truth, the mirror follows it).
    for (std::size_t i = 0; i < kNumParams; ++i)
        if (touched_[i]) {
            fromAudio_[i].store(synth::getParam(engine_.patch(), synth::kParams[i]), std::memory_order_relaxed);
            fromAudioDirty_[i].store(true, std::memory_order_release);
        }

    // 4. Tell the host which notes ended.
    reportNoteEnds(out, frames);
}

bool FssClap::drainUi(const clap_output_events* out) noexcept
{
    // At most 256 messages per block, so the event list keeps room for the host's events; the rest
    // simply wait for the next block (a few ms).
    UiMsg m;
    for (int n = 0; n < 256; ++n) {
        if (!toAudio_.pop(m))
            return true;
        switch (m.kind) {
        case UiMsg::Kind::Begin: pushGesture(out, CLAP_EVENT_PARAM_GESTURE_BEGIN, m.index); break;
        case UiMsg::Kind::End: pushGesture(out, CLAP_EVENT_PARAM_GESTURE_END, m.index); break;
        case UiMsg::Kind::Value:
            applyParam(m.index, m.plain, 0);
            if (m.notifyHost)
                pushParamValue(out, 0, m.index, m.plain);
            uiPending_[m.index].fetch_sub(1, std::memory_order_release);
            break;
        }
    }
    return false; // stopped at the limit: more messages may be waiting
}

void FssClap::resyncFromMain(const clap_output_events* out) noexcept
{
    // The queue overflowed at some point: compare every parameter with the main thread's latest value.
    for (std::size_t i = 0; i < kNumParams; ++i) {
        const float want = mainValues_[i].load(std::memory_order_relaxed);
        if (want != synth::getParam(engine_.patch(), synth::kParams[i])) {
            applyParam(i, want, 0);
            pushParamValue(out, 0, i, want);
        }
    }
}

void FssClap::applyParam(std::size_t index, float plain, std::uint32_t frame) noexcept
{
    if (numEvents_ >= kMaxEvents)
        return; // can't happen (kSplitAt leaves room), but never write past the array
    events_[numEvents_++] = Event::paramChange(frame, synth::kParams[index].id, plain);
    touched_.set(index);
}

void FssClap::translate(const clap_event_header* h, std::uint32_t frame, std::uint32_t blockTime,
                        const clap_output_events* out) noexcept
{
    // frame: offset in the current sub-block (what the Engine sees); blockTime: offset in the host's block
    // (what out_events must use).
    // Events from other namespaces (another vendor's extension events) share type numbers with core events:
    // only the core namespace's are ours to interpret.
    if (h->space_id != CLAP_CORE_EVENT_SPACE_ID) {
        ++stats_.other;
        return;
    }
    auto add = [this](const Event& e) {
        if (numEvents_ < kMaxEvents)
            events_[numEvents_++] = e;
    };
    switch (h->type) {
    case CLAP_EVENT_NOTE_ON: {
        ++stats_.noteOn;
        const auto* ev = reinterpret_cast<const clap_event_note*>(h);
        if (ev->key < 0 || ev->key > 127)
            return; // a note-on must name a key; a wildcard makes no sense here
        Event e = Event::noteOn(frame, static_cast<std::uint8_t>(ev->key),
                                static_cast<float>(std::clamp(ev->velocity, 0.0, 1.0)), ev->note_id);
        e.channel = static_cast<std::uint8_t>(std::clamp<std::int16_t>(ev->channel, 0, 15));
        lastChannel_[static_cast<std::size_t>(ev->key)] = ev->channel;
        add(e);
        return;
    }
    case CLAP_EVENT_NOTE_OFF:
    case CLAP_EVENT_NOTE_CHOKE: {
        // CHOKE means "stop now" (a hi-hat closing). CFS has no instant-kill path yet, so it releases like a
        // note-off; with fast release times the difference is small. (Journal: open question.)
        ++(h->type == CLAP_EVENT_NOTE_OFF ? stats_.noteOff : stats_.noteChoke);
        const auto* ev = reinterpret_cast<const clap_event_note*>(h);
        if (ev->key >= 0 && ev->key <= 127) {
            add(Event::noteOff(frame, static_cast<std::uint8_t>(ev->key), ev->note_id));
        } else if (ev->note_id >= 0) {
            // Wildcard key, specific note id: find the key that note id is playing.
            for (const synth::Voice& v : engine_.voices())
                if (v.busy() && v.noteId() == ev->note_id)
                    add(Event::noteOff(frame, v.key(), ev->note_id));
        } else {
            add(Event{Event::Type::AllNotesOff, frame}); // key -1 and note id -1: every note
        }
        return;
    }
    case CLAP_EVENT_MIDI: {
        ++stats_.midi;
        // FL Studio and most hosts send pitch bend, mod wheel and sustain as raw MIDI on the note port
        // (CLAP has no native event for them). Reuse the standalone's decoder (D10).
        const auto* ev = reinterpret_cast<const clap_event_midi*>(h);
        const midi::DecodedMidi d = midi::classify({ev->data[0], ev->data[1], ev->data[2], 0});
        if (d.kind == midi::MidiKind::ControlChange)
            for (std::size_t i = 0; i < kNumParams; ++i)
                if (synth::kParams[i].midiCC != 0 && synth::kParams[i].midiCC == d.number) {
                    // CC → parameter, like fss_play. Reported to the host as a parameter change it must NOT
                    // record as automation: the CC itself is what gets recorded (see clap/ext/params.h).
                    const float plain = synth::fromNormalized(synth::kParams[i], static_cast<float>(d.value) / 127.0f);
                    applyParam(i, plain, frame);
                    pushParamValue(out, blockTime, i, plain, CLAP_EVENT_DONT_RECORD);
                }
        if (d.kind == midi::MidiKind::NoteOn)
            lastChannel_[d.number] = d.channel;
        Event e;
        if (midi::toEvent(d, frame, e))
            add(e);
        return;
    }
    case CLAP_EVENT_PARAM_VALUE: {
        ++stats_.paramValue;
        const auto* ev = reinterpret_cast<const clap_event_param_value*>(h);
        const int index = indexFromEvent(ev->param_id, ev->cookie);
        if (index < 0)
            return;
        // CFS has no per-note parameters, so a per-note/per-channel automation event (note_id, key or channel
        // ≠ -1) is applied globally: the closest thing we can do, and better than dropping it.
        applyParam(static_cast<std::size_t>(index), fromHost(synth::kParams[static_cast<std::size_t>(index)], ev->value),
                   frame);
        return;
    }
    case CLAP_EVENT_PARAM_MOD: ++stats_.paramMod; return; // no parameter is declared modulatable (until L4.1)
    case CLAP_EVENT_NOTE_EXPRESSION: ++stats_.noteExpression; return; // per-note expression: later
    case CLAP_EVENT_TRANSPORT: ++stats_.transport; return;            // tempo sync arrives with L5.3
    default: ++stats_.other; return;
    }
}

void FssClap::renderEngine(float* outL, float* outR, std::uint32_t frames) noexcept
{
    const std::span<Event> events(events_.data(), numEvents_);
    numEvents_ = 0;
    if (outL && outR) { // the normal case: render straight into the host's buffers
        engine_.process(events, outL, outR, static_cast<int>(frames));
        return;
    }
    // A missing channel (or no buffers at all): render in scratch-sized chunks. Each chunk gets the events
    // that fall inside it, re-based to the chunk's start; the last chunk takes all the rest.
    std::size_t first = 0;
    std::uint32_t done = 0;
    do {
        const std::uint32_t len = std::min(frames - done, kScratchFrames);
        std::size_t last = first;
        if (done + len >= frames)
            last = events.size();
        else
            while (last < events.size() && events[last].frame < done + len)
                ++last;
        for (std::size_t k = first; k < last; ++k)
            events[k].frame = events[k].frame >= done ? events[k].frame - done : 0;
        engine_.process(events.subspan(first, last - first), outL ? outL + done : scratchL_.data(),
                        outR ? outR + done : scratchR_.data(), static_cast<int>(len));
        first = last;
        done += len;
    } while (done < frames);
}

void FssClap::reportNoteEnds(const clap_output_events* out, std::uint32_t frames) noexcept
{
    // CLAP wants NOTE_END when a voice has really finished (after its release), so a host doing polyphonic
    // modulation can free its per-note state. Compare each voice slot with the end of the previous block:
    // a slot that went idle, or now plays a different note (new age), ended its old note.
    const auto& voices = engine_.voices();
    const std::uint32_t time = frames ? frames - 1 : 0;
    for (std::size_t v = 0; v < voices.size(); ++v) {
        VoiceSnap& s = snap_[v];
        const synth::Voice& now = voices[v];
        const bool same = now.busy() && s.busy && now.age() == s.age && now.key() == s.key;
        if (s.busy && !same && out) {
            clap_event_note ev{};
            ev.header = {sizeof ev, time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_END, 0};
            ev.note_id = s.noteId;
            ev.port_index = 0;
            ev.channel = s.channel;
            ev.key = s.key;
            ev.velocity = 0.0;
            (void)out->try_push(out, &ev.header);
            ++stats_.noteEndSent;
        }
        if (!same) {
            s.busy = now.busy();
            s.age = now.age();
            s.key = now.key();
            s.noteId = now.noteId();
            s.channel = lastChannel_[now.key() & 0x7F];
        }
    }
}

// =====================================================================================================
// Parameters, main-thread side [main]
// =====================================================================================================

void FssClap::pollAudio()
{
    for (std::size_t i = 0; i < kNumParams; ++i) {
        // While the user drags this parameter, or editor values for it are still queued, the audio side's
        // value is older than the mirror's: taking it would make the knob jitter or snap back. Leave the dirty
        // flag SET, so the final value is picked up once the gesture ends and the queue has caught up.
        if (gesture_[i] || uiPending_[i].load(std::memory_order_acquire) != 0)
            continue;
        if (!fromAudioDirty_[i].exchange(false, std::memory_order_acquire))
            continue;
        const float v = fromAudio_[i].load(std::memory_order_relaxed);
        synth::setParam(mirror_, synth::kParams[i], v);
        mainValues_[i].store(v, std::memory_order_relaxed);
    }
}

const synth::Patch& FssClap::mainPatch()
{
    pollAudio();
    return mirror_;
}

void FssClap::pushUi(const UiMsg& m)
{
    // Count a value as in flight BEFORE it's visible to the audio thread (which decrements after applying).
    if (m.kind == UiMsg::Kind::Value)
        uiPending_[m.index].fetch_add(1, std::memory_order_relaxed);
    if (!toAudio_.push(m)) {
        if (m.kind == UiMsg::Kind::Value)
            uiPending_[m.index].fetch_sub(1, std::memory_order_relaxed);
        resync_.store(true, std::memory_order_release); // lossless fallback (see plugin_instance.h)
    }
}

void FssClap::uiBegin(std::size_t index)
{
    gesture_[index] = true;
    pushUi({UiMsg::Kind::Begin, true, static_cast<std::uint16_t>(index), 0.0f});
}

void FssClap::uiSet(std::size_t index, float plain)
{
    const synth::ParamInfo& p = synth::kParams[index];
    synth::setParam(mirror_, p, plain);
    const float v = synth::getParam(mirror_, p); // clamped/rounded exactly as the Engine will see it
    mainValues_[index].store(v, std::memory_order_relaxed);
    pushUi({UiMsg::Kind::Value, true, static_cast<std::uint16_t>(index), v});
    // Scenario III of clap/ext/params.h: ask the host to call process() or flush() soon, so the change
    // reaches the Engine (and the host) even when no audio is running. [thread-safe, !audio-thread]
    if (hostParams_)
        hostParams_->request_flush(host_);
}

void FssClap::uiEnd(std::size_t index)
{
    pushUi({UiMsg::Kind::End, true, static_cast<std::uint16_t>(index), 0.0f});
    gesture_[index] = false;
    if (hostParams_)
        hostParams_->request_flush(host_);
}

void FssClap::applyPatchFromMain(const synth::Patch& patch)
{
    mirror_ = patch;
    for (std::size_t i = 0; i < kNumParams; ++i) {
        const float v = synth::getParam(mirror_, synth::kParams[i]);
        mainValues_[i].store(v, std::memory_order_relaxed);
        fromAudioDirty_[i].store(false, std::memory_order_relaxed); // older automation echoes are now stale
        gesture_[i] = false;
        if (active_) // the audio thread owns the Engine: send every value through the queue
            pushUi({UiMsg::Kind::Value, false, static_cast<std::uint16_t>(i), v});
    }
    if (!active_)
        engine_.setPatch(mirror_); // no audio thread: set it directly (no smoothing needed)
    // The host may have cached values (generic editors, automation lanes): tell it to read them again.
    if (hostParams_)
        hostParams_->rescan(host_, CLAP_PARAM_RESCAN_VALUES);
}

bool FssClap::paramValue(clap_id id, double* out)
{
    const int i = indexOf(id);
    if (i < 0 || !out)
        return false;
    const synth::ParamInfo& p = synth::kParams[static_cast<std::size_t>(i)];
    *out = toHost(p, synth::getParam(mainPatch(), p));
    return true;
}

bool FssClap::paramValueToText(clap_id id, double value, char* out, std::uint32_t size) const
{
    const int i = indexOf(id);
    return i >= 0 && out && valueToText(synth::kParams[static_cast<std::size_t>(i)], value, out, size);
}

bool FssClap::paramTextToValue(clap_id id, const char* text, double* out) const
{
    const int i = indexOf(id);
    if (i < 0 || !text || !out)
        return false;
    const auto v = textToValue(synth::kParams[static_cast<std::size_t>(i)], text);
    if (!v)
        return false;
    *out = *v;
    return true;
}

// =====================================================================================================
// State [main]
// =====================================================================================================

// Since L3.4 (D15) the state is the same text as a preset file (synth::patchToText): a project's CFS state
// can be read, diffed, even copied out into a preset. Projects saved by the L3.2 build contain the binary
// "CFS0" format instead; it starts with that magic (no text patch can), so load() tells them apart.

bool FssClap::stateSave(const clap_ostream* stream)
{
    if (!stream)
        return false;
    const std::string text = synth::patchToText(mainPatch(), presetName_);
    std::vector<std::byte> bytes(text.size());
    std::memcpy(bytes.data(), text.data(), text.size());
    const bool ok = writeAll(stream, bytes);
    Trace::log(this, "state.save: %zu bytes, %s", bytes.size(), ok ? "ok" : "FAILED (stream write)");
    return ok;
}

bool FssClap::stateLoad(const clap_istream* stream)
{
    std::vector<std::byte> bytes;
    if (!stream || !readAll(stream, bytes, synth::kMaxPatchTextBytes)) {
        Trace::log(this, "state.load: REJECTED (stream error or too large)");
        return false;
    }
    synth::Patch patch;
    std::string name;
    std::string error;
    bool ok = false;
    if (bytes.size() >= 4 && std::memcmp(bytes.data(), state_v0::kMagic, 4) == 0) {
        ok = state_v0::load(bytes, patch); // a project saved by the L3.2 build
        error = "corrupt CFS0 state";
    } else {
        const synth::PatchLoad r =
            synth::patchFromText(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        ok = r.ok;
        patch = r.patch;
        name = r.name;
        error = r.error + (r.errorLine ? " (line " + std::to_string(r.errorLine) + ")" : "");
    }
    Trace::log(this, "state.load: %zu bytes, %s%s", bytes.size(), ok ? "ok" : "REJECTED: ", ok ? "" : error.c_str());
    if (!ok) {
        if (hostLog_)
            hostLog_->log(host_, CLAP_LOG_WARNING, ("CFS: could not load the saved state: " + error).c_str());
        return false;
    }
    presetName_ = name;
    applyPatchFromMain(patch);
    return true;
}

// =====================================================================================================
// The C side: static trampolines, extension tables, the descriptor
// =====================================================================================================
//
// A C host can't call a C++ member function. Every CLAP function pointer is therefore a captureless lambda
// (which converts to a plain C function pointer) that recovers the FssClap object from plugin->plugin_data
// and forwards. These tables are the hand-made vtables of L3.1.

namespace {

const clap_plugin_audio_ports kAudioPorts = {
    [](const clap_plugin*, bool isInput) -> std::uint32_t { return isInput ? 0u : 1u; },
    [](const clap_plugin*, std::uint32_t index, bool isInput, clap_audio_port_info* info) -> bool {
        if (isInput || index != 0 || !info)
            return false;
        std::memset(info, 0, sizeof *info);
        info->id = 0;
        std::snprintf(info->name, sizeof info->name, "Main out");
        info->flags = CLAP_AUDIO_PORT_IS_MAIN;
        info->channel_count = 2;
        info->port_type = CLAP_PORT_STEREO;
        info->in_place_pair = CLAP_INVALID_ID;
        return true;
    },
};

const clap_plugin_note_ports kNotePorts = {
    [](const clap_plugin*, bool isInput) -> std::uint32_t { return isInput ? 1u : 0u; },
    [](const clap_plugin*, std::uint32_t index, bool isInput, clap_note_port_info* info) -> bool {
        if (!isInput || index != 0 || !info)
            return false;
        std::memset(info, 0, sizeof *info);
        info->id = 0;
        // CLAP notes (with note ids) preferred; raw MIDI accepted too, which is how bend/CC/sustain arrive.
        info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
        info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
        std::snprintf(info->name, sizeof info->name, "Notes");
        return true;
    },
};

const clap_plugin_params kParamsExt = {
    [](const clap_plugin*) -> std::uint32_t { return static_cast<std::uint32_t>(kNumParams); },
    [](const clap_plugin*, std::uint32_t index, clap_param_info* info) -> bool {
        if (index >= kNumParams || !info)
            return false;
        fillInfo(index, *info);
        return true;
    },
    [](const clap_plugin* p, clap_id id, double* out) { return FssClap::self(p)->paramValue(id, out); },
    [](const clap_plugin* p, clap_id id, double v, char* out, std::uint32_t size) {
        return FssClap::self(p)->paramValueToText(id, v, out, size);
    },
    [](const clap_plugin* p, clap_id id, const char* text, double* out) {
        return FssClap::self(p)->paramTextToValue(id, text, out);
    },
    [](const clap_plugin* p, const clap_input_events* in, const clap_output_events* out) {
        FssClap::self(p)->paramsFlush(in, out);
    },
};

const clap_plugin_state kStateExt = {
    [](const clap_plugin* p, const clap_ostream* s) { return FssClap::self(p)->stateSave(s); },
    [](const clap_plugin* p, const clap_istream* s) { return FssClap::self(p)->stateLoad(s); },
};

const clap_plugin_voice_info kVoiceInfo = {
    [](const clap_plugin*, clap_voice_info* info) -> bool {
        if (!info)
            return false;
        info->voice_count = Engine::kMaxPolyphony;
        info->voice_capacity = Engine::kNumVoices;
        info->flags = CLAP_VOICE_INFO_SUPPORTS_OVERLAPPING_NOTES; // a re-struck key gets a fresh voice (D8)
        return true;
    },
};

} // namespace

const void* FssClap::getExtension(const char* id) const noexcept
{
    if (!id)
        return nullptr;
    if (std::strcmp(id, CLAP_EXT_AUDIO_PORTS) == 0)
        return &kAudioPorts;
    if (std::strcmp(id, CLAP_EXT_NOTE_PORTS) == 0)
        return &kNotePorts;
    if (std::strcmp(id, CLAP_EXT_PARAMS) == 0)
        return &kParamsExt;
    if (std::strcmp(id, CLAP_EXT_STATE) == 0)
        return &kStateExt;
    if (std::strcmp(id, CLAP_EXT_VOICE_INFO) == 0)
        return &kVoiceInfo;
    return nullptr;
}

const clap_plugin_descriptor kDescriptor = {
    CLAP_VERSION_INIT,
    "io.github.423w.claudefs", // PERMANENT: every FL Studio project that uses CFS refers to it by this id
    "CFS",
    "ClaudeFS",
    "https://github.com/423w/ClaudeFS",
    "",
    "",
    FSS_VERSION_STRING,
    "Claude's build of FirstStreetSynthesis: 4-operator FM into a virtual-analog filter",
    kFeatures,
};

const clap_plugin* createPlugin(const clap_host* host)
{
    auto* self = new FssClap(host);
    clap_plugin& p = self->plugin_;
    p.desc = &kDescriptor;
    p.plugin_data = self;
    p.init = [](const clap_plugin* pl) { return FssClap::self(pl)->init(); };
    p.destroy = [](const clap_plugin* pl) { FssClap::self(pl)->destroy(); };
    p.activate = [](const clap_plugin* pl, double sr, std::uint32_t minF, std::uint32_t maxF) {
        return FssClap::self(pl)->activate(sr, minF, maxF);
    };
    p.deactivate = [](const clap_plugin* pl) { FssClap::self(pl)->deactivate(); };
    p.start_processing = [](const clap_plugin* pl) { return FssClap::self(pl)->startProcessing(); };
    p.stop_processing = [](const clap_plugin* pl) { FssClap::self(pl)->stopProcessing(); };
    p.reset = [](const clap_plugin* pl) { FssClap::self(pl)->reset(); };
    p.process = [](const clap_plugin* pl, const clap_process* pr) { return FssClap::self(pl)->process(pr); };
    p.get_extension = [](const clap_plugin* pl, const char* id) { return FssClap::self(pl)->getExtension(id); };
    p.on_main_thread = [](const clap_plugin*) {};
    return &p;
}

} // namespace fss::plugin
