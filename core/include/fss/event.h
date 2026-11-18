// fss/event.h: what a host tells the Engine, time-stamped to the sample (L1.7).
//
// Every host (offline renderer, standalone app, CLAP plugin) translates its own world (score files,
// MIDI bytes, CLAP events) into a list of these, sorted by `frame`, and hands them to Engine::process
// with the block they belong to. `frame` is the sample offset inside that block, so a note can start
// on any sample, not just at block boundaries (ARCHITECTURE.md principle 6).
#pragma once

#include <cstdint>

namespace fss {

struct Event {
    enum class Type : std::uint8_t {
        NoteOn,      // key, value = velocity 0..1, noteId
        NoteOff,     // key (and noteId if ≥ 0)
        PitchBend,   // value = −1..1 (× the patch's bend range)
        AllNotesOff, // release everything (panic / transport stop)
        ParamChange, // paramId, value = new PLAIN value (units of the param table, L2.4)
        Controller,  // key = MIDI CC number, value = 0..1 (L1.11): performance controllers such as the mod
                     // wheel (CC 1). CCs mapped to *parameters* are translated by the host (param table).
        Tempo,       // value = the host's tempo in BPM (L5.3): tempo-synced effects follow it. Not part of the
                     // patch (the song owns the tempo); hosts without a transport never send it (120 BPM).
    };

    Type type = Type::NoteOn;
    std::uint32_t frame = 0;   // offset within the current block; events must be sorted by frame
    std::int32_t noteId = -1;  // host note id (CLAP has them), −1 = match by key only
    std::uint8_t key = 60;     // MIDI key 0..127
    std::uint8_t channel = 0;
    float value = 0.0f;        // meaning depends on type (see above)
    std::uint32_t paramId = 0; // ParamChange: a stable id from fss/synth/params.h

    // Small named constructors, so hosts read clearly: Event::noteOn(frame, key, velocity).
    static constexpr Event noteOn(std::uint32_t frame, std::uint8_t key, float velocity, std::int32_t id = -1)
    {
        return {Type::NoteOn, frame, id, key, 0, velocity};
    }
    static constexpr Event noteOff(std::uint32_t frame, std::uint8_t key, std::int32_t id = -1)
    {
        return {Type::NoteOff, frame, id, key, 0, 0.0f};
    }
    static constexpr Event pitchBend(std::uint32_t frame, float bend) { return {Type::PitchBend, frame, -1, 0, 0, bend}; }
    static constexpr Event controller(std::uint32_t frame, std::uint8_t cc, float value01)
    {
        return {Type::Controller, frame, -1, cc, 0, value01};
    }
    static constexpr Event paramChange(std::uint32_t frame, std::uint32_t id, float plainValue)
    {
        return {Type::ParamChange, frame, -1, 0, 0, plainValue, id};
    }
    static constexpr Event tempo(std::uint32_t frame, float bpm) { return {Type::Tempo, frame, -1, 0, 0, bpm}; }
};

} // namespace fss
