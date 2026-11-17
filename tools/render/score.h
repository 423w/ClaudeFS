// score.h: a tiny text score format for offline renders (L1.6): a mini sequencer.
//
//   # comment lines and blank lines are ignored
//   # t_ms   key   vel   dur_ms
//     0      60    100   400        middle C at velocity 100, starting at 0 ms, held 400 ms
//     500    64    80    400
//
// key and vel are MIDI numbers (0..127; vel 1..127, since in MIDI a note-on with velocity 0 means note-off).
// Host-side only: parsing a file is I/O, so it lives in tools/render, not core.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

namespace fss::render {

struct ScoreNote {
    double startMs;
    std::uint8_t key;
    std::uint8_t velocity; // 1..127
    double durationMs;
};

/// Parse score text. Throws std::runtime_error naming the line on malformed input.
[[nodiscard]] std::vector<ScoreNote> parseScore(std::string_view text);
[[nodiscard]] std::vector<ScoreNote> loadScore(const std::filesystem::path& path);

/// End time of the last note, in ms (0 for an empty score).
[[nodiscard]] double scoreEndMs(const std::vector<ScoreNote>& notes);

} // namespace fss::render
