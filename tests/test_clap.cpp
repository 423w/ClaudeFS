// Tests for the CLAP plugin layer (L3.2), in-process: the plugin's C function tables are called directly,
// with a fake host, no DLL. (tests/CMakeLists.txt also runs fss_clap_host on the real DLL, and clap-validator.)
#include "clap_params.h"
#include "plugin.h"
#include "plugin_instance.h"
#include "state_v0.h"

#include <fss/engine.h>
#include <fss/synth/params.h>
#include <fss/synth/patch_io.h>
#include <fss/synth/presets.h>
#include <fss/util/realtime_guard.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#include <string>
#include <vector>

using namespace fss;
using plugin::FssClap;

namespace {

// ---- a fake host -------------------------------------------------------------------------------------
int gRescans = 0, gFlushRequests = 0;
const clap_host_params kHostParams = {
    [](const clap_host_t*, clap_param_rescan_flags) { ++gRescans; },
    [](const clap_host_t*, clap_id, clap_param_clear_flags) {},
    [](const clap_host_t*) { ++gFlushRequests; },
};
const clap_host kHost = {
    CLAP_VERSION_INIT, nullptr, "tests", "CFS", "", "1",
    [](const clap_host_t*, const char* id) -> const void* {
        return std::strcmp(id, CLAP_EXT_PARAMS) == 0 ? &kHostParams : nullptr;
    },
    [](const clap_host_t*) {}, [](const clap_host_t*) {}, [](const clap_host_t*) {},
};

struct InList {
    std::vector<std::vector<std::uint8_t>> storage; // each event's bytes (events have different sizes)
    clap_input_events iface{this,
                            [](const clap_input_events* l) {
                                return static_cast<std::uint32_t>(static_cast<const InList*>(l->ctx)->storage.size());
                            },
                            [](const clap_input_events* l, std::uint32_t i) {
                                return reinterpret_cast<const clap_event_header*>(
                                    static_cast<const InList*>(l->ctx)->storage[i].data());
                            }};
    template <typename E>
    void add(const E& e)
    {
        const auto* b = reinterpret_cast<const std::uint8_t*>(&e);
        storage.emplace_back(b, b + sizeof e);
    }
    void param(std::uint32_t time, std::uint32_t id, double hostValue, const void* cookie = nullptr)
    {
        clap_event_param_value e{};
        e.header = {sizeof e, time, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_VALUE, 0};
        e.param_id = id;
        e.cookie = const_cast<void*>(cookie);
        e.note_id = e.port_index = e.channel = e.key = -1;
        e.value = hostValue;
        add(e);
    }
    void note(std::uint32_t time, std::uint16_t type, int key, int id = -1)
    {
        clap_event_note e{};
        e.header = {sizeof e, time, CLAP_CORE_EVENT_SPACE_ID, type, 0};
        e.note_id = id;
        e.key = static_cast<std::int16_t>(key);
        e.velocity = 0.8;
        add(e);
    }
};

struct OutList {
    std::vector<std::vector<std::uint8_t>> events;
    clap_output_events iface{this, [](const clap_output_events* l, const clap_event_header* h) {
                                 auto* self = static_cast<OutList*>(l->ctx);
                                 const auto* b = reinterpret_cast<const std::uint8_t*>(h);
                                 self->events.emplace_back(b, b + h->size);
                                 return true;
                             }};
    [[nodiscard]] const clap_event_header& at(std::size_t i) const
    {
        return *reinterpret_cast<const clap_event_header*>(events[i].data());
    }
};

/// One plugin instance, created, initialized and (optionally) activated, destroyed at scope exit.
struct Plugin {
    const clap_plugin* p;
    const clap_plugin_params* params;
    explicit Plugin(bool activate = true, double sr = 48000.0)
        : p(plugin::createPlugin(&kHost)), params(nullptr)
    {
        REQUIRE(p->init(p));
        params = static_cast<const clap_plugin_params*>(p->get_extension(p, CLAP_EXT_PARAMS));
        if (activate) {
            REQUIRE(p->activate(p, sr, 1, 4096));
            REQUIRE(p->start_processing(p));
            active = true;
        }
    }
    ~Plugin()
    {
        if (active) {
            p->stop_processing(p);
            p->deactivate(p);
        }
        p->destroy(p);
    }
    Plugin(const Plugin&) = delete;
    Plugin& operator=(const Plugin&) = delete;
    FssClap& impl() { return *FssClap::self(p); }

