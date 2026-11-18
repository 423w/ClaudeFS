# fss_play (L2.4, milestone M3): the playable synth. Audio device + all MIDI inputs + Dear ImGui dev control
# surface + fss::Engine. Windows only (the GUI is Win32 + D3D11).
add_executable(fss_play main.cpp) # a console app on purpose: device info and errors print there
target_link_libraries(fss_play PRIVATE fss::core fss::audio fss::midi fss::devui fss::preset_library fss::warnings)
# Debug builds trap audio-thread allocations (L2.2), exactly like fss_standalone.
target_link_libraries(fss_play PRIVATE $<$<CONFIG:Debug>:fss_rt_trap>)
