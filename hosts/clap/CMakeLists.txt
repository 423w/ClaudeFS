# hosts/clap: CFS as a CLAP plugin (L3.2, D13 = hand-written against the bare CLAP C headers).
#
# Targets:
#   fss_clap_sdk     the CLAP headers (third party, header-only, MIT), as an INTERFACE target
#   fss_clap_impl    the plugin itself, as a static library: FssClap + params/state/event translation.
#                    A library so the unit tests can create a plugin in-process and call its C function
#                    tables directly, with no DLL in between.
#   fss_clap         CFS.clap: the DLL a host loads. Only adds the exported entry point (entry.cpp).
#   fss_clap_host    a small headless CLAP host (test_host.cpp) that loads CFS.clap like a DAW would and
#                    checks it: lifecycle, params, state, bit-exactness against fss::Engine, timing.
#
# The plugin is a HOST ADAPTER over fss::Engine (ARCHITECTURE.md §2): no DSP in this folder, ever.

include(FetchContent)

# --- the CLAP SDK -----------------------------------------------------------------------------------
# Pinned like Catch2/miniaudio/ImGui: a release tag's tarball + its SHA-256 (trust-on-first-use, taken from
# the first download on 2026-11-18, approved by the owner). We only need include/, so we download and use
# the headers without running the SDK's own CMakeLists (which builds its tests when top-level).
FetchContent_Declare(clap
    URL      https://github.com/free-audio/clap/archive/refs/tags/1.2.10.tar.gz
    URL_HASH SHA256=58fdb977c6678859c15d57942378b651399178f49a3a8ecad1813b9f231dc096
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_GetProperties(clap)
if(NOT clap_POPULATED)
    if(POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD) # one-argument FetchContent_Populate, as in hosts/devui
    endif()
    FetchContent_Populate(clap)
endif()
add_library(fss_clap_sdk INTERFACE)
# SYSTEM: CLAP's headers are third-party code; our /W4 /WX must not fire on them.
target_include_directories(fss_clap_sdk SYSTEM INTERFACE ${clap_SOURCE_DIR}/include)

# --- the plugin -------------------------------------------------------------------------------------
add_library(fss_clap_impl STATIC
    plugin.cpp
    state_v0.cpp
    trace.cpp)
target_include_directories(fss_clap_impl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fss_clap_impl
    PUBLIC  fss::core fss_clap_sdk
    PRIVATE fss::midi_message fss::warnings)
target_compile_definitions(fss_clap_impl
    PUBLIC  FSS_VERSION_STRING="${PROJECT_VERSION}"
    PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)

# MODULE = a library that is only ever loaded at run time (LoadLibrary), never linked against. CLAP's
# convention is a normal DLL with the extension .clap and no "lib" prefix.
add_library(fss_clap MODULE entry.cpp)
set_target_properties(fss_clap PROPERTIES
    OUTPUT_NAME "CFS"
    PREFIX ""
    SUFFIX ".clap")
target_link_libraries(fss_clap PRIVATE fss_clap_impl fss::warnings)
# Debug builds trap audio-thread allocations (L2.2) inside the plugin too. The trap replaces operator new
# for this DLL only (each Windows module has its own), so it can't affect the host. It COUNTS (never aborts:
# crashing a DAW is worse than any bug it could report); the test host reads the count through an extra
# export that only Debug builds have (see entry.cpp).
target_link_libraries(fss_clap PRIVATE $<$<CONFIG:Debug>:fss_rt_trap>)
target_compile_definitions(fss_clap PRIVATE $<$<CONFIG:Debug>:FSS_RT_TRAP_LINKED=1>)

# --- the headless test host -------------------------------------------------------------------------
add_executable(fss_clap_host test_host.cpp)
# fss_clap_impl for the shared param mapping (clap_params.h) and fss::core for the direct-Engine reference
# render. The plugin itself is loaded at run time from CFS.clap, like a DAW does.
target_link_libraries(fss_clap_host PRIVATE fss_clap_impl fss::midi_message fss::warnings)
target_compile_definitions(fss_clap_host PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
add_dependencies(fss_clap_host fss_clap)

# --- clap-validator (free-audio, MIT): the CLAP conformance suite --------------------------------------
# A prebuilt Windows binary from the 0.4.1 release (Rust isn't installed here, so building it from source
# isn't an option). Downloaded with the owner's approval on 2026-11-18 and pinned by SHA-256 like the
# sources above. It only ever runs against our own CFS.clap.
option(FSS_CLAP_VALIDATOR "Fetch clap-validator and register it as a CTest test" ${WIN32})
if(FSS_CLAP_VALIDATOR AND FSS_BUILD_TESTS)
    FetchContent_Declare(clap_validator
        URL      https://github.com/free-audio/clap-validator/releases/download/0.4.1/clap-validator-0.4.1-127-g152b982-windows.zip
        URL_HASH SHA256=d935c3af0a45c3911ea2e900f4aa5d6709dac82bb485f0c4ce28648ab2cd0c10
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_GetProperties(clap_validator)
    if(NOT clap_validator_POPULATED)
        FetchContent_Populate(clap_validator)
    endif()
    set(FSS_CLAP_VALIDATOR_EXE ${clap_validator_SOURCE_DIR}/clap-validator.exe CACHE INTERNAL "")
endif()
# The CTest registrations (test host, validator) live in tests/CMakeLists.txt, after enable_testing().
