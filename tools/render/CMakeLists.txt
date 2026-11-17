# fss_render: the offline host. For now it proves the toolchain links a host against fss_core;
# from L1.1 on it writes WAV files into renders/.
add_executable(fss_render main.cpp)
target_link_libraries(fss_render PRIVATE fss::core fss::warnings)
