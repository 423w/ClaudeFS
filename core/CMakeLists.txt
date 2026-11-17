# fss_core: the whole synth, as a library every host links to.
#
# Why its own target (the L0.3 [THINK]): the same DSP must run in the offline renderer, the tests, the
# standalone app and the CLAP plugin. As one library it compiles once, carries its include path and C++20
# requirement with it (`target_*` + PUBLIC/INTERFACE), and *cannot* accidentally depend on a host: core
# has no way to see tools/ or hosts/ includes. The pure-core rule (ARCHITECTURE.md §3.1) is enforced by
# the build graph, not just by good intentions.
#
# INTERFACE for now: core is header-only until the first .cpp shows up. A STATIC library with no sources is
# an error in CMake, and an INTERFACE target is how CMake spells "header-only library". When the first
# .cpp arrives, this becomes add_library(fss_core STATIC ...) and the INTERFACE keywords become PUBLIC.
add_library(fss_core INTERFACE)
add_library(fss::core ALIAS fss_core)

target_include_directories(fss_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include)

# A requirement, not a flag: anything linking fss_core is compiled as C++20 or newer.
target_compile_features(fss_core INTERFACE cxx_std_20)
