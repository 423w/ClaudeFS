# MIDI input (L2.3, D10: WinMM by hand).
#
# fss_midi_message: the portable, pure part (MidiMessage + classify()). Header-only, any OS, so the
# unit tests build everywhere and the CLAP host can reuse it later.
add_library(fss_midi_message INTERFACE)
add_library(fss::midi_message ALIAS fss_midi_message)
target_include_directories(fss_midi_message INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fss_midi_message INTERFACE fss::core) # for fss/util/spsc_queue.h

# fss_midi: the WinMM device input, and the console monitor. Windows only.
if(WIN32)
    add_library(fss_midi STATIC midi_input_winmm.cpp)
    add_library(fss::midi ALIAS fss_midi)
    target_link_libraries(fss_midi
        PUBLIC  fss::midi_message
        PRIVATE fss::warnings winmm) # winmm.lib: midiIn* live there

    add_executable(fss_midi_monitor midi_monitor.cpp)
    target_link_libraries(fss_midi_monitor PRIVATE fss::midi fss::warnings)
endif()
