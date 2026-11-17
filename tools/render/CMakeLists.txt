# fss_wav: WAV file I/O for hosts and tests. A separate library (not part of fss_core) because core
# never does I/O, and separate from fss_render so the tests can link it without a second main().
add_library(fss_wav STATIC
    wav_writer.cpp
    wav_reader.cpp)
add_library(fss::wav ALIAS fss_wav)
target_include_directories(fss_wav PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(fss_wav PUBLIC cxx_std_20)
target_link_libraries(fss_wav PRIVATE fss::warnings)

# fss_render: the offline host. Renders the current lesson's scenarios into renders/.
add_executable(fss_render main.cpp)
target_link_libraries(fss_render PRIVATE fss::core fss::wav fss::warnings)
