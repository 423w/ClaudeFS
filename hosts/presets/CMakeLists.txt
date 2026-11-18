# fss_preset_library (L3.4): the factory bank plus the user's preset folder, for hosts with a GUI
# (fss_play now, the plugin editor in L3.5). Host-side because it touches the file system; core only parses.
add_library(fss_preset_library STATIC preset_library.cpp)
add_library(fss::preset_library ALIAS fss_preset_library)
target_include_directories(fss_preset_library PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fss_preset_library PUBLIC fss::core PRIVATE fss::warnings)
target_compile_definitions(fss_preset_library PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
if(WIN32)
    target_link_libraries(fss_preset_library PRIVATE shell32 ole32) # SHGetKnownFolderPath, CoTaskMemFree
endif()
