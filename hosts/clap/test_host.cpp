// test_host.cpp: fss_clap_host, a small headless CLAP host (L3.2). It does to CFS.clap what a DAW does,
// minus the DAW: LoadLibrary, find clap_entry, walk the factory, create instances, activate, process with
// awkward block sizes, automate, save and load state, re-activate at other sample rates. Then it checks the
// results. Claude can't drive FL Studio; this, the unit tests and clap-validator are what it can check.
//
//   fss_clap_host <path\to\CFS.clap> [--bench]
//
// Exit code = number of failed checks (0 = all good). Output is ASCII (Windows console code page).
#include "clap_params.h"
#include "midi_message.h"
#include "midi_stream.h"

#include <clap/clap.h>
#include <fss/engine.h>
#include <fss/event.h>
#include <fss/synth/params.h>

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace fss;

namespace {

int gFailures = 0;

void check(bool ok, const char* what)
{
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok)
        ++gFailures;
}

// ---- the host side of the contract ---------------------------------------------------------------------

struct HostState {
    int rescans = 0, flushRequests = 0, logs = 0;
};
HostState gHost;

const clap_host_params kHostParams = {
    [](const clap_host_t*, clap_param_rescan_flags) { ++gHost.rescans; },
    [](const clap_host_t*, clap_id, clap_param_clear_flags) {},
    [](const clap_host_t*) { ++gHost.flushRequests; },
};
const clap_host_log kHostLog = {
    [](const clap_host_t*, clap_log_severity, const char* msg) {
        ++gHost.logs;
        std::printf("    (plugin log: %s)\n", msg);
    },
};

const clap_host kHost = {
    CLAP_VERSION_INIT,
    nullptr,
    "fss_clap_host",
    "CFS",
    "",
    "0.1",
    [](const clap_host_t*, const char* id) -> const void* {
        if (std::strcmp(id, CLAP_EXT_PARAMS) == 0)
            return &kHostParams;
        if (std::strcmp(id, CLAP_EXT_LOG) == 0)
            return &kHostLog;
        return nullptr;
    },
    [](const clap_host_t*) {}, // request_restart
    [](const clap_host_t*) {}, // request_process
    [](const clap_host_t*) {}, // request_callback
};

// Input events for one block: a vector of pointers into the score, already re-timed to the block.
struct InEvents {
    std::vector<const clap_event_header*> list;
    clap_input_events iface{this,
                            [](const clap_input_events* l) {
                                return static_cast<std::uint32_t>(static_cast<const InEvents*>(l->ctx)->list.size());
                            },
                            [](const clap_input_events* l, std::uint32_t i) {
                                return static_cast<const InEvents*>(l->ctx)->list[i];
                            }};
};

// Output events: we only count what we care about.
struct OutEvents {
    int noteEnds = 0, paramValues = 0, gestures = 0, other = 0;
    clap_output_events iface{this, [](const clap_output_events* l, const clap_event_header* h) {
                                 auto* self = static_cast<OutEvents*>(l->ctx);
                                 if (h->type == CLAP_EVENT_NOTE_END)
                                     ++self->noteEnds;
                                 else if (h->type == CLAP_EVENT_PARAM_VALUE)
                                     ++self->paramValues;
                                 else if (h->type == CLAP_EVENT_PARAM_GESTURE_BEGIN || h->type == CLAP_EVENT_PARAM_GESTURE_END)
                                     ++self->gestures;
                                 else
                                     ++self->other;
                                 return true;
                             }};
};

// State streams over a byte vector. The reader hands out at most `chunk` bytes per call, like a fussy host.
struct OutStream {
    std::vector<std::uint8_t> bytes;
    clap_ostream iface{this, [](const clap_ostream* s, const void* buf, std::uint64_t n) -> std::int64_t {
                           auto* self = static_cast<OutStream*>(s->ctx);
                           const auto* b = static_cast<const std::uint8_t*>(buf);
                           self->bytes.insert(self->bytes.end(), b, b + n);
                           return static_cast<std::int64_t>(n);
                       }};
};
struct InStream {
    const std::vector<std::uint8_t>* bytes;
    std::size_t pos = 0, chunk = 7;
    clap_istream iface{this, [](const clap_istream* s, void* buf, std::uint64_t n) -> std::int64_t {
                           auto* self = static_cast<InStream*>(s->ctx);
                           const std::size_t left = self->bytes->size() - self->pos;
                           const std::size_t take = std::min<std::size_t>({left, static_cast<std::size_t>(n), self->chunk});
                           std::memcpy(buf, self->bytes->data() + self->pos, take);
                           self->pos += take;
                           return static_cast<std::int64_t>(take);
                       }};
};

