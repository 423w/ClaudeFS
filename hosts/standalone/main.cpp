// fss_standalone (L2.1): play fss::Engine through the default audio device, live.
//
//   fss_standalone [--seconds N] [--period FRAMES] [--rate HZ] [--variable] [--glitch-ms MS] [--keys] [--exclusive]
//                  [--midi N] [--list-midi]
//
//   default   plays a hard-coded chord (L2.1 step 3), releases it, prints device + timing statistics
//   --keys    play with the computer keyboard while the console window has focus (L2.1 step 5):
//             Z S X D C V G B H N J M = C3..B3, Q 2 W 3 E R 5 T 6 Y 7 U = C4..B4. Esc quits early.
//
// What to look for in the printout (see the L2.1 journal):
//   - granted vs requested: the backend may run at another rate or period than we asked for.
//   - callback interval: averages period/rate, but individual intervals jitter, and with miniaudio's
//     fixed-size buffering they can come in bursts when our period doesn't match the backend's.
//   - work time vs budget: how much of each deadline the synth uses. --glitch-ms deliberately blows the
//     budget (step 4) so you can hear what a deadline miss sounds like.
#include "audio_device.h"

#include <fss/engine.h>
#include <fss/synth/presets.h>
#include <fss/util/realtime_guard.h>
#include <fss/util/spsc_queue.h>

#include "midi_input_winmm.h"
#include "midi_stream.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <span>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace fss::standalone {
namespace {

using Clock = std::chrono::steady_clock; // monotonic: never jumps when the wall clock is adjusted

/// One row per callback, written by the audio thread into a preallocated array.
struct CallbackRecord {
    std::int64_t startNs = 0; // callback entry, relative to the first callback
    std::int64_t workNs = 0;  // time spent inside render()
    std::uint32_t frames = 0;
};

constexpr int kMaxBlock = 1024; // the Engine's block limit; bigger callbacks are processed in chunks

/// The audio-thread object: the Engine, its scratch buffers, a key-state inbox, and a timing recorder.
class EngineHost final : public RenderCallback {
public:
    /// Main thread, before start(): everything that allocates or depends on the sample rate happens here.
    void prepare(double sampleRateHz, std::size_t maxRecords, double glitchMs)
    {
        engine_.prepare(sampleRateHz, kMaxBlock);
        engine_.setPatch(synth::presets::epiano());
        records_.assign(maxRecords, CallbackRecord{});
        count_.store(0, std::memory_order_relaxed);
        glitch_ = std::chrono::microseconds(static_cast<std::int64_t>(glitchMs * 1000.0));
    }

    // ---- other threads → audio thread (L2.3) -------------------------------------------------------
    // Two single-producer queues, because SPSC means exactly one producer each:
    //   uiEvents_  main thread (computer keyboard, demo chord) → audio thread
    //   midi_      WinMM's callback thread → audio thread (the queue lives inside MidiInputWinmm)
    // This replaces L2.1's one-atomic-flag-per-key hand-off, which lost key taps shorter than a block.
    // A queue keeps every event, in order. If it's full, the *producer* drops the event (push returns
    // false): the audio thread must never wait, and losing a note-on is better than a glitch.
    bool postEvent(const Event& e) noexcept { return uiEvents_.push(e); }
    void attachMidi(midi::MidiInputWinmm* input) noexcept { midi_ = input; } // before start() only