    /// One block. Null left = no audio buffers at all; null right = a 1-channel bus.
    clap_process_status run(std::uint32_t frames, InList* in, OutList* out, float* left, float* right,
                            const clap_event_transport* transport = nullptr)
    {
        InList empty;
        OutList sink;
        float* ch[2] = {left, right};
        clap_audio_buffer buf{};
        buf.data32 = ch;
        buf.channel_count = right ? 2u : 1u;
        clap_process proc{};
        proc.frames_count = frames;
        proc.audio_outputs = left ? &buf : nullptr;
        proc.audio_outputs_count = left ? 1u : 0u;
        proc.in_events = in ? &in->iface : &empty.iface;
        proc.out_events = out ? &out->iface : &sink.iface;
        proc.transport = transport; // L5.3: the host's tempo
        return p->process(p, &proc);
    }
    double value(std::uint32_t id)
    {
        double v = -1;
        REQUIRE(params->get_value(p, id, &v));
        return v;
    }
    bool active = false;
};

std::size_t indexOf(std::uint32_t id) { return static_cast<std::size_t>(plugin::indexOf(id)); }

} // namespace

// ---- the parameter mapping (clap_params.h) -----------------------------------------------------------

TEST_CASE("clap params: host value <-> plain round-trips for every parameter", "[clap]")
{
    std::mt19937 rng(1);
    for (const synth::ParamInfo& p : synth::kParams) {
        for (int i = 0; i < 50; ++i) {
            const float plain = synth::fromNormalized(p, std::uniform_real_distribution<float>(0, 1)(rng));
            const float back = plugin::fromHost(p, plugin::toHost(p, plain));
            INFO(p.name << " " << plain);
            CHECK(std::abs(back - plain) <= 1e-4f * std::max(1.0f, std::abs(plain)));
        }
        clap_param_info info{};
        plugin::fillInfo(static_cast<std::size_t>(plugin::indexOf(p.id)), info);
        CHECK(info.id == p.id);
        CHECK(info.default_value == plugin::toHost(p, p.def));
        CHECK(((info.flags & CLAP_PARAM_IS_STEPPED) != 0) == (p.skew == synth::Skew::Stepped));
    }
}

TEST_CASE("clap params: text -> value -> text is stable for every parameter", "[clap]")
{
    // What clap-validator's param-conversions test asks: printing the value you parsed gives the same text.
    std::mt19937 rng(2);
    for (const synth::ParamInfo& p : synth::kParams) {
        for (int i = 0; i < 200; ++i) {
            const double host = plugin::isStepped(p) ? std::round(p.min + (p.max - p.min) * std::uniform_real_distribution<double>(0, 1)(rng))
                                                     : std::uniform_real_distribution<double>(0, 1)(rng);
            char t1[64], t2[64];
            REQUIRE(plugin::valueToText(p, host, t1, sizeof t1));
            const auto v2 = plugin::textToValue(p, t1);
            INFO(p.name << ": '" << t1 << "'");
            REQUIRE(v2.has_value());
            REQUIRE(plugin::valueToText(p, *v2, t2, sizeof t2));
            CHECK(std::string(t1) == t2);
        }
    }
    const synth::ParamInfo& mode = *synth::findParam(synth::param_id::kFilterMode);
    CHECK(plugin::textToValue(mode, "low-PASS") == 1.0);
    CHECK(plugin::textToValue(mode, "3") == 3.0);
    CHECK_FALSE(plugin::textToValue(mode, "wobble").has_value());
    const synth::ParamInfo& cutoff = *synth::findParam(synth::param_id::kCutoff);
    char t[64];
    plugin::valueToText(cutoff, plugin::toHost(cutoff, 999.96f), t, sizeof t);
    CHECK(std::string(t) == "1000 Hz"); // rounding crosses a decade: decimals chosen after rounding
    CHECK(plugin::textToValue(cutoff, " 440 Hz ").has_value());
}

// ---- the plugin, through its C tables ------------------------------------------------------------------

