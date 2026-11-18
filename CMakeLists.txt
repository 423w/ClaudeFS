# CFS (Claude's build of FirstStreetSynthesis): top-level build.
#
# Layout (ARCHITECTURE.md §2):
#   core/          fss_core: the DSP + synth library. Standard library only.
#   tools/render/  offline CLI host (prints "hello" for now; renders WAVs from L1.1)
#   hosts/standalone/ real-time host: plays through the audio device (L2.1, miniaudio)
#   hosts/clap/    CFS.clap, the plugin (L3.2, hand-written CLAP), and a headless test host
#   tests/         Catch2 unit tests (D4)
#
# Configure/build/test with the presets (run tools/devshell.ps1 first so cl, cmake and ninja are on PATH):
#   cmake --preset release && cmake --build --preset release && ctest --preset release

# 3.25 is the first CMake that understands CMakePresets.json v6 (which adds workflow presets).
# VS 2022 17.14 ships 3.31, so this costs nothing.
cmake_minimum_required(VERSION 3.25)

project(CFS
    VERSION 0.5.1 # branch.lesson of the last finished lesson; the plugin reports it to the host
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
add_subdirectory(hosts/midi) # L2.3: MidiMessage everywhere; WinMM input + monitor on Windows only
add_subdirectory(hosts/presets) # L3.4: factory bank + the user's preset folder, for GUI hosts

# The real-time host (L2.1). Default ON only on Windows, the platform it's developed and tested on;
# miniaudio itself is cross-platform, so turning it on elsewhere should work but is untested.
if(WIN32)
    set(_fss_standalone_default ON)
else()
    set(_fss_standalone_default OFF)
endif()
option(FSS_BUILD_STANDALONE "Build hosts/standalone (fetches miniaudio, D9)" ${_fss_standalone_default})
# The CLAP plugin (L3.2, D13). Plain C ABI + the CLAP headers: no platform restriction in principle, but
# only built and tested on Windows so far. (Its editor, L3.5, is Win32 + D3D11.)
option(FSS_BUILD_CLAP "Build hosts/clap: CFS.clap and its test host (fetches the CLAP SDK)" ${WIN32})

# Dear ImGui (D12, and D14 since L3.5) on Win32 + D3D11: the dev surface in fss_play AND the plugin's editor.
if(WIN32 AND (FSS_BUILD_STANDALONE OR FSS_BUILD_CLAP))
    add_subdirectory(hosts/devui) # fss_imgui + fss_devui: window, piano keyboard widget (fetches Dear ImGui)
endif()
if(FSS_BUILD_STANDALONE)
    add_subdirectory(hosts/standalone)
    if(WIN32)
        add_subdirectory(hosts/play)  # fss_play: the playable synth = audio + MIDI + GUI + Engine
    endif()
endif()

if(FSS_BUILD_CLAP)
    add_subdirectory(hosts/clap)
endif()

if(FSS_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
