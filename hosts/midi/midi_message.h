// midi_message.h: MIDI 1.0 short messages, and a pure function that classifies them (L2.3).
//
// Pure and portable on purpose: no WinMM, no I/O, so the tests can feed it raw bytes on any OS, and the
// CLAP host (Branch 5) can reuse it, because CLAP also delivers MIDI 1.0 as three raw bytes.
//
// MIDI 1.0 in one paragraph [MIDI-SPEC]: a message is one *status* byte (top bit set, 0x80..0xFF) and
// 0-2 *data* bytes (top bit clear, 0..127). For channel messages (0x80..0xEF) the status splits into
// the message type (high nibble) and the channel (low nibble, 0..15 on the wire, shown to humans as 1..16):
//   0x8n key vel     Note Off
//   0x9n key vel     Note On  (velocity 0 means Note Off! keyboards use it to exploit running status)
//   0xAn key press   Polyphonic aftertouch
//   0xBn cc  value   Control Change (CC 64 = sustain pedal, CC 1 = mod wheel, ...)
//   0xCn prog        Program Change (one data byte)
//   0xDn press       Channel pressure / aftertouch (one data byte)
//   0xEn lsb msb     Pitch Bend: a 14-bit value, LSB first; 8192 = centre
//   0xF0..0xFF       System messages (sysex, clock, active sensing, ...): not per-channel
// Running status (a sender may omit a repeated status byte) is a *byte-stream* concern. WinMM and CLAP
// both hand us complete 3-byte messages with the status filled back in, so nothing here deals with it.
// (A raw byte-stream parser with running status is part of the L2.3 lesson proper; see the journal.)
#pragma once

#include <cstdint>

namespace fss::midi {

// One short MIDI message as it crosses from the MIDI thread to the audio thread through an SpscQueue.
// Trivially copyable (just bytes), so pushing and popping it is a plain copy.
struct MidiMessage
{
    std::uint8_t status = 0;      // type in the high nibble + channel in the low nibble (channel messages)
    std::uint8_t data1 = 0;       // 0..127: key, CC number, program, pressure, or pitch-bend LSB
    std::uint8_t data2 = 0;       // 0..127: velocity, CC value, or pitch-bend MSB (0 if unused)
    std::uint32_t timestampMs = 0; // WinMM: milliseconds since midiInStart(); used for jitter work later
};

// WinMM packs a short message into a 32-bit integer, status in the lowest byte:
//   bits 0-7 status, 8-15 data1, 16-23 data2, 24-31 unused.
// Data bytes are masked to 7 bits: a well-behaved device never sets bit 7 there, a broken one must not
// turn a velocity of 200 into something the synth then trusts.
constexpr MidiMessage unpackShortMessage(std::uint32_t packed, std::uint32_t timestampMs) noexcept
{
    return MidiMessage{static_cast<std::uint8_t>(packed & 0xFFu),
                       static_cast<std::uint8_t>((packed >> 8) & 0x7Fu),
                       static_cast<std::uint8_t>((packed >> 16) & 0x7Fu),
                       timestampMs};
}

enum class MidiKind : std::uint8_t
{
    NoteOff,
    NoteOn,
    PolyPressure,
    ControlChange,
    ProgramChange,
    ChannelPressure,
    PitchBend,
    System,  // 0xF0..0xFF: not per-channel; the synth ignores these for now
    Invalid, // first byte isn't a status byte (bit 7 clear): garbage or a lost running-status context
};

// A message in the synth's terms. Only the fields that make sense for `kind` are meaningful.
struct DecodedMidi
{
    MidiKind kind = MidiKind::Invalid;
    std::uint8_t channel = 0;  // 0..15 (add 1 when showing it to a human)
    std::uint8_t number = 0;   // key (notes, poly pressure), CC number, or program
    std::uint8_t value = 0;    // velocity, CC value, pressure: 0..127
    float pitchBend = 0.0f;    // PitchBend only: -1..+1, 0 = centre
};

// Maps the 14-bit pitch-bend value (0..16383, centre 8192) to -1..+1 with centre exactly 0.
// The range is lopsided: 8192 steps below centre, only 8191 above. Dividing both sides by 8192 would
// make full-up 0.99988 instead of 1.0, so we scale each side separately. Both ends then hit exactly
// -1 and +1, at the cost of steps 1/8191 vs 1/8192 in size: far below anything audible.
constexpr float pitchBendToUnit(std::uint16_t value14) noexcept
{
    const int centred = static_cast<int>(value14) - 8192; // -8192..+8191
    return centred < 0 ? static_cast<float>(centred) / 8192.0f : static_cast<float>(centred) / 8191.0f;
}

constexpr DecodedMidi classify(const MidiMessage& m) noexcept
{
    DecodedMidi d;
    if ((m.status & 0x80u) == 0)
        return d; // Invalid
    if (m.status >= 0xF0u)
    {
        d.kind = MidiKind::System;
        return d;
    }

    d.channel = static_cast<std::uint8_t>(m.status & 0x0Fu);
    const std::uint8_t data1 = static_cast<std::uint8_t>(m.data1 & 0x7Fu);
    const std::uint8_t data2 = static_cast<std::uint8_t>(m.data2 & 0x7Fu);
    switch (m.status & 0xF0u)
    {
    case 0x80u:
        d.kind = MidiKind::NoteOff;
        d.number = data1;
        d.value = data2; // release velocity: most synths ignore it, but it's real data
        break;
    case 0x90u:
        // Note On with velocity 0 IS a Note Off [MIDI-SPEC]. Senders use it with running status so a
        // whole performance can be sent as 0x9n + pairs of bytes. Missing this = stuck notes.
        d.kind = data2 == 0 ? MidiKind::NoteOff : MidiKind::NoteOn;
        d.number = data1;
        d.value = data2;
        break;
    case 0xA0u:
        d.kind = MidiKind::PolyPressure;
        d.number = data1;
        d.value = data2;
        break;
    case 0xB0u:
        d.kind = MidiKind::ControlChange;
        d.number = data1;
        d.value = data2;
        break;
    case 0xC0u:
        d.kind = MidiKind::ProgramChange;
        d.number = data1;
        break;
    case 0xD0u:
        d.kind = MidiKind::ChannelPressure;
        d.value = data1; // one data byte: the pressure is in data1, not data2
        break;
    case 0xE0u:
    default: // only 0xE0 can reach here: 0x80..0xD0 are handled above and >= 0xF0 returned early
        d.kind = MidiKind::PitchBend;
        // LSB first, 7 bits each: value = msb * 128 + lsb.
        d.pitchBend = pitchBendToUnit(static_cast<std::uint16_t>((data2 << 7) | data1));
        break;
    }
    return d;
}

// For logs and the monitor tool. ASCII only.
constexpr const char* kindName(MidiKind k) noexcept
{
    switch (k)
    {
    case MidiKind::NoteOff: return "NoteOff";
    case MidiKind::NoteOn: return "NoteOn";
    case MidiKind::PolyPressure: return "PolyPressure";
    case MidiKind::ControlChange: return "CC";
    case MidiKind::ProgramChange: return "Program";
    case MidiKind::ChannelPressure: return "ChanPressure";
    case MidiKind::PitchBend: return "PitchBend";
    case MidiKind::System: return "System";
    case MidiKind::Invalid: break;
    }
    return "Invalid";
}

} // namespace fss::midi
