// midi_input_winmm.h: MIDI input from a hardware/virtual port via Windows' WinMM API (L2.3, D10).
//
// Why WinMM by hand (D10): it ships with every Windows since 95, needs nothing installed, and is ~100
// lines for input. Its limits (no MIDI 2.0, no multi-client on some drivers, Windows-only) don't matter
// for a standalone test host. The CLAP plugin gets MIDI from the host instead, so this file never ships
// inside the plugin.
//
// Threads:
//   - The *main thread* lists devices, opens, starts, closes, and is the single CONSUMER of the queue
//     (in the standalone app this becomes the audio thread).
//   - WinMM calls our callback on a *system thread it owns*. That thread is the single PRODUCER. It must
//     never block: Microsoft documents that the callback may only call a handful of functions (no
//     allocation, no other MIDI calls), and while it runs, further MIDI input for the device waits.
//     So the callback does the minimum: unpack the bytes, push into a lock-free SpscQueue, return.
//
// The header doesn't include <windows.h> (which drags in thousands of macros, `min`/`max` among them):
// the handle is stored as an opaque pointer and the .cpp does the casting.
#pragma once

#include "midi_message.h"

#include <fss/util/spsc_queue.h>

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace fss::midi {

class MidiInputWinmm
{
public:
    // 1024 messages = several seconds of dense playing even if the consumer stalls. A power of two.
    using Queue = fss::util::SpscQueue<MidiMessage, 1024>;

    MidiInputWinmm() = default;
    ~MidiInputWinmm();
    // WinMM holds a pointer to this object (the callback's dwInstance), so it must not move or copy.
    MidiInputWinmm(const MidiInputWinmm&) = delete;
    MidiInputWinmm& operator=(const MidiInputWinmm&) = delete;

    // Names of the MIDI input devices, index = device id for open(). Non-ASCII characters become '?'.
    // Main thread; allocates.
    static std::vector<std::string> listDevices();

    // Opens device `deviceIndex` and starts receiving. Returns false and fills `error` on failure
    // (bad index, device in use by another app: many drivers allow only one client).
    bool open(unsigned deviceIndex, std::string& error);
    void close() noexcept; // stops, flushes and closes; safe to call twice
    bool isOpen() const noexcept { return handle_ != nullptr; }

    // Consumer side: one thread only.
    bool pop(MidiMessage& out) noexcept { return queue_.pop(out); }

    // Messages dropped because the queue was full (the callback can't wait). Any nonzero value is a bug
    // or a stalled consumer worth reporting.
    std::uint32_t droppedCount() const noexcept { return dropped_.load(std::memory_order_relaxed); }

    // Called from the WinMM callback (public only so the free callback function in the .cpp can reach it).
    void onShortMessage(std::uint32_t packed, std::uint32_t timestampMs) noexcept;

private:
    void* handle_ = nullptr; // HMIDIIN
    Queue queue_;
    std::atomic<std::uint32_t> dropped_{0};
};

} // namespace fss::midi
