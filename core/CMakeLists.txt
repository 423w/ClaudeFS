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
    src/realtime_guard_stubs.cpp)
add_library(fss::core ALIAS fss_core)

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
