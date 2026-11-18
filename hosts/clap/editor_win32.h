// editor_win32.h: the editor's window (L3.5): a Win32 child window inside the host's plugin window, a D3D11
// device to draw it, a Dear ImGui context of its own, and a ~60 Hz timer that redraws it.
//
// Everything here runs on the host's MAIN thread (every clap.gui call is [main-thread], and Windows delivers
// the timer and mouse messages to the thread that created the window). The audio thread never touches it:
// the editor reads the plugin's main-thread mirror and sends gestures through the plugin's queue.
#pragma once

#include "editor_ui.h"

#include <clap/id.h>

#include <cstdint>
#include <memory>

namespace fss::plugin {

class EditorWindow {
public:
    explicit EditorWindow(EditorModel& model);
    ~EditorWindow();
    EditorWindow(const EditorWindow&) = delete;
    EditorWindow& operator=(const EditorWindow&) = delete;

    /// Create the child window inside `parentHwnd` (a HWND) on the first call; re-parent on later ones.
    bool setParent(void* parentHwnd);
    /// CLAP's scale (1.0 = 96 DPI). Sizes below are physical pixels = logical × scale.
    void setScale(double scale);
    void size(std::uint32_t& width, std::uint32_t& height) const;
    void adjustSize(std::uint32_t& width, std::uint32_t& height) const; // clamp to the minimum size
    bool setSize(std::uint32_t width, std::uint32_t height);
    bool show();
    bool hide();

    // ---- for the test host (exported from the DLL as fss_debug_*; not part of CLAP) ----
    /// Draw one frame and write it to a 32-bit BMP file.
    bool snapshot(const char* bmpPath);
    /// The client-pixel rectangle of a parameter's control in the last frame (x0, y0, x1, y1).
    bool controlRect(clap_id paramId, int rect[4]) const;
    /// Draw one frame now (the test host has no message loop running the timer).
    void renderNow();
    /// Show editor page `page` (L5.1; kPageNames). Takes effect on the next frame.
    void setPage(int page);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fss::plugin
