// midi_stream.h: MIDI byte-stream parsing (running status) and MIDI → fss::Event (L2.3).
//
// WinMM hands us complete messages, but a raw MIDI stream (a serial port, a MIDI file's track data, some
// USB drivers) is just bytes, and senders may use RUNNING STATUS: after `90 3C 64` (note on, C4) the
// next note can be sent as `3E 64` with no status byte. The receiver must remember the last status.
//
// Rules implemented ([MIDI-SPEC]):
//   - A status byte 0x80..0xEF sets the running status and starts a new message.
//   - Data bytes (0x00..0x7F) fill the current message; when it has its 1 or 2 data bytes it's complete,
//     and the next data byte starts a new message with the SAME status (running status).
//   - Real-time bytes 0xF8..0xFF (clock, start, stop, active sensing) may appear *anywhere*, even between a
//     status and its data. They're ignored here and don't disturb running status.
//   - System common / sysex (0xF0..0xF7) cancel running status; sysex data is skipped until 0xF7.
//   - Data bytes with no status to belong to (stream start, after sysex) are dropped.
#pragma once

#include "midi_message.h"

#include <fss/event.h>

#include <cstdint>

namespace fss::midi {

class MidiStreamParser {
public:
    /// Feed one byte. Returns true and fills `out` when a complete channel message is ready.
    bool feed(std::uint8_t byte, MidiMessage& out) noexcept
    {
        if (byte >= 0xF8) // real-time: transparent
            return false;
        if (byte >= 0xF0) { // system common / sysex start or end: no running status across these
            running_ = 0;
            inSysex_ = (byte == 0xF0);
            count_ = 0;
            return false;
        }
        if (byte & 0x80) { // channel status byte
            running_ = byte;
            inSysex_ = false;
            count_ = 0;
            return false;
        }
        // Data byte.
        if (inSysex_ || running_ == 0)
            return false;
        data_[count_++] = byte;
        if (count_ < dataBytesFor(running_))
            return false;
        out = MidiMessage{running_, data_[0], count_ > 1 ? data_[1] : std::uint8_t{0}, 0};
        count_ = 0; // keep running_: the next data byte starts another message with the same status
        return true;
    }

    void reset() noexcept { running_ = 0, count_ = 0, inSysex_ = false; }

private:
    static constexpr int dataBytesFor(std::uint8_t status) noexcept
    {
        const std::uint8_t type = status & 0xF0;
        return (type == 0xC0 || type == 0xD0) ? 1 : 2; // program change and channel pressure have one
    }

    std::uint8_t running_ = 0; // 0 = no running status
    std::uint8_t data_[2]{};
    int count_ = 0;
    bool inSysex_ = false;
};

/// The subset of MIDI the Engine understands today, as an Event at sample offset `frame`.
/// Returns false for messages that don't map to an Event (CCs arrive with the parameter system in L2.4).
inline bool toEvent(const DecodedMidi& m, std::uint32_t frame, Event& out) noexcept
{
    switch (m.kind) {
    case MidiKind::NoteOn:
        out = Event::noteOn(frame, m.number, static_cast<float>(m.value) / 127.0f);
        out.channel = m.channel;
        return true;
    case MidiKind::NoteOff:
        out = Event::noteOff(frame, m.number);
        out.channel = m.channel;
        return true;
    case MidiKind::PitchBend:
        out = Event::pitchBend(frame, m.pitchBend);
        out.channel = m.channel;
        return true;
    case MidiKind::ControlChange:
        if (m.number == 123) { // CC 123 "All Notes Off": a panic button many controllers have
            out = Event{Event::Type::AllNotesOff, frame};
            return true;
        }
        // Every other CC goes to the Engine as a Controller event (L1.11); it reacts to the ones it knows
        // (CC 1 mod wheel; CC 64 sustain from L2.5) and ignores the rest.
        out = Event::controller(frame, m.number, static_cast<float>(m.value) / 127.0f);
        out.channel = m.channel;
        return true;
    default:
        return false;
    }
}

} // namespace fss::midi