TEST_CASE("clap plugin: host automation reaches the Engine and get_value", "[clap]")
{
    Plugin pl;
    InList in;
    const auto& cutoff = *synth::findParam(synth::param_id::kCutoff);
    in.param(10, cutoff.id, 0.25, &cutoff);                                     // with a cookie
    in.param(20, synth::param_id::kResonance, plugin::toHost(*synth::findParam(synth::param_id::kResonance), 4.0f)); // without
    std::vector<float> l(64), r(64);
    pl.run(64, &in, nullptr, l.data(), r.data());
    CHECK(pl.value(cutoff.id) == plugin::toHost(cutoff, plugin::fromHost(cutoff, 0.25)));
    CHECK(std::abs(pl.impl().mainPatch().filter.q - 4.0f) < 1e-4f);

    // Wrong namespace: same type number, someone else's event. Must be ignored.
    InList bad;
    clap_event_param_value e{};
    e.header = {sizeof e, 0, static_cast<std::uint16_t>(CLAP_CORE_EVENT_SPACE_ID + 1), CLAP_EVENT_PARAM_VALUE, 0};
    e.param_id = cutoff.id;
    e.value = 0.9;
    bad.add(e);
    pl.run(64, &bad, nullptr, l.data(), r.data());
    CHECK(pl.value(cutoff.id) == plugin::toHost(cutoff, plugin::fromHost(cutoff, 0.25)));
}

TEST_CASE("clap plugin: an editor gesture goes to the host as BEGIN, VALUE, END in order", "[clap]")
{
    Plugin pl;
    const std::size_t i = indexOf(synth::param_id::kCutoff);
    const int before = gFlushRequests;
    pl.impl().uiBegin(i);
    pl.impl().uiSet(i, 500.0f);
    pl.impl().uiSet(i, 600.0f);
    pl.impl().uiEnd(i);
    CHECK(gFlushRequests > before); // the plugin asked the host for a flush/process
    OutList out;
    std::vector<float> l(32), r(32);
    pl.run(32, nullptr, &out, l.data(), r.data());
    REQUIRE(out.events.size() == 4);
    CHECK(out.at(0).type == CLAP_EVENT_PARAM_GESTURE_BEGIN);
    CHECK(out.at(1).type == CLAP_EVENT_PARAM_VALUE);
    CHECK(out.at(2).type == CLAP_EVENT_PARAM_VALUE);
    CHECK(out.at(3).type == CLAP_EVENT_PARAM_GESTURE_END);
    CHECK(pl.impl().mainPatch().filter.cutoffHz == 600.0f);
}

TEST_CASE("clap plugin: the knob under the mouse ignores automation echoes until the gesture ends", "[clap]")
{
    Plugin pl;
    const auto& cutoff = *synth::findParam(synth::param_id::kCutoff);
    const std::size_t i = indexOf(cutoff.id);
    std::vector<float> l(32), r(32);
    pl.impl().uiBegin(i);
    pl.impl().uiSet(i, 500.0f);
    InList in;
    in.param(5, cutoff.id, plugin::toHost(cutoff, 3000.0f)); // the host's automation lane fights the user
    pl.run(32, &in, nullptr, l.data(), r.data());
    CHECK(pl.impl().mainPatch().filter.cutoffHz == 500.0f); // still the user's value while dragging
    pl.impl().uiEnd(i);
    pl.run(32, nullptr, nullptr, l.data(), r.data());
    // After the gesture the mirror converges to what the audio side really has (the last applied value).
    CHECK(std::abs(pl.impl().mainPatch().filter.cutoffHz - 3000.0f) < 0.5f);
}

TEST_CASE("clap plugin: a flooded editor queue loses nothing (resync)", "[clap]")
{
    Plugin pl;
    const std::size_t i = indexOf(synth::param_id::kCutoff);
    for (int k = 0; k < 3000; ++k) // far more than the 1024-slot queue, with no process() in between
        pl.impl().uiSet(i, 100.0f + static_cast<float>(k));
    OutList out;
    std::vector<float> l(32), r(32);
    for (int b = 0; b < 8; ++b)
        pl.run(32, nullptr, &out, l.data(), r.data());
    // The Engine ends on the user's last value, and the host was told about it.
    const auto& last = out.at(out.events.size() - 1);
    REQUIRE(last.type == CLAP_EVENT_PARAM_VALUE);
    const auto& pv = reinterpret_cast<const clap_event_param_value&>(last);
    CHECK(std::abs(plugin::fromHost(synth::kParams[i], pv.value) - 3099.0f) < 0.01f); // via the 0..1 knob position
    CHECK(pl.impl().mainPatch().filter.cutoffHz == 3099.0f);
}

