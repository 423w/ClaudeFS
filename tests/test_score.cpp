// Tests for tools/render/score.{h,cpp} (L1.6): the text score parser.
#include "score.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

using namespace fss::render;

TEST_CASE("parseScore: fields, comments, blank lines, and sorting by time", "[score]")
{
    const auto notes = parseScore("# header comment\n"
                                  "\n"
                                  "500 64 80 400   # trailing comment\n"
                                  "  0 60 100 250\n");
    REQUIRE(notes.size() == 2);
    CHECK(notes[0].startMs == 0.0); // sorted
    CHECK(notes[0].key == 60);
    CHECK(notes[0].velocity == 100);
    CHECK(notes[0].durationMs == 250.0);
    CHECK(notes[1].key == 64);
    CHECK(scoreEndMs(notes) == 900.0);
}

TEST_CASE("parseScore: malformed lines are errors, not silent garbage", "[score]")
{
    CHECK_THROWS_AS(parseScore("0 60 100\n"), std::runtime_error);       // missing duration
    CHECK_THROWS_AS(parseScore("0 128 100 10\n"), std::runtime_error);   // key out of range
    CHECK_THROWS_AS(parseScore("0 60 0 10\n"), std::runtime_error);      // velocity 0 = note-off in MIDI
    CHECK_THROWS_AS(parseScore("0 60 100 0\n"), std::runtime_error);     // zero length
    CHECK(parseScore("").empty());
}