// ---- the plugin, as a host sees it ------------------------------------------------------------------

struct Library {
    HMODULE dll = nullptr;
    const clap_plugin_entry* entry = nullptr;
    const clap_plugin_factory* factory = nullptr;
};

struct Instance {
    const clap_plugin* p = nullptr;
    const clap_plugin_params* params = nullptr;
    const clap_plugin_state* state = nullptr;

    Instance(const Library& lib)
    {
        p = lib.factory->create_plugin(lib.factory, &kHost, "io.github.423w.claudefs");
        if (p && p->init(p)) {
            params = static_cast<const clap_plugin_params*>(p->get_extension(p, CLAP_EXT_PARAMS));
            state = static_cast<const clap_plugin_state*>(p->get_extension(p, CLAP_EXT_STATE));
        }
    }
    ~Instance()
    {
        if (p)
            p->destroy(p);
    }
    Instance(const Instance&) = delete;
    Instance& operator=(const Instance&) = delete;
};

// ---- a score: CLAP events at absolute sample times ----------------------------------------------------

// Every event kept as a union so pointers to it are stable and look exactly like what a host sends.
union AnyEvent {
    clap_event_header header;
    clap_event_note note;
    clap_event_param_value param;
    clap_event_midi midi;
};

struct Score {
    std::vector<AnyEvent> events; // sorted by absolute time (header.time holds the ABSOLUTE frame here)
    int notes = 0;
    void note(std::uint32_t at, std::uint16_t type, int key, double velocity, int id)
    {
        AnyEvent e{};
        e.note = {{sizeof(clap_event_note), at, CLAP_CORE_EVENT_SPACE_ID, type, 0}, id, 0, 0, static_cast<std::int16_t>(key), velocity};
        events.push_back(e);
        notes += type == CLAP_EVENT_NOTE_ON ? 1 : 0;
    }
    void param(std::uint32_t at, std::uint32_t id, double hostValue)
    {
        AnyEvent e{};
        e.param = {{sizeof(clap_event_param_value), at, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0},
                   id, nullptr, -1, -1, -1, -1, hostValue};
        events.push_back(e);
    }
    void midi(std::uint32_t at, std::uint8_t s, std::uint8_t d1, std::uint8_t d2)
    {
        AnyEvent e{};
        e.midi = {{sizeof(clap_event_midi), at, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_MIDI, 0}, 0, {s, d1, d2}};
        events.push_back(e);
    }
    void sort()
    {
        std::stable_sort(events.begin(), events.end(),
                         [](const AnyEvent& a, const AnyEvent& b) { return a.header.time < b.header.time; });
    }
};

const synth::ParamInfo& info(std::uint32_t id) { return *synth::findParam(id); }