TEST_CASE("clap plugin: NOTE_END once per note, after the release", "[clap]")
{
    Plugin pl;
    InList in;
    in.note(0, CLAP_EVENT_NOTE_ON, 60, 7);
    in.note(0, CLAP_EVENT_NOTE_ON, 64, 8);
    in.note(100, CLAP_EVENT_NOTE_OFF, 60, 7);
    in.note(100, CLAP_EVENT_NOTE_OFF, 64, 8);
    OutList out;
    std::vector<float> l(512), r(512);
    pl.run(512, &in, &out, l.data(), r.data());
    CHECK(out.events.empty()); // still releasing
    for (int b = 0; b < 200; ++b)  // ~2 s
        pl.run(512, nullptr, &out, l.data(), r.data());
    int ends = 0;
    for (std::size_t k = 0; k < out.events.size(); ++k)
        if (out.at(k).type == CLAP_EVENT_NOTE_END) {
            const auto& n = reinterpret_cast<const clap_event_note&>(out.at(k));
            CHECK((n.note_id == 7 || n.note_id == 8));
            ++ends;
        }
    CHECK(ends == 2);
    // ...and once everything is silent, the plugin tells the host it may sleep.
    CHECK(pl.run(512, nullptr, nullptr, l.data(), r.data()) == CLAP_PROCESS_SLEEP);
}

TEST_CASE("clap plugin: thousands of events in one block are exact (event-list split)", "[clap]")
{
    // 3000 automation points in one 4096-frame block: more than the plugin's 1024-event list, so it must
    // split the block. A bare Engine given all 3000 in one list is the reference.
    Plugin pl;
    InList in;
    std::vector<Event> ref;
    in.note(0, CLAP_EVENT_NOTE_ON, 57, 1);
    ref.push_back(Event::noteOn(0, 57, 0.8f, 1));
    const auto& cutoff = *synth::findParam(synth::param_id::kCutoff);
    in.param(0, synth::param_id::kFilterMode, 1.0);
    ref.push_back(Event::paramChange(0, synth::param_id::kFilterMode, 1.0f));
    for (std::uint32_t k = 0; k < 3000; ++k) {
        const double v = 0.5 + 0.4 * std::sin(k * 0.02);
        in.param(k, cutoff.id, v);
        ref.push_back(Event::paramChange(k, cutoff.id, plugin::fromHost(cutoff, v)));
    }
    std::vector<float> l(4096), r(4096), rl(4096), rr(4096);
    pl.run(4096, &in, nullptr, l.data(), r.data());
    Engine e;
    e.prepare(48000.0, 4096);
    e.process(ref, rl.data(), rr.data(), 4096);
    CHECK(l == rl);
    CHECK(r == rr);
}

TEST_CASE("clap plugin: missing buffers still advance time exactly", "[clap]")
{
    // Block 1 with no buffers (or one channel), block 2 normal: block 2 must equal a run where block 1 had
    // buffers. Time and events must not depend on whether the host gave us somewhere to write.
    const int mode = GENERATE(0, 1);
    Plugin a, b;
    std::vector<float> l(700), r(700), l2(700), r2(700);
    for (Plugin* pl : {&a, &b}) {
        InList in;
        in.note(3, CLAP_EVENT_NOTE_ON, 60, 1);
        in.param(600, synth::param_id::kOutputDb, plugin::toHost(*synth::findParam(synth::param_id::kOutputDb), -6.0f));
        if (pl == &a)
            pl->run(700, &in, nullptr, mode == 0 ? nullptr : l.data(), nullptr); // no buffers / 1 channel
        else
            pl->run(700, &in, nullptr, l.data(), r.data());
    }
    a.run(700, nullptr, nullptr, l.data(), r.data());
    b.run(700, nullptr, nullptr, l2.data(), r2.data());
    CHECK(l == l2);
    CHECK(r == r2);
}

