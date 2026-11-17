// midi_input_winmm.cpp: see the header for the threading story.
#include "midi_input_winmm.h"

// Trim <windows.h>: no min/max macros (they break std::min), and skip rarely used headers.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h> // midiIn*: link winmm.lib (done in CMake)

namespace fss::midi {

namespace {

// The WinMM input callback. Runs on a WinMM system thread, NOT the thread that called open().
// Rules (Microsoft's MidiInProc docs): don't call other MIDI functions, don't block, don't allocate.
// We only unpack and push into the lock-free queue, which is all of: two loads, a copy, a store.
void CALLBACK midiInProc(HMIDIIN /*handle*/, UINT msg, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR param2)
{
    if (msg != MIM_DATA)
        return; // MIM_OPEN/MIM_CLOSE: nothing to do. MIM_LONGDATA (sysex): ignored; we never add sysex
                // buffers, so none arrive. MIM_ERROR: a malformed message the driver couldn't parse; drop it.
    auto* self = reinterpret_cast<MidiInputWinmm*>(instance);
    // MIM_DATA: param1 = packed short message (status in the low byte),
    //           param2 = timestamp in ms since midiInStart(). Both fit in 32 bits.
    self->onShortMessage(static_cast<std::uint32_t>(param1), static_cast<std::uint32_t>(param2));
}

std::string toAscii(const wchar_t* s)
{
    std::string out;
    for (; *s != L'\0'; ++s)
        out.push_back(*s < 0x80 ? static_cast<char>(*s) : '?'); // keep console output ASCII-only
    return out;
}

} // namespace

MidiInputWinmm::~MidiInputWinmm() { close(); }

std::vector<std::string> MidiInputWinmm::listDevices()
{
    std::vector<std::string> names;
    const UINT count = midiInGetNumDevs();
    for (UINT i = 0; i < count; ++i)
    {
        MIDIINCAPSW caps{};
        // The W (UTF-16) variant: device names can be non-ASCII; toAscii() makes them printable.
        if (midiInGetDevCapsW(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
            names.push_back(toAscii(caps.szPname));
        else
            names.push_back("(unavailable)"); // keep index == device id
    }
    return names;
}

bool MidiInputWinmm::open(unsigned deviceIndex, std::string& error)
{
    close();
    HMIDIIN h = nullptr;
    // CALLBACK_FUNCTION: WinMM calls midiInProc directly on its own thread, with `this` as dwInstance.
    // (The alternatives, CALLBACK_WINDOW/THREAD/EVENT, route through a message queue: extra latency.)
    MMRESULT r = midiInOpen(&h, deviceIndex, reinterpret_cast<DWORD_PTR>(&midiInProc),
                            reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION);
    if (r != MMSYSERR_NOERROR)
    {
        char text[MAXERRORLENGTH] = {};
        midiInGetErrorTextA(r, text, MAXERRORLENGTH);
        error = "midiInOpen failed: " + std::string(text);
        return false;
    }
    // Input does nothing until started; timestamps count from here.
    r = midiInStart(h);
    if (r != MMSYSERR_NOERROR)
    {
        midiInClose(h);
        error = "midiInStart failed";
        return false;
    }
    handle_ = h;
    return true;
}

void MidiInputWinmm::close() noexcept
{
    if (handle_ == nullptr)
        return;
    auto h = static_cast<HMIDIIN>(handle_);
    midiInStop(h);
    midiInReset(h); // returns any pending (sysex) buffers; after this no more callbacks for this handle
    midiInClose(h);
    handle_ = nullptr;
}

void MidiInputWinmm::onShortMessage(std::uint32_t packed, std::uint32_t timestampMs) noexcept
{
    // WinMM also forwards realtime bytes like Active Sensing (0xFE, ~3 times a second from many
    // keyboards) and Clock (0xF8). The synth ignores them; dropping them here keeps the queue for notes.
    const MidiMessage m = unpackShortMessage(packed, timestampMs);
    if (m.status >= 0xF8u)
        return;
    if (!queue_.push(m))
        dropped_.fetch_add(1, std::memory_order_relaxed); // full: drop and count; never wait here
}

} // namespace fss::midi
