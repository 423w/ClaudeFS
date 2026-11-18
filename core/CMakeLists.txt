# fss_core: the whole synth, as a library every host links to.
#
# Why its own target (the L0.3 [THINK]): the same DSP must run in the offline renderer, the tests, the
# standalone app and the CLAP plugin. As one library it compiles once, carries its include path and C++20
# requirement with it (`target_*` + PUBLIC), and *cannot* accidentally depend on a host: core has no way
# to see tools/ or hosts/ includes. The pure-core rule (ARCHITECTURE.md §3.1) is enforced by the build
# graph, not just by good intentions.
#
# STATIC since L1.7, when the Engine brought the first .cpp. (Until then it was INTERFACE: CMake's word
# for header-only.) Most primitives stay header-only so the compiler can inline them into voice loops.
add_library(fss_core STATIC
    src/engine.cpp
    src/params.cpp
    src/patch_io.cpp
    src/realtime_guard_stubs.cpp)
add_library(fss::core ALIAS fss_core)

# The factory preset bank (L3.4): presets/factory/*.cfspatch embedded as string literals. Generated at
# configure time; CMAKE_CONFIGURE_DEPENDS makes CMake re-run (and regenerate) whenever a preset file changes.
# A raw string literal R"cfspatch(...)cfspatch" holds each file verbatim. (MSVC caps one literal at ~16 KB;
# a patch file is ~3 KB.)
file(GLOB _fss_presets CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/presets/factory/*.cfspatch)
list(SORT _fss_presets)
set(FSS_FACTORY_ENTRIES "")
foreach(_f IN LISTS _fss_presets)
    file(READ ${_f} _text)
    get_filename_component(_name ${_f} NAME_WE)
    string(REGEX REPLACE "^[0-9]+ " "" _name "${_name}")
    # (In a CMake quoted argument \" is a quote and \n a newline; \\n would reach C++ as the escape \n.)
    string(APPEND FSS_FACTORY_ENTRIES "    {\"${_name}\", R\"cfspatch(${_text})cfspatch\"},\n")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${_f})
endforeach()
if(NOT _fss_presets) # first configure, before `fss_render l3.4` has written the files: an empty C array won't compile
    set(FSS_FACTORY_ENTRIES "    {\"Init\", \"CFS patch 1\\n\"},\n")
endif()
configure_file(src/factory_bank.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/factory_bank.cpp @ONLY)
target_sources(fss_core PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/factory_bank.cpp)

target_include_directories(fss_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(fss_core PUBLIC cxx_std_20)
# Our warnings apply to core's own .cpp files, not to whoever links core.
target_link_libraries(fss_core PRIVATE fss::warnings)

# fss_rt_trap (L2.2): the global operator new/delete replacement that catches allocations inside a
# RealtimeGuard. An OBJECT library, so its .obj goes straight to the linker of whatever links it (tests,
# Debug hosts) and really replaces the C runtime's operator new. See src/rt_alloc_trap.cpp.
add_library(fss_rt_trap OBJECT src/rt_alloc_trap.cpp)
add_library(fss::rt_trap ALIAS fss_rt_trap)
target_link_libraries(fss_rt_trap PUBLIC fss_core PRIVATE fss::warnings)