TEST_CASE("clap plugin: 0-frame blocks and flush() apply events", "[clap]")
{
    Plugin pl;
    InList in;
    in.param(0, synth::param_id::kFilterMode, 2.0);
    pl.run(0, &in, nullptr, nullptr, nullptr);
    CHECK(pl.value(synth::param_id::kFilterMode) == 2.0);

    Plugin idle(false); // never activated: flush() runs on the main thread and owns the Engine
    InList in2;
    in2.param(0, synth::param_id::kVoiceMode, 2.0);
    idle.params->flush(idle.p, &in2.iface, nullptr);
    CHECK(idle.value(synth::param_id::kVoiceMode) == 2.0);
}

TEST_CASE("clap plugin: process() never allocates", "[clap][realtime]")
{
    REQUIRE(util::realtimeTrapInstalled());
    util::setRealtimeTrapMode(util::RealtimeTrapMode::Count);
    Plugin pl;
    std::mt19937 rng(3);
    std::vector<float> l(1024), r(1024);
    util::resetRealtimeViolations();
    for (int b = 0; b < 400; ++b) {
        InList in; // built OUTSIDE process (the host's job), so its allocations don't count
        for (int k = 0; k < 8; ++k) {
            const auto t = static_cast<std::uint32_t>(rng() % 256);
            if (rng() % 2)
                in.note(t, static_cast<std::uint16_t>(rng() % 3 ? CLAP_EVENT_NOTE_ON : CLAP_EVENT_NOTE_OFF), 40 + static_cast<int>(rng() % 40),
                        static_cast<int>(rng() % 100));
            else
                in.param(t, synth::kParams[rng() % synth::kParams.size()].id, std::uniform_real_distribution<double>(0, 1)(rng));
        }
        std::sort(in.storage.begin(), in.storage.end(), [](const auto& x, const auto& y) {
            return reinterpret_cast<const clap_event_header*>(x.data())->time <
                   reinterpret_cast<const clap_event_header*>(y.data())->time;
        });
        if (b % 50 == 0)
            pl.impl().uiSet(indexOf(synth::param_id::kCutoff), 300.0f + static_cast<float>(b));
        // The fake host's output list must not allocate either: it runs inside the plugin's guard. Count only.
        int count = 0;
        clap_output_events counting{&count, [](const clap_output_events* o, const clap_event_header*) {
                                        ++*static_cast<int*>(o->ctx);
                                        return true;
                                    }};
        float* ch[2] = {l.data(), r.data()};
        clap_audio_buffer buf{};
        buf.data32 = ch;
        buf.channel_count = 2;
        clap_process proc{};
        proc.frames_count = 256;
        proc.audio_outputs = &buf;
        proc.audio_outputs_count = 1;
        proc.in_events = &in.iface;
        proc.out_events = &counting;
        pl.p->process(pl.p, &proc);
    }
    CHECK(util::realtimeViolations() == 0);
}

TEST_CASE("clap state v0: round trip, and corrupted bytes never crash the loader", "[clap][state]")
{
    synth::Patch p{};
    p.filter.cutoffHz = 1234.5f;
    p.ops[2].ratio = util::Ratio{3.5};
    synth::setParam(p, *synth::findParam(synth::param_id::kAlgorithm), 5.0f);
    synth::setParam(p, *synth::findParam(synth::param_id::op(1, synth::OpField::Feedback)), 0.7f);
    const auto bytes = plugin::state_v0::save(p);
    synth::Patch q;
    REQUIRE(plugin::state_v0::load(bytes, q));
    for (const synth::ParamInfo& info : synth::kParams)
        CHECK(synth::getParam(q, info) == synth::getParam(p, info));

    std::mt19937 rng(4);
    int accepted = 0;
    for (int trial = 0; trial < 5000; ++trial) {
        auto b = bytes;
        const int flips = 1 + static_cast<int>(rng() % 8);
        for (int f = 0; f < flips; ++f)
            b[rng() % b.size()] ^= static_cast<std::byte>(1u << (rng() % 8));
        if (rng() % 4 == 0)
            b.resize(rng() % (b.size() + 20));
        synth::Patch r;
        if (plugin::state_v0::load(b, r)) {
            ++accepted;
            for (const synth::ParamInfo& info : synth::kParams) { // whatever got in is in range
                const float v = synth::getParam(r, info);
                CHECK((v >= info.min - 1e-3f * std::abs(info.min) && v <= info.max * 1.0001f + 1e-6f));
            }
        }
    }
    SUCCEED("accepted " << accepted << " of 5000 mutated states (values clamped)");
}

// ---- state through the plugin (L3.4) ---------------------------------------------------------------------

