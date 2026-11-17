# Dev control surface (L2.4, D12 = Dear ImGui). Windows only: Win32 window + Direct3D 11 renderer.
#
# Three targets:
#   fss_imgui      third-party Dear ImGui (core + Win32/DX11 backends), built WITHOUT our warnings
#   fss_devui      our thin shell: DevUi (window/device/frame loop) + NoteInput (on-screen/QWERTY keyboard)
#   fss_devui_demo a window that shows the widgets and logs note events (smoke test: --frames N)

# --- Dear ImGui -----------------------------------------------------------------------------------
# Same pinning pattern as Catch2 in tests/CMakeLists.txt: a tagged release tarball + its SHA-256.
# The hash is trust-on-first-use: it was computed from the first download (2026-11-17), so it proves
# "same bytes as the first time", not "the bytes the author published". Upgrading = new tag + new hash.
include(FetchContent)
FetchContent_Declare(imgui
    URL      https://github.com/ocornut/imgui/archive/refs/tags/v1.91.9b.tar.gz
    URL_HASH SHA256=8e1bbc76c71d74fef2fb85db7e7ca8eba13d6a86623c54992b60162db554ffdb
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
# ImGui ships no CMakeLists, so we only download it (MakeAvailable would try add_subdirectory) and
# describe the library ourselves.
FetchContent_GetProperties(imgui)
if(NOT imgui_POPULATED)
    if(POLICY CMP0169)
        cmake_policy(SET CMP0169 OLD) # allow the one-argument FetchContent_Populate on CMake >= 3.30
    endif()
    FetchContent_Populate(imgui)
endif()

add_library(fss_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp          # ImGui::ShowDemoWindow: the best ImGui reference there is
    ${imgui_SOURCE_DIR}/backends/imgui_impl_win32.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_dx11.cpp)
# SYSTEM: our targets include imgui.h, and SYSTEM makes MSVC treat it as an external header
# (/external:I + /external:W0), so our /W4 /WX never fires on ImGui's code.
target_include_directories(fss_imgui SYSTEM PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_compile_features(fss_imgui PUBLIC cxx_std_20)
# d3d11/dxgi: the renderer; d3dcompiler: the DX11 backend compiles its two tiny shaders at startup.
target_link_libraries(fss_imgui PUBLIC d3d11 dxgi d3dcompiler)

# --- Our shell ----------------------------------------------------------------------------------
add_library(fss_devui STATIC
    dev_ui.cpp
    keyboard.cpp)
add_library(fss::devui ALIAS fss_devui)
target_include_directories(fss_devui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# PUBLIC: anyone using DevUi draws their own ImGui panels between beginFrame() and endFrame().
target_link_libraries(fss_devui PUBLIC fss_imgui PRIVATE fss::warnings)
# Keep <windows.h> from defining min/max macros and pulling in rarely used headers. UNICODE makes
# macros like IDC_ARROW expand to the UTF-16 (W) variants we call explicitly.
target_compile_definitions(fss_devui PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)

add_executable(fss_devui_demo demo.cpp)
target_link_libraries(fss_devui_demo PRIVATE fss::devui fss::warnings)
