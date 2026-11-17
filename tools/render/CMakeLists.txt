# fss_wav: WAV file I/O for hosts and tests. A separate library (not part of fss_core) because core
# never does I/O, and separate from fss_render so the tests can link it without a second main().
add_library(fss_wav STATIC
    wav_writer.cpp
    wav_reader.cpp)
add_library(fss::wav ALIAS fss_wav)
target_include_directories(fss_wav PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(fss_wav PUBLIC cxx_std_20)
target_link_libraries(fss_wav PRIVATE fss::warnings)

# fss_score: the text score format (L1.6). Host-side (it parses files), tested in tests/test_score.cpp.
add_library(fss_score STATIC score.cpp)
add_library(fss::score ALIAS fss_score)
target_include_directories(fss_score PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(fss_score PUBLIC cxx_std_20)
target_link_libraries(fss_score PRIVATE fss::warnings)

# fss_render: the offline host. One source file per lesson's scenarios.
add_executable(fss_render
    main.cpp
    l1_1.cpp
    l1_2.cpp
    l1_3.cpp
    l1_4.cpp
    l1_5.cpp
    l1_6.cpp
    l1_7.cpp
    l2_4.cpp
    engine_render.cpp)
target_link_libraries(fss_render PRIVATE fss::core fss::wav fss::score fss::warnings)
# Score files live in the source tree; bake their absolute path in so fss_render works from any folder.
target_compile_definitions(fss_render PRIVATE FSS_SCORES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/scores")