/// A musical workout: a filtered FM chord, automation on cutoff and resonance, a filter-mode switch,
/// pitch bend, mod wheel, sustain pedal, repeated notes, and dense automation (one point every 16 frames).
Score makeScore(double sampleRate)
{
    using namespace synth::param_id;
    Score s;
    const auto at = [&](double seconds) { return static_cast<std::uint32_t>(seconds * sampleRate); };
    s.param(0, kFilterMode, 1.0); // low-pass (stepped: plain value)
    s.param(0, kFilterEnvAmount, plugin::toHost(info(kFilterEnvAmount), 3.0f));
    s.param(0, op(0, synth::OpField::Level), plugin::toHost(info(op(0, synth::OpField::Level)), 2.0f));
    int id = 100;
    const int chord[] = {48, 55, 60, 64, 67};
    for (int k : chord)
        s.note(at(0.05), CLAP_EVENT_NOTE_ON, k, 0.8, id++);
    for (int i = 0; i < 2000; ++i) // a cutoff sweep, one point every 16 samples (≈ 3 kHz automation rate)
        s.param(16u * static_cast<std::uint32_t>(i), kCutoff, 0.3 + 0.5 * std::sin(i * 0.01));
    s.param(at(0.4), kResonance, 0.6);
    s.midi(at(0.5), 0xE0, 0x00, 0x60); // pitch bend up
    s.midi(at(0.6), 0xB0, 1, 100);     // mod wheel
    s.midi(at(0.7), 0xB0, 64, 127);    // sustain down
    for (int i = 0; i < 5; ++i)
        s.note(at(0.8), CLAP_EVENT_NOTE_OFF, chord[i], 0.0, 100 + i);
    for (int r = 0; r < 6; ++r) { // repeated notes while the pedal holds the chord
        s.note(at(0.9 + r * 0.1), CLAP_EVENT_NOTE_ON, 72, 0.6, id);
        s.note(at(0.95 + r * 0.1), CLAP_EVENT_NOTE_OFF, 72, 0.0, id++);
    }
    s.param(at(1.2), kFilterMode, 3.0); // switch to high-pass mid-note
    s.midi(at(1.5), 0xB0, 64, 0);       // sustain up: everything releases
    s.midi(at(1.6), 0xE0, 0x00, 0x40);  // bend back to centre
    s.sort();
    return s;
}

/// The same score as fss::Events, through the same mappings the plugin uses, rendered by a bare Engine in
/// fixed 4096-frame blocks. This is the reference the plugin must match bit for bit.
std::vector<float> renderDirect(const Score& s, double sampleRate, std::uint32_t total, std::uint32_t block = 4096)
{
    Engine engine;
    engine.prepare(sampleRate, static_cast<int>(block));
    std::vector<float> out(2 * static_cast<std::size_t>(total));
    std::vector<float> l(block), r(block);
    std::size_t next = 0;
    for (std::uint32_t pos = 0; pos < total; pos += block) {
        const std::uint32_t n = std::min<std::uint32_t>(block, total - pos);
        std::vector<Event> ev;
        for (; next < s.events.size() && s.events[next].header.time < pos + n; ++next) {
            const AnyEvent& a = s.events[next];
            const std::uint32_t f = a.header.time - pos;
            if (a.header.type == CLAP_EVENT_NOTE_ON)
                ev.push_back(Event::noteOn(f, static_cast<std::uint8_t>(a.note.key), static_cast<float>(a.note.velocity), a.note.note_id));
            else if (a.header.type == CLAP_EVENT_NOTE_OFF)
                ev.push_back(Event::noteOff(f, static_cast<std::uint8_t>(a.note.key), a.note.note_id));
            else if (a.header.type == CLAP_EVENT_PARAM_VALUE)
                ev.push_back(Event::paramChange(f, a.param.param_id, plugin::fromHost(info(a.param.param_id), a.param.value)));
            else if (a.header.type == CLAP_EVENT_MIDI) {
                Event e;
                if (midi::toEvent(midi::classify({a.midi.data[0], a.midi.data[1], a.midi.data[2], 0}), f, e))
                    ev.push_back(e);
            }
        }
        engine.process(ev, l.data(), r.data(), static_cast<int>(n));
        for (std::uint32_t i = 0; i < n; ++i) {
            out[2 * (pos + i)] = l[i];
            out[2 * (pos + i) + 1] = r[i];
        }
    }
    return out;
}