namespace {
struct MemOut {
    std::string bytes;
    clap_ostream iface{this, [](const clap_ostream* s, const void* b, std::uint64_t n) -> std::int64_t {
                           static_cast<MemOut*>(s->ctx)->bytes.append(static_cast<const char*>(b), n);
                           return static_cast<std::int64_t>(n);
                       }};
};
struct MemIn {
    std::string bytes;
    std::size_t pos = 0;
    clap_istream iface{this, [](const clap_istream* s, void* b, std::uint64_t n) -> std::int64_t {
                           auto* self = static_cast<MemIn*>(s->ctx);
                           const std::size_t take = std::min<std::size_t>({static_cast<std::size_t>(n), self->bytes.size() - self->pos, 5});
                           std::memcpy(b, self->bytes.data() + self->pos, take);
                           self->pos += take;
                           return static_cast<std::int64_t>(take);
                       }};
};
const clap_plugin_state* stateOf(const Plugin& pl)
{
    return static_cast<const clap_plugin_state*>(pl.p->get_extension(pl.p, CLAP_EXT_STATE));
}
} // namespace

TEST_CASE("clap state: saves the text format, with the preset name", "[clap][state]")
{
    Plugin pl(false);
    pl.impl().applyPatchFromMain(synth::presets::bell());
    pl.impl().setPresetName("Bell");
    MemOut out;
    REQUIRE(stateOf(pl)->save(pl.p, &out.iface));
    CHECK(out.bytes.rfind("CFS patch 1\n", 0) == 0);
    Plugin other(false);
    MemIn in{out.bytes};
    REQUIRE(stateOf(other)->load(other.p, &in.iface));
    CHECK(other.impl().presetName() == "Bell");
    for (const synth::ParamInfo& info : synth::kParams)
        CHECK(synth::getParam(other.impl().mainPatch(), info) == synth::getParam(synth::presets::bell(), info));
}

TEST_CASE("clap state: a project saved by the L3.2 build (binary CFS0) still loads", "[clap][state]")
{
    // A golden fixture, written out by hand from the v0 layout (not produced by today's code): magic, version
    // 0, two entries (cutoff 500 Hz, algorithm 3), little-endian. This is what an FL Studio project saved
    // between L3.2 and L3.4 contains.
    const unsigned char v0[] = {'C', 'F', 'S', '0', 0, 0, 0, 0, 2, 0, 0, 0,
                                11, 0, 0, 0, 0x00, 0x00, 0xFA, 0x43,  // id 11 = 500.0f
                                1, 0, 0, 0, 0x00, 0x00, 0x40, 0x40}; // id 1 = 3.0f
    Plugin pl(false);
    MemIn in{std::string(reinterpret_cast<const char*>(v0), sizeof v0)};
    REQUIRE(stateOf(pl)->load(pl.p, &in.iface));
    CHECK(pl.impl().mainPatch().filter.cutoffHz == 500.0f);
    CHECK(pl.impl().mainPatch().algorithmNumber == 3);
    CHECK(pl.impl().mainPatch().ops[0].env.decayMs == synth::OperatorParams{}.env.decayMs); // the rest: defaults
    // ...and the next save upgrades the project to the text format.
    MemOut out;
    REQUIRE(stateOf(pl)->save(pl.p, &out.iface));
    CHECK(out.bytes.rfind("CFS patch 1\n", 0) == 0);
}

TEST_CASE("clap state: loading while active reaches the Engine through the queue", "[clap][state]")
{
    Plugin pl; // active
    MemIn in{synth::patchToText(synth::presets::organ(), "Organ")};
    REQUIRE(stateOf(pl)->load(pl.p, &in.iface));
    OutList out;
    std::vector<float> l(64), r(64);
    pl.run(64, nullptr, &out, l.data(), r.data());
    CHECK(out.events.empty()); // a state load is not a user edit: nothing for the host to record
    CHECK(pl.impl().mainPatch().algorithmNumber == 8);
}

