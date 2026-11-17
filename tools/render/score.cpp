#include "score.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace fss::render {

std::vector<ScoreNote> parseScore(std::string_view text)
{
    std::vector<ScoreNote> notes;
    std::istringstream in{std::string(text)};
    std::string line;
    for (int lineNo = 1; std::getline(in, line); ++lineNo) {
        if (const auto hash = line.find('#'); hash != std::string::npos)
            line.erase(hash);
        std::istringstream fields(line);
        double t = 0.0, dur = 0.0;
        int key = 0, vel = 0;
        if (!(fields >> t)) // blank (or comment-only) line
            continue;
        if (!(fields >> key >> vel >> dur))
            throw std::runtime_error("score line " + std::to_string(lineNo) + ": expected 't_ms key vel dur_ms'");
        if (key < 0 || key > 127 || vel < 1 || vel > 127 || t < 0.0 || dur <= 0.0)
            throw std::runtime_error("score line " + std::to_string(lineNo) +
                                     ": key 0..127, vel 1..127, t >= 0, dur > 0");
        notes.push_back({t, static_cast<std::uint8_t>(key), static_cast<std::uint8_t>(vel), dur});
    }
    // Sorted by start time, so later stages (event lists in L1.7) can rely on the order.
    std::stable_sort(notes.begin(), notes.end(), [](const ScoreNote& a, const ScoreNote& b) { return a.startMs < b.startMs; });
    return notes;
}

std::vector<ScoreNote> loadScore(const std::filesystem::path& path)
{
    std::ifstream in(path);
    if (!in)
        throw std::runtime_error("cannot open score " + path.string());
    std::stringstream buf;
    buf << in.rdbuf();
    return parseScore(buf.str());
}

double scoreEndMs(const std::vector<ScoreNote>& notes)
{
    double end = 0.0;
    for (const ScoreNote& n : notes)
        end = std::max(end, n.startMs + n.durationMs);
    return end;
}

} // namespace fss::render
