# Unit tests (D4: Catch2 v3, fetched at configure time).
#
# FetchContent downloads Catch2's source into build/<preset>/_deps (git-ignored) the first time you
# configure, then builds it like any other target. Pinning:
#   - URL to a release tarball (not a moving branch), so every clone gets the same code;
#   - URL_HASH, so a changed or tampered download fails the configure instead of being used.
# To upgrade: change the version, set the hash to the new tarball's SHA-256, and re-run the tests.
include(FetchContent)
FetchContent_Declare(Catch2
    URL      https://github.com/catchorg/Catch2/archive/refs/tags/v3.8.1.tar.gz
    URL_HASH SHA256=18b3f70ac80fccc340d8c6ff0f339b2ae64944782f8d2fca2bd705cf47cadb79
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(Catch2)

# One test executable for the whole core; test files are added here as lessons add primitives.
add_executable(fss_tests
    test_math.cpp
    test_wav.cpp
    test_phasor.cpp
    test_sine.cpp
    test_adsr.cpp
    test_operator.cpp
    test_fm_voice.cpp
    test_note.cpp
    test_score.cpp
    test_engine.cpp)
# Catch2WithMain supplies main(), so test files contain only TEST_CASEs.
# fss::warnings is linked here, but not to Catch2 itself (see the top-level CMakeLists.txt).
target_link_libraries(fss_tests PRIVATE fss::core fss::wav fss::score fss::warnings Catch2::Catch2WithMain)

# catch_discover_tests registers every TEST_CASE as its own CTest test (it runs `fss_tests --list-tests`
# after each build). So `ctest` reports and re-runs tests individually instead of one big pass/fail.
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(fss_tests)