    void render(float* out, std::uint32_t frames, std::uint32_t channels) noexcept override
    {
        const util::RealtimeGuard rt; // the whole callback is real-time (L2.2); Debug builds trap allocations
        const Clock::time_point t0 = Clock::now();
        if (!firstCallbackSeen_) {
            origin_ = t0;
            firstCallbackSeen_ = true;
        }

        // Drain both queues into a fixed-size event array (bounded: at most events_.size() per callback;
        // anything beyond waits in its queue for the next callback, it isn't lost). All events land on
        // frame 0 of this callback. Mapping each event's arrival time to a sample offset would remove up to
        // one callback of timing jitter: see the L2.3 journal.
        std::size_t numEvents = 0;
        Event e;
        while (numEvents < events_.size() && uiEvents_.pop(e))
            events_[numEvents++] = e;
        midi::MidiMessage m;
        while (midi_ && numEvents < events_.size() && midi_->pop(m))
            if (midi::toEvent(midi::classify(m), 0, e))
                events_[numEvents++] = e;

        // The Engine writes planar L/R; the device wants interleaved. Process in chunks of ≤ kMaxBlock.
        std::uint32_t done = 0;
        while (done < frames) {
            const auto n = static_cast<int>(std::min<std::uint32_t>(frames - done, kMaxBlock));
            const std::span<const Event> ev(events_.data(), done == 0 ? numEvents : 0);
            engine_.process(ev, left_.data(), right_.data(), n);
            for (int i = 0; i < n; ++i) {
                float* frame = out + (done + static_cast<std::uint32_t>(i)) * channels;
                frame[0] = left_[static_cast<std::size_t>(i)];
                for (std::uint32_t c = 1; c < channels; ++c)
                    frame[c] = right_[static_cast<std::size_t>(i)];
                peak_ = std::max(peak_, std::fabs(left_[static_cast<std::size_t>(i)]));
            }
            done += static_cast<std::uint32_t>(n);
        }

        if (glitch_.count() > 0) {
            // DELIBERATE real-time violation (L2.1 step 4): sleeping hands the CPU back to the OS scheduler,
            // which may not return it for longer than asked. Past the budget = audible clicks/dropouts.
            std::this_thread::sleep_for(glitch_);
        }

        const Clock::time_point t1 = Clock::now();
        const std::size_t n = count_.load(std::memory_order_relaxed); // we're the only writer
        if (n < records_.size()) {
            records_[n] = {std::chrono::duration_cast<std::chrono::nanoseconds>(t0 - origin_).count(),
                           std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count(), frames};
            // release: a reader that sees the new count also sees the record written before it.
            count_.store(n + 1, std::memory_order_release);
        }
    }