/// Run a score through a plugin instance with RANDOM block sizes 1..maxBlock (and some 0-frame calls), or,
/// with seed 0, fixed maxBlock-sized blocks. Returns interleaved stereo; `outCount` counts what came back.
std::vector<float> renderPlugin(Instance& inst, const Score& s, std::uint32_t total, std::uint32_t maxBlock,
                                std::uint32_t seed, OutEvents& outCount)
{
    std::mt19937 rng(seed);
    std::uniform_int_distribution<std::uint32_t> size(1, maxBlock);
    std::vector<float> out(2 * static_cast<std::size_t>(total));
    std::vector<float> l(maxBlock), r(maxBlock);
    std::vector<AnyEvent> local; // re-timed copies for this block
    std::size_t next = 0;
    std::uint32_t pos = 0;
    while (pos < total) {
        std::uint32_t n = std::min(seed ? size(rng) : maxBlock, total - pos);
        if (seed && rng() % 17 == 0)
            n = 0; // hosts really do send 0-frame blocks (e.g. to deliver events while the transport is stopped)
        local.clear();
        // A 0-frame block takes the events AT pos (they're applied "at the end" of nothing, i.e. at pos).
        for (; next < s.events.size() && s.events[next].header.time < pos + std::max(n, 1u); ++next) {
            AnyEvent e = s.events[next];
            e.header.time -= pos;
            local.push_back(e);
        }
        InEvents in;
        for (const AnyEvent& e : local)
            in.list.push_back(&e.header);
        float* ch[2] = {l.data(), r.data()};
        clap_audio_buffer buf{};
        buf.data32 = ch;
        buf.channel_count = 2;
        clap_process proc{};
        proc.steady_time = pos;
        proc.frames_count = n;
        proc.audio_outputs = &buf;
        proc.audio_outputs_count = 1;
        proc.in_events = &in.iface;
        proc.out_events = &outCount.iface;
        inst.p->process(inst.p, &proc);
        for (std::uint32_t i = 0; i < n; ++i) {
            out[2 * (pos + i)] = l[i];
            out[2 * (pos + i) + 1] = r[i];
        }
        pos += n;
    }
    return out;
}

bool allFinite(const std::vector<float>& v)
{
    return std::all_of(v.begin(), v.end(), [](float x) { return std::isfinite(x); });
}
float peakOf(const std::vector<float>& v)
{
    float p = 0.0f;
    for (float x : v)
        p = std::max(p, std::abs(x));
    return p;
}

