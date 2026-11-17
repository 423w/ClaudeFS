# fss_standalone: the real-time host (L2.1). Opens the default audio device and plays the synth live.
#
# D9: the audio backend is miniaudio [MINIAUDIO], a single-header C library (public domain / MIT-0) that
# wraps WASAPI, DirectSound, WinMM (and CoreAudio, ALSA, PulseAudio... on other OSes). One header, no
# DLLs, no SDK install. It is the one place CFS talks to the sound card; fss_core still knows nothing
# about devices (ARCHITECTURE.md §3.1).
#
# Pinning, same pattern as Catch2 (tests/CMakeLists.txt): a tagged release tarball + its SHA-256.
# The hash below was computed from the first download (2026-11-17), i.e. "trust on first use": it can't
# prove that first download was genuine, only that every later one is byte-identical to it.
# To upgrade: change the tag, download the new tarball, put its SHA-256 here, rebuild, re-listen.
include(FetchContent)
FetchContent_Declare(miniaudio
    URL      https://github.com/mackron/miniaudio/archive/refs/tags/0.11.23.tar.gz
    URL_HASH SHA256=1062155cc5882b55c48cb37f57a4dc783669e83ae0838535c62b206eeb1587a6
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    # The tarball ships its own CMakeLists.txt (examples, tests, extra libraries). We only want
    # miniaudio.h, so point SOURCE_SUBDIR at a folder with no CMakeLists.txt: FetchContent_MakeAvailable
    # then downloads and extracts but does NOT add_subdirectory() their build.
    SOURCE_SUBDIR do-not-build)
FetchContent_MakeAvailable(miniaudio)

# fss_miniaudio: miniaudio's implementation, compiled exactly once (MINIAUDIO_IMPLEMENTATION in
# miniaudio_impl.cpp). Deliberately NOT linked to fss::warnings: it's third-party code, and /W4 /WX on
# 90k lines we don't own would break our build on their warnings. /w silences it entirely.
add_library(fss_miniaudio STATIC miniaudio_impl.cpp)
# SYSTEM: our own files that #include <miniaudio.h> see it as an external header, so MSVC applies
# /external:W0 to it and our /W4 /WX only judges our code.
target_include_directories(fss_miniaudio SYSTEM PUBLIC ${miniaudio_SOURCE_DIR})
# Compile out the parts we don't use (decoders, encoders, the high-level engine/node graph). PUBLIC so every
# file that includes miniaudio.h sees the same configuration as the implementation (an ODR requirement).
target_compile_definitions(fss_miniaudio PUBLIC
    MA_NO_DECODING MA_NO_ENCODING MA_NO_GENERATION MA_NO_RESOURCE_MANAGER MA_NO_NODE_GRAPH MA_NO_ENGINE)
if(MSVC)
    target_compile_options(fss_miniaudio PRIVATE /w)
endif()

# fss_audio: the RAII device wrapper, shared by every real-time host (fss_standalone, fss_play).
add_library(fss_audio STATIC audio_device.cpp)
add_library(fss::audio ALIAS fss_audio)
target_include_directories(fss_audio PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(fss_audio PUBLIC cxx_std_20)
target_link_libraries(fss_audio PRIVATE fss_miniaudio fss::warnings)

add_executable(fss_standalone main.cpp)
target_link_libraries(fss_standalone PRIVATE fss::core fss::midi fss::audio fss::warnings)
# L2.2: Debug builds link the allocation trap, so any allocation on the audio thread aborts on the spot.
# (Linking an OBJECT library passes its .obj straight to the linker, which is what makes the operator new
# replacement take effect.) Release builds keep the C runtime's allocator untouched.
target_link_libraries(fss_standalone PRIVATE $<$<CONFIG:Debug>:fss_rt_trap>)