    /// Main thread, after AudioDevice::stop() (which guarantees render() is no longer running).
    [[nodiscard]] std::size_t recordCount() const noexcept { return count_.load(std::memory_order_acquire); }
    [[nodiscard]] const std::vector<CallbackRecord>& records() const noexcept { return records_; }
    [[nodiscard]] float peak() const noexcept { return peak_; }

private:
    Engine engine_;
    std::array<float, kMaxBlock> left_{}, right_{};
    std::array<Event, 128> events_{};
    util::SpscQueue<Event, 256> uiEvents_;
    midi::MidiInputWinmm* midi_ = nullptr;
    std::vector<CallbackRecord> records_;
    std::atomic<std::size_t> count_{0};
    // Lock-free atomics are a real-time requirement, not a given: a non-lock-free atomic hides a mutex.
    static_assert(std::atomic<bool>::is_always_lock_free && std::atomic<std::size_t>::is_always_lock_free);
    bool firstCallbackSeen_ = false;
    Clock::time_point origin_{};
    std::chrono::microseconds glitch_{0};
    float peak_ = 0.0f;
};

struct Options {
    double seconds = 3.0;
    std::uint32_t periodFrames = 256;
    std::uint32_t rateHz = 48000;
    bool variable = false;
    bool keys = false;
    bool exclusive = false;
    bool listMidi = false;
    int midiDevice = -1; // -1 = no MIDI input
    double glitchMs = 0.0;
};

template <typename T>
bool parseNumber(std::string_view text, T& out)
{
    const char* end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(text.data(), end, out);
    return ec == std::errc{} && ptr == end;
}

void printUsage()
{
    std::puts("usage: fss_standalone [--seconds N] [--period FRAMES] [--rate HZ] [--variable] [--glitch-ms MS] [--keys] [--exclusive] [--midi N] [--list-midi]\n"
              "  --seconds N      total play time, 0.5..600 (default 3)\n"
              "  --period FRAMES  requested frames per callback, 16..8192 (default 256)\n"
              "  --rate HZ        requested sample rate, 8000..384000 (default 48000)\n"
              "  --variable       let callback sizes vary (no miniaudio fixed-size buffering)\n"
              "  --glitch-ms MS   sleep MS inside every callback: deliberately miss deadlines (0..100)\n"
              "  --keys           play with the computer keyboard (Z..M, Q..U rows); Esc quits\n"
              "  --exclusive      WASAPI exclusive mode (lower latency if the device allows it)\n"
              "  --midi N         play from MIDI input device N (see --list-midi); runs for --seconds\n"
              "  --list-midi      list MIDI input devices and exit");
}

bool parseArgs(int argc, char** argv, Options& o)
{
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        const bool hasValue = i + 1 < argc;
        bool ok = true;
        if (a == "--seconds" && hasValue) {
            ok = parseNumber(argv[++i], o.seconds) && o.seconds >= 0.5 && o.seconds <= 600.0;
        } else if (a == "--period" && hasValue) {
            ok = parseNumber(argv[++i], o.periodFrames) && o.periodFrames >= 16 && o.periodFrames <= 8192;
        } else if (a == "--rate" && hasValue) {
            ok = parseNumber(argv[++i], o.rateHz) && o.rateHz >= 8000 && o.rateHz <= 384000;
        } else if (a == "--glitch-ms" && hasValue) {
            ok = parseNumber(argv[++i], o.glitchMs) && o.glitchMs >= 0.0 && o.glitchMs <= 100.0;
        } else if (a == "--variable") {
            o.variable = true;
        } else if (a == "--exclusive") {
            o.exclusive = true;
        } else if (a == "--list-midi") {
            o.listMidi = true;
        } else if (a == "--midi" && hasValue) {
            ok = parseNumber(argv[++i], o.midiDevice) && o.midiDevice >= 0;
        } else if (a == "--keys") {
            o.keys = true;
        } else {
            ok = false;
        }
        if (!ok) {
            std::fprintf(stderr, "error: bad or out-of-range argument near '%s'\n", argv[i]);
            return false;
        }
    }
    return true;
}

void printStats(const EngineHost& host, const DeviceInfo& info)
{
    const std::size_t n = host.recordCount();
    const auto& rec = host.records();
    if (n < 2) {
        std::puts("too few callbacks recorded to compute statistics");
        return;
    }
    std::vector<double> intervalMs;
    intervalMs.reserve(n - 1);
    for (std::size_t i = 1; i < n; ++i)
        intervalMs.push_back(static_cast<double>(rec[i].startNs - rec[i - 1].startNs) * 1e-6);

    std::uint32_t minFrames = rec[0].frames, maxFrames = rec[0].frames;
    std::uint64_t totalFrames = 0;
    double maxWorkMs = 0.0, sumWorkMs = 0.0, maxLoad = 0.0;
    std::size_t overBudget = 0;
    for (std::size_t i = 0; i < n; ++i) {
        minFrames = std::min(minFrames, rec[i].frames);
        maxFrames = std::max(maxFrames, rec[i].frames);
        totalFrames += rec[i].frames;
        const double workMs = static_cast<double>(rec[i].workNs) * 1e-6;
        const double budgetMs = 1000.0 * rec[i].frames / info.sampleRateHz; // the audio time this callback made
        maxWorkMs = std::max(maxWorkMs, workMs);
        sumWorkMs += workMs;
        maxLoad = std::max(maxLoad, workMs / budgetMs);
        if (workMs > budgetMs)
            ++overBudget;
    }
    const auto [minIt, maxIt] = std::minmax_element(intervalMs.begin(), intervalMs.end());
    double sumInterval = 0.0;
    for (double v : intervalMs)
        sumInterval += v;
    std::vector<double> sorted = intervalMs;
    std::sort(sorted.begin(), sorted.end());
    const double p99 = sorted[std::min(sorted.size() - 1, sorted.size() * 99 / 100)];
    const double nominalMs = 1000.0 * (static_cast<double>(totalFrames) / static_cast<double>(n)) / info.sampleRateHz;

    std::printf("callbacks:            %zu (frames per callback min %u / max %u)\n", n, minFrames, maxFrames);
    std::printf("callback interval ms: min %.3f  avg %.3f  p99 %.3f  max %.3f  (nominal %.3f)\n", *minIt,
                sumInterval / static_cast<double>(intervalMs.size()), p99, *maxIt, nominalMs);
    std::printf("work per callback ms: avg %.4f  max %.4f  (peak load %.2f%% of a callback's budget)\n",
                sumWorkMs / static_cast<double>(n), maxWorkMs, 100.0 * maxLoad);
    std::printf("over budget:          %zu callbacks\n", overBudget);
    std::printf("output peak:          %.3f (%.1f dBFS)\n", static_cast<double>(host.peak()),
                20.0 * std::log10(std::max(1e-9, static_cast<double>(host.peak()))));
}

