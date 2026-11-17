// Tests for hosts/midi/midi_message.h (L2.3): classifying MIDI 1.0 short messages from raw bytes.
#include <midi_message.h>

#include <catch2/catch_test_macros.hpp>

using namespace fss::midi;

namespace {
DecodedMidi decode(std::uint8_t s, std::uint8_t d1 = 0, std::uint8_t d2 = 0)
{
    return classify(MidiMessage{s, d1, d2, 0});
}
} // namespace

TEST_CASE("MIDI: note on, with channel extraction", "[midi]")
{
    const auto d = decode(0x93, 60, 100); // channel 4 to a human
    CHECK(d.kind == MidiKind::NoteOn);
    CHECK(d.channel == 3);
    CHECK(d.number == 60);
    CHECK(d.value == 100);
    CHECK(decode(0x9F, 1, 1).channel == 15);
}

TEST_CASE("MIDI: note on with velocity 0 is a note off", "[midi]")
{
    const auto d = decode(0x90, 64, 0);
    CHECK(d.kind == MidiKind::NoteOff);
    CHECK(d.number == 64);
}

TEST_CASE("MIDI: note off keeps its release velocity", "[midi]")
{
    const auto d = decode(0x85, 64, 40);
    CHECK(d.kind == MidiKind::NoteOff);
    CHECK(d.channel == 5);
    CHECK(d.number == 64);
    CHECK(d.value == 40);
}

TEST_CASE("MIDI: control change, program change, pressures", "[midi]")
{
    const auto cc = decode(0xB0, 64, 127); // sustain pedal down
    CHECK(cc.kind == MidiKind::ControlChange);
    CHECK(cc.number == 64);
    CHECK(cc.value == 127);

    CHECK(decode(0xC2, 5).kind == MidiKind::ProgramChange);
    CHECK(decode(0xC2, 5).number == 5);

    const auto cp = decode(0xD0, 77); // one data byte: pressure lives in data1
    CHECK(cp.kind == MidiKind::ChannelPressure);
    CHECK(cp.value == 77);

    CHECK(decode(0xA0, 60, 10).kind == MidiKind::PolyPressure);
}

TEST_CASE("MIDI: pitch bend is 14-bit, LSB first, mapped to -1..+1", "[midi]")
{
    CHECK(decode(0xE0, 0x00, 0x40).pitchBend == 0.0f);  // 64*128 + 0 = 8192 = centre, exactly 0
    CHECK(decode(0xE0, 0x00, 0x00).pitchBend == -1.0f); // 0 = full down
    CHECK(decode(0xE0, 0x7F, 0x7F).pitchBend == 1.0f);  // 16383 = full up, exactly +1
    CHECK(decode(0xE0, 0x00, 0x20).pitchBend == -0.5f); // 4096
    CHECK(decode(0xE1, 0x01, 0x40).pitchBend > 0.0f);   // 8193: the LSB matters
    CHECK(decode(0xE1, 0x01, 0x40).channel == 1);
}

TEST_CASE("MIDI: system and invalid status bytes", "[midi]")
{
    CHECK(decode(0xF8).kind == MidiKind::System); // timing clock
    CHECK(decode(0xFE).kind == MidiKind::System); // active sensing
    CHECK(decode(0x40, 0x40).kind == MidiKind::Invalid); // a data byte where a status should be
}

TEST_CASE("MIDI: unpacking WinMM's packed DWORD", "[midi]")
{
    // midiInProc's dwParam1 for Note On, ch 1, key 60, vel 100: 0x00643C90
    const auto m = unpackShortMessage(0x00643C90u, 1234u);
    CHECK(m.status == 0x90);
    CHECK(m.data1 == 60);
    CHECK(m.data2 == 100);
    CHECK(m.timestampMs == 1234u);
    // Data bytes are masked to 7 bits: a misbehaving device can't produce velocity > 127.
    CHECK(unpackShortMessage(0x00FFFF90u, 0).data2 == 0x7F);
}