std::vector<std::uint8_t> saveState(Instance& inst)
{
    OutStream os;
    if (!inst.state->save(inst.p, &os.iface))
        os.bytes.clear();
    return os.bytes;
}
bool loadState(Instance& inst, const std::vector<std::uint8_t>& bytes, std::size_t chunk = 7)
{
    InStream is{&bytes};
    is.chunk = chunk;
    return inst.state->load(inst.p, &is.iface);
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2) {
        std::printf("usage: fss_clap_host <path to CFS.clap> [--bench]\n");
        return 2;
    }
    const bool bench = argc > 2 && std::strcmp(argv[2], "--bench") == 0;
    const double sr = 48000.0;

    std::printf("1. Loading %s\n", argv[1]);
    Library lib;
    lib.dll = LoadLibraryA(argv[1]);
    check(lib.dll != nullptr, "LoadLibrary");
    if (!lib.dll)
        return 1;
    lib.entry = reinterpret_cast<const clap_plugin_entry*>(GetProcAddress(lib.dll, "clap_entry"));
    check(lib.entry != nullptr, "exports clap_entry");
    if (!lib.entry)
        return 1;
    check(clap_version_is_compatible(lib.entry->clap_version), "entry CLAP version is compatible");
    check(lib.entry->init(argv[1]), "entry.init");
    lib.factory = static_cast<const clap_plugin_factory*>(lib.entry->get_factory(CLAP_PLUGIN_FACTORY_ID));
    check(lib.factory != nullptr, "plugin factory");
    check(lib.entry->get_factory("not.a.factory") == nullptr, "unknown factory id -> null");
    if (!lib.factory)
        return 1;
    check(lib.factory->get_plugin_count(lib.factory) == 1, "one plugin in the library");
    const clap_plugin_descriptor* d = lib.factory->get_plugin_descriptor(lib.factory, 0);
    check(d && std::strcmp(d->id, "io.github.423w.claudefs") == 0, "descriptor id");
    if (d)
        std::printf("    %s by %s, version %s\n", d->name, d->vendor, d->version);
    check(lib.factory->create_plugin(lib.factory, &kHost, "io.github.423w.claudefs.x") == nullptr,
          "id with trailing garbage -> no instance");

    std::printf("2. Instance, ports, parameters\n");
    {
        Instance a(lib);
        check(a.p && a.params && a.state, "create + init + params/state extensions");
        if (!a.params)
            return 1;
        const auto* ap = static_cast<const clap_plugin_audio_ports*>(a.p->get_extension(a.p, CLAP_EXT_AUDIO_PORTS));
        const auto* np = static_cast<const clap_plugin_note_ports*>(a.p->get_extension(a.p, CLAP_EXT_NOTE_PORTS));
        check(ap && ap->count(a.p, false) == 1 && ap->count(a.p, true) == 0, "audio ports: 1 out, 0 in");
        check(np && np->count(a.p, true) == 1, "note ports: 1 in");
        const std::uint32_t n = a.params->count(a.p);
        check(n == plugin::kNumParams, "parameter count == param table size");
        bool idsOk = true, defaultsOk = true, textOk = true;
        for (std::uint32_t i = 0; i < n; ++i) {
            clap_param_info pi{};
            idsOk &= a.params->get_info(a.p, i, &pi) && pi.id == synth::kParams[i].id;
            double v = -1.0;
            defaultsOk &= a.params->get_value(a.p, pi.id, &v) && v == pi.default_value;
            char text[CLAP_NAME_SIZE];
            double back = -1.0;
            textOk &= a.params->value_to_text(a.p, pi.id, v, text, sizeof text) &&
                      a.params->text_to_value(a.p, pi.id, text, &back) && std::abs(back - v) < 1e-3;
        }
        check(idsOk, "every param id matches the table (stable ids)");
        check(defaultsOk, "every get_value == its default after init");
        check(textOk, "value -> text -> value round trip, every param");
    }

    std::printf("3. Playing: CLAP path vs direct Engine (random block sizes, dense automation)\n");
    const Score score = makeScore(sr);
    const auto total = static_cast<std::uint32_t>(2.5 * sr);
    const std::vector<float> reference = renderDirect(score, sr, total);
    std::vector<std::uint8_t> savedA;
    {
        Instance a(lib);
        check(a.p->activate(a.p, sr, 1, 1024) && a.p->start_processing(a.p), "activate(48 kHz, 1..1024) + start");
        OutEvents out;
        const std::vector<float> got = renderPlugin(a, score, total, 1024, 1234, out);
        check(allFinite(got) && peakOf(got) > 0.05f, "output finite and audible");
        check(got == reference, "plugin output == Engine output, bit for bit");
        std::printf("    NOTE_END events: %d for %d notes\n", out.noteEnds, score.notes);
        check(out.noteEnds == score.notes, "one NOTE_END per note, after its release");

        std::printf("4. State\n");
        savedA = saveState(a);
        check(!savedA.empty(), "state.save");
        a.p->stop_processing(a.p);
        a.p->deactivate(a.p);
    }
    {
        Instance b(lib);
        check(loadState(b, savedA), "state.load into a fresh instance (7 bytes per read)");
        Instance a2(lib);
        (void)loadState(a2, savedA, 4096);
        bool same = true;
        for (const synth::ParamInfo& p : synth::kParams) {
            double vb = 0, va = 0;
            same &= b.params->get_value(b.p, p.id, &vb) && a2.params->get_value(a2.p, p.id, &va) && va == vb;
        }
        check(same, "loaded values identical whatever the read size");
        check(saveState(b) == savedA, "save(load(state)) == state, byte for byte");
        double cutoff = 0;
        b.params->get_value(b.p, synth::param_id::kCutoff, &cutoff);
        check(std::abs(cutoff - (0.3 + 0.5 * std::sin(1999 * 0.01))) < 1e-5, "state holds the last automation value");
        check(!loadState(b, {}), "empty state rejected");
        std::vector<std::uint8_t> junk(1000);
        std::mt19937 rng(7);
        for (auto& x : junk)
            x = static_cast<std::uint8_t>(rng());
        check(!loadState(b, junk), "random bytes rejected");
        std::vector<std::uint8_t> cut(savedA.begin(), savedA.end() - 3);
        check(!loadState(b, cut), "truncated state rejected");
        check(saveState(b) == savedA, "a rejected load leaves the patch untouched");
    }

    std::printf("5. Lifecycle: re-activation at other sample rates, flush without processing\n");
    {
        Instance a(lib);
        bool ok = true;
        for (double rate : {44100.0, 22050.0, 88200.0, 96000.0, 192000.0, 48000.0}) {
            if (!a.p->activate(a.p, rate, 1, 512)) {
                ok = false;
                continue;
            }
            a.p->start_processing(a.p);
            Score s;
            s.note(0, CLAP_EVENT_NOTE_ON, 69, 1.0, 1);
            s.note(static_cast<std::uint32_t>(0.2 * rate), CLAP_EVENT_NOTE_OFF, 69, 0.0, 1);
            OutEvents out;
            const auto got = renderPlugin(a, s, static_cast<std::uint32_t>(0.5 * rate), 512, 99, out);
            const float pk = peakOf(got);
            ok &= allFinite(got) && pk > 0.1f && pk <= 1.0f;
            a.p->stop_processing(a.p);
            a.p->deactivate(a.p);
        }
        check(ok, "44.1/22.05/88.2/96/192/48 kHz: finite, audible, within +-1");

        // flush() while inactive: parameter changes with no audio running (main thread).
        Score s;
        s.param(0, synth::param_id::kResonance, 0.25);
        InEvents in;
        in.list.push_back(&s.events[0].header);
        OutEvents out;
        a.params->flush(a.p, &in.iface, &out.iface);
        double q = 0;
        a.params->get_value(a.p, synth::param_id::kResonance, &q);
        check(q == 0.25, "flush() while inactive applies the change");
    }

    std::printf("6. Several instances at once\n");
    {
        Instance a(lib), b(lib), c(lib);
        for (Instance* i : {&a, &b, &c}) {
            i->p->activate(i->p, sr, 1, 512);
            i->p->start_processing(i->p);
        }
        OutEvents o1, o2, o3;
        const std::uint32_t n = static_cast<std::uint32_t>(sr);
        const auto ra = renderPlugin(a, score, n, 512, 1, o1);
        const auto rb = renderPlugin(b, score, n, 512, 2, o2);
        const auto rc = renderPlugin(c, score, n, 512, 3, o3);
        check(ra == rb && rb == rc, "three instances, different block sizes, identical output (no shared state)");
        for (Instance* i : {&a, &b, &c}) {
            i->p->stop_processing(i->p);
            i->p->deactivate(i->p);
        }
    }

    using DebugCount = std::uint64_t (*)();
    if (auto f = reinterpret_cast<DebugCount>(GetProcAddress(lib.dll, "fss_debug_rt_violations"))) {
        std::printf("7. Real-time audit (Debug build: allocation trap linked into the DLL)\n");
        check(f() == 0, "0 allocations inside process()/flush()");
    }

    if (bench) {
        std::printf("8. Overhead of the CLAP layer (16-voice pad, 256-frame blocks, 4 s)\n");
        Score s;
        for (int k = 0; k < 16; ++k)
            s.note(0, CLAP_EVENT_NOTE_ON, 40 + 3 * k, 0.7, k);
        s.param(0, synth::param_id::kFilterMode, 1.0);
        s.sort();
        const auto frames = static_cast<std::uint32_t>(4 * sr);
        using clock = std::chrono::steady_clock;
        double bestPlugin = 1e30, bestEngine = 1e30;
        for (int rep = 0; rep < 5; ++rep) {
            Instance a(lib);
            a.p->activate(a.p, sr, 256, 256);
            a.p->start_processing(a.p);
            OutEvents out;
            auto t0 = clock::now();
            (void)renderPlugin(a, s, frames, 256, 0, out); // fixed 256-frame blocks, like the direct render
            bestPlugin = std::min(bestPlugin, std::chrono::duration<double>(clock::now() - t0).count());
            a.p->stop_processing(a.p);
            a.p->deactivate(a.p);
            t0 = clock::now();
            (void)renderDirect(s, sr, frames, 256);
            bestEngine = std::min(bestEngine, std::chrono::duration<double>(clock::now() - t0).count());
        }
        std::printf("    plugin path %.1f ms, direct Engine %.1f ms for %.0f s of audio (best of 5)\n",
                    bestPlugin * 1e3, bestEngine * 1e3, frames / sr);
    }

    lib.entry->deinit();
    FreeLibrary(lib.dll);
    std::printf("%s: %d failure(s)\n", gFailures ? "FAILED" : "ALL PASSED", gFailures);
    return gFailures;
}