TEST_CASE("clap plugin: clap.tail reports the FX chain's tail (none until an effect with a tail is on)", "[clap][fx]")
{
    Plugin pl;
    const auto* tail = static_cast<const clap_plugin_tail*>(pl.p->get_extension(pl.p, CLAP_EXT_TAIL));
    REQUIRE(tail != nullptr);
    CHECK(tail->get(pl.p) == 0u); // width only (L5.1): no memory, no tail
    // Switch width on through host automation: still no tail, and the plugin keeps working.
    InList in;
    in.param(0, synth::param_id::kWidthOn, 1.0);
    std::vector<float> l(64), r(64);
    pl.run(64, &in, nullptr, l.data(), r.data());
    CHECK(pl.impl().mainPatch().fx.width.enabled);
    CHECK(tail->get(pl.p) == 0u);
}

TEST_CASE("clap plugin: the host tempo (transport) reaches the synced delay, and clap.tail follows it", "[clap][fx]")
{
    Plugin pl;
    const auto* tail = static_cast<const clap_plugin_tail*>(pl.p->get_extension(pl.p, CLAP_EXT_TAIL));
    REQUIRE(tail != nullptr);
    InList in;
    in.param(0, synth::param_id::kDelayOn, 1.0);
    in.param(0, synth::param_id::kDelayFeedback, plugin::toHost(*synth::findParam(synth::param_id::kDelayFeedback), 0.0f));
    std::vector<float> l(64), r(64);
    clap_event_transport tr{};
    tr.header = {sizeof tr, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_TRANSPORT, 0};
    tr.flags = CLAP_TRANSPORT_HAS_TEMPO;
    tr.tempo = 90.0;
    pl.run(64, &in, nullptr, l.data(), r.data(), &tr);
    // 1/8 dotted at 90 BPM = 0.5 s = 24000 samples; feedback 0 = one echo: tail = 24000 + 1.
    CHECK(tail->get(pl.p) == 24001u);
    // A mid-block tempo change, as a CLAP_EVENT_TRANSPORT event.
    InList in2;
    clap_event_transport ev = tr;
    ev.header.time = 10;
    ev.tempo = 120.0;
    in2.add(ev);
    pl.run(64, &in2, nullptr, l.data(), r.data(), &tr); // the block starts at 90, changes at frame 10
    CHECK(tail->get(pl.p) == 18001u);
}

TEST_CASE("clap plugin: cutoff is modulatable per note, and PARAM_MOD reaches that note only", "[clap][polymod]")
{
    Plugin pl;
    clap_param_info info{};
    const int ci = plugin::indexOf(synth::param_id::kCutoff);
    REQUIRE(pl.params->get_info(pl.p, static_cast<std::uint32_t>(ci), &info));
    CHECK((info.flags & CLAP_PARAM_IS_MODULATABLE) != 0);
    CHECK((info.flags & CLAP_PARAM_IS_MODULATABLE_PER_NOTE_ID) != 0);
    REQUIRE(pl.params->get_info(pl.p, static_cast<std::uint32_t>(plugin::indexOf(synth::param_id::kOutputDb)), &info));
    CHECK((info.flags & CLAP_PARAM_IS_MODULATABLE) == 0);

    // Filter on, two notes with note ids 10 and 11; then a PARAM_MOD for note 10 only. Compare with a second
    // plugin that gets the same notes and a PARAM_MOD for a note id nobody plays (= no effect).
    auto run = [](std::int32_t modNoteId) {
        Plugin q;
        InList setup;
        setup.param(0, synth::param_id::kFilterMode, 1.0);
        clap_event_note a{}, b{};
        a.header = {sizeof a, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_NOTE_ON, 0};
        a.note_id = 10;
        a.key = 60;
        a.channel = 0;
        a.port_index = 0;
        a.velocity = 1.0;
        b = a;
        b.note_id = 11;
        b.key = 67;
        setup.add(a);
        setup.add(b);
        clap_event_param_mod m{};
        m.header = {sizeof m, 0, CLAP_CORE_EVENT_SPACE_ID, CLAP_EVENT_PARAM_MOD, 0};
        m.param_id = synth::param_id::kCutoff;
        m.note_id = modNoteId;
        m.port_index = 0;
        m.channel = -1;
        m.key = -1;
        m.amount = -0.4;
        setup.add(m);
        std::vector<float> l(2048), r(2048);
        q.run(2048, &setup, nullptr, l.data(), r.data());
        return l;
    };
    const auto modded = run(10), untouched = run(99);
    CHECK(modded != untouched);
    // And a mod for a note that doesn't exist is exactly no mod: same as a third run with note 99 again.
    CHECK(untouched == run(99));
}
