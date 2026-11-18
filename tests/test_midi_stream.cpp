// Tests for hosts/midi/midi_stream.h (L2.3): running status from raw bytes, and MIDI -> fss::Event.
#include "midi_stream.h"

#include <catch2/catch_test_macros.hpp>

#include <initializer_list>
#include <vector>

using namespace fss;
using namespace fss::midi;

namespace {
std::vector<MidiMessage> parse(std::initializer_list<std::uint8_t> bytes)
{
    MidiStreamParser p;
    std::vector<MidiMessage> out;
    MidiMessage m;
    for (std::uint8_t b : bytes)
        if (p.feed(b, m))
            out.push_back(m);
    return out;
}
} // namespace

TEST_CASE("MidiStreamParser: complete messages and running status", "[midi][stream]")
{
    // Note on C4, then two more notes with running status, then a velocity-0 note-off, also running status.
    const auto m = parse({0x90, 60, 100, 64, 90, 67, 80, 60, 0});
    REQUIRE(m.size() == 4);
    CHECK((m[0].status == 0x90 && m[0].data1 == 60 && m[0].data2 == 100));
    CHECK((m[1].status == 0x90 && m[1].data1 == 64));
    CHECK((m[2].status == 0x90 && m[2].data1 == 67));
    CHECK(classify(m[3]).kind == MidiKind::NoteOff); // 0x90 with velocity 0
}

TEST_CASE("MidiStreamParser: real-time bytes don't break messages or running status", "[midi][stream]")
{
    // A MIDI clock (0xF8) arrives between the status and its data, and again between running-status notes.
    const auto m = parse({0x91, 0xF8, 62, 0xF8, 70, 0xFE, 65, 71});
    REQUIRE(m.size() == 2);
    CHECK((m[0].status == 0x91 && m[0].data1 == 62 && m[0].data2 == 70));
    CHECK((m[1].status == 0x91 && m[1].data1 == 65 && m[1].data2 == 71));
}

TEST_CASE("MidiStreamParser: one-data-byte messages, sysex, and orphan data", "[midi][stream]")
{
    // Orphan data at stream start is dropped; program change has 1 data byte (and runs);
    // sysex is skipped and cancels running status, so the data after it is orphaned too.
    const auto m = parse({60, 100, 0xC0, 5, 6, 0xF0, 0x43, 0x10, 0xF7, 60, 100, 0xE0, 0x00, 0x40});
    REQUIRE(m.size() == 3);
    CHECK((m[0].status == 0xC0 && m[0].data1 == 5));
    CHECK((m[1].status == 0xC0 && m[1].data1 == 6));
    const auto bend = classify(m[2]);
    CHECK(bend.kind == MidiKind::PitchBend);
    CHECK(bend.pitchBend == 0.0f); // 0x40 << 7 = 8192 = centre
}

TEST_CASE("toEvent: notes, velocity-0, bend and panic map to Engine events", "[midi][event]")
{
    Event e;
    REQUIRE(toEvent(classify({0x92, 61, 127, 0}), 17, e));
    CHECK(e.type == Event::Type::NoteOn);
    CHECK(e.key == 61);
    CHECK(e.value == 1.0f);
    CHECK(e.frame == 17u);
    CHECK(e.channel == 2);

    REQUIRE(toEvent(classify({0x90, 61, 0, 0}), 0, e));
    CHECK(e.type == Event::Type::NoteOff);

    REQUIRE(toEvent(classify({0xE0, 0x7F, 0x7F, 0}), 0, e)); // full up
    CHECK(e.type == Event::Type::PitchBend);
    CHECK(e.value == 1.0f);

    REQUIRE(toEvent(classify({0xB0, 123, 0, 0}), 0, e));
    CHECK(e.type == Event::Type::AllNotesOff);

    // Since L1.11 every other CC becomes a Controller event (the Engine uses CC 1, and CC 64 from L2.5).
    REQUIRE(toEvent(classify({0xB1, 1, 127, 0}), 5, e));
    CHECK(e.type == fss::Event::Type::Controller);
    CHECK(e.key == 1);
    CHECK(e.value == 1.0f);
    CHECK(e.frame == 5);
    CHECK_FALSE(toEvent(classify({0xF8, 0, 0, 0}), 0, e));   // system
}
