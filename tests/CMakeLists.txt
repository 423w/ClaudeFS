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
    test_engine.cpp
    test_realtime.cpp
    test_spsc_queue.cpp
    test_midi_parse.cpp
    test_midi_stream.cpp
    test_params.cpp
    test_waveforms.cpp
    test_filters.cpp
    test_svf.cpp
    test_lfo.cpp
    test_play.cpp
    test_denormal.cpp)
# Catch2WithMain supplies main(), so test files contain only TEST_CASEs.
# fss::warnings is linked here, but not to Catch2 itself (see the top-level CMakeLists.txt).
target_link_libraries(fss_tests PRIVATE fss::core fss::wav fss::score fss::midi_message fss_rt_trap fss::warnings
    Catch2::Catch2WithMain)

# catch_discover_tests registers every TEST_CASE as its own CTest test (it runs `fss_tests --list-tests`
# after each build). So `ctest` reports and re-runs tests individually instead of one big pass/fail.
list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)
catch_discover_tests(fss_tests)

# ---- the CLAP plugin (L3.2) --------------------------------------------------------------------------
if(TARGET fss_clap)
    # In-process tests: the plugin's C function tables called directly (no DLL), with a fake host.
    add_executable(fss_clap_tests test_clap.cpp)
    target_link_libraries(fss_clap_tests PRIVATE fss_clap_impl fss::midi_message fss_rt_trap fss::warnings
        Catch2::Catch2WithMain)
    catch_discover_tests(fss_clap_tests)

    # The real DLL, loaded by our headless host exactly like a DAW would.
    add_test(NAME clap_test_host COMMAND fss_clap_host $<TARGET_FILE:fss_clap>)

    # clap-validator (free-audio): the community's conformance suite. --only-failed keeps the log short.
    if(FSS_CLAP_VALIDATOR_EXE)
        add_test(NAME clap_validator COMMAND ${FSS_CLAP_VALIDATOR_EXE} validate --only-failed $<TARGET_FILE:fss_clap>)
    endif()
endif()