/// Hard-coded demo (L2.1 step 3): an A minor chord, held, then released.
void playChord(EngineHost& host, double seconds)
{
    constexpr std::uint8_t chord[] = {57, 60, 64};
    for (std::uint8_t k : chord)
        host.postEvent(Event::noteOn(0, k, 0.8f));
    std::this_thread::sleep_for(std::chrono::duration<double>(std::max(0.0, seconds - 1.0)));
    for (std::uint8_t k : chord)
        host.postEvent(Event::noteOff(0, k));
    std::this_thread::sleep_for(std::chrono::duration<double>(std::min(seconds, 1.0)));
}

/// L2.1 step 5: poll the computer keyboard (1 ms) while our console window is in front.
void playKeys(EngineHost& host, double seconds)
{
#ifdef _WIN32
    // Virtual-key codes for two rows of a piano: the classic "tracker" layout.
    constexpr std::pair<int, std::uint8_t> map[] = {
        {'Z', 48}, {'S', 49}, {'X', 50}, {'D', 51}, {'C', 52}, {'V', 53}, {'G', 54}, {'B', 55}, {'H', 56},
        {'N', 57}, {'J', 58}, {'M', 59}, {'Q', 60}, {'2', 61}, {'W', 62}, {'3', 63}, {'E', 64}, {'R', 65},
        {'5', 66}, {'T', 67}, {'6', 68}, {'Y', 69}, {'7', 70}, {'U', 71}};
    std::puts("--keys: play Z..M (C3..B3) and Q..U (C4..B4) while this window has focus; Esc quits.");
    const HWND console = GetConsoleWindow();
    bool wasDown[128] = {}; // main-thread copy, so only *changes* become events
    const auto set = [&](std::uint8_t key, bool down) {
        if (down != wasDown[key] && host.postEvent(down ? Event::noteOn(0, key, 0.8f) : Event::noteOff(0, key)))
            wasDown[key] = down; // if the queue was full, try again next poll instead of losing the change
    };
    const auto end = Clock::now() + std::chrono::duration<double>(seconds);
    while (Clock::now() < end) {
        const bool focused = GetForegroundWindow() == console; // don't read keys typed into other apps
        if (focused && (GetAsyncKeyState(VK_ESCAPE) & 0x8000))
            break;
        for (const auto& [vk, key] : map)
            set(key, focused && (GetAsyncKeyState(vk) & 0x8000) != 0);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for (const auto& [vk, key] : map)
        set(key, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(500)); // release tails
#else
    (void)host;
    (void)seconds;
    std::puts("--keys is Windows-only for now.");
#endif
}

int run(int argc, char** argv)
{
    Options opt;
    if (!parseArgs(argc, argv, opt)) {
        printUsage();
        return 2;
    }

    if (opt.listMidi) {
        const auto names = midi::MidiInputWinmm::listDevices();
        std::printf("%zu MIDI input device(s)\n", names.size());
        for (std::size_t i = 0; i < names.size(); ++i)
            std::printf("  %zu: %s\n", i, names[i].c_str());
        return 0;
    }

    // Big enough object to keep off the stack (the Engine holds 20 voices).
    auto host = std::make_unique<EngineHost>();
    AudioDevice device;
    DeviceConfig cfg;
    cfg.sampleRateHz = opt.rateHz;
    cfg.periodFrames = opt.periodFrames;
    cfg.fixedSizeCallback = !opt.variable;
    cfg.exclusive = opt.exclusive;
    if (!device.open(cfg, *host)) {
        // No sound card, or the default device is unplugged/disabled: a normal situation, not a crash.
        std::fprintf(stderr, "error: could not open the default playback device: %s\n", device.lastError().c_str());
        return 1;
    }

    const DeviceInfo& info = device.info();
    std::printf("backend:   %s, device \"%s\"\n", info.backendName.c_str(), info.deviceName.c_str());
    std::printf("requested: %u Hz, %u frames/period (%.2f ms)\n", opt.rateHz, opt.periodFrames,
                1000.0 * opt.periodFrames / opt.rateHz);
    std::printf("granted:   callback at %u Hz, %u ch; device internal %u Hz, %u frames x %u periods\n",
                info.sampleRateHz, info.channels, info.internalSampleRateHz, info.internalPeriodFrames,
                info.internalPeriods);
    const double fixedBufferMs = opt.variable ? 0.0 : 1000.0 * opt.periodFrames / opt.rateHz;
    std::printf("latency:   >= %.2f ms device buffering + %.2f ms fixed-size callback buffer = %.2f ms (estimate)\n",
                info.bufferLatencyMs(), fixedBufferMs, info.bufferLatencyMs() + fixedBufferMs);

    const auto maxRecords = static_cast<std::size_t>(opt.seconds * info.sampleRateHz / 16.0) + 1024;
    host->prepare(info.sampleRateHz, maxRecords, opt.glitchMs);

    // MIDI (L2.3): open before the audio starts, so the audio thread's pointer never changes while it runs.
    midi::MidiInputWinmm midiIn;
    if (opt.midiDevice >= 0) {
        std::string err;
        if (!midiIn.open(static_cast<unsigned>(opt.midiDevice), err)) {
            std::fprintf(stderr, "error: MIDI device %d: %s (try --list-midi)\n", opt.midiDevice, err.c_str());
            return 1;
        }
        host->attachMidi(&midiIn);
        std::printf("midi:      device %d open; play for %.0f s\n", opt.midiDevice, opt.seconds);
    }

    if (!device.start()) {
        std::fprintf(stderr, "error: %s\n", device.lastError().c_str());
        return 1;
    }
    // Debug builds link the allocation trap: make any audio-thread allocation stop the program on the spot.
    util::setRealtimeTrapMode(util::RealtimeTrapMode::Abort);
    if (opt.keys)
        playKeys(*host, opt.seconds);
    else if (opt.midiDevice >= 0)
        std::this_thread::sleep_for(std::chrono::duration<double>(opt.seconds)); // just listen to MIDI
    else
        playChord(*host, opt.seconds);
    device.stop();   // first the audio thread stops reading the MIDI queue...
    midiIn.close();  // ...then the MIDI thread stops writing it

    printStats(*host, info);
    std::printf("rt trap:              %s, %llu violations\n",
                util::realtimeTrapInstalled() ? "installed (Debug)" : "not linked (Release)",
                static_cast<unsigned long long>(util::realtimeViolations()));
    return 0;
}

} // namespace
} // namespace fss::standalone

int main(int argc, char** argv)
{
    return fss::standalone::run(argc, argv);
}
