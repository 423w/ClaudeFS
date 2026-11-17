# CFS (Claude's build of FirstStreetSynthesis): top-level build.
#
# Layout (ARCHITECTURE.md §2):
#   core/          fss_core: the DSP + synth library. Standard library only.
#   tools/render/  offline CLI host (prints "hello" for now; renders WAVs from L1.1)
#   tests/         Catch2 unit tests (D4)
#
# Configure/build/test with the presets (run tools/devshell.ps1 first so cl, cmake and ninja are on PATH):
#   cmake --preset release && cmake --build --preset release && ctest --preset release

# 3.25 is the first CMake that understands CMakePresets.json v6 (which adds workflow presets).
# VS 2022 17.14 ships 3.31, so this costs nothing.
cmake_minimum_required(VERSION 3.25)

project(CFS
    VERSION 0.0.1
    DESCRIPTION "Claude's build of FirstStreetSynthesis, an FM-core hybrid synth"
    LANGUAGES CXX)

option(FSS_BUILD_TESTS "Build the unit tests (fetches Catch2)" ON)
option(FSS_WERROR      "Treat compiler warnings as errors in CFS's own targets" ON)

# --- Warnings -----------------------------------------------------------------------------------
# Warnings are teachers (L0.3). They go on an INTERFACE target that *our* targets link to, instead of
# into CMAKE_CXX_FLAGS, for two reasons:
#   1. Modern CMake: flags belong to targets, never to the whole directory tree.
#   2. Third-party code (Catch2) must not be built with our /WX, or a warning in their code breaks our build.
add_library(fss_warnings INTERFACE)
add_library(fss::warnings ALIAS fss_warnings)
if(MSVC)
    target_compile_options(fss_warnings INTERFACE
        /W4              # high warning level: catches implicit float<->double<->int narrowing (C4244), etc.
        /permissive-     # standard-conforming mode: reject MSVC-only extensions
        /Zc:__cplusplus  # make __cplusplus report the real standard (MSVC reports 199711L otherwise)
        /utf-8           # source and execution charset are UTF-8 (our comments use ≈, →, etc.)
        $<$<BOOL:${FSS_WERROR}>:/WX>)
else()
    target_compile_options(fss_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wconversion -Wshadow
        $<$<BOOL:${FSS_WERROR}>:-Werror>)
endif()

add_subdirectory(core)
add_subdirectory(tools/render)

if(FSS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
