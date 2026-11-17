// dev_ui.h: a window to draw Dear ImGui panels in (L2.4, D12).
//
// DevUi owns the boring platform plumbing: a Win32 window, a Direct3D 11 device + swap chain, and the
// ImGui context with its Win32/DX11 backends. A host's GUI loop then looks like:
//
//     fss::devui::DevUi ui;
//     if (!ui.open("CFS", 1280, 800)) return 1;
//     while (ui.beginFrame()) {     // false once the user closes the window
//         ImGui::Begin("Patch"); ... ImGui::End();
//         ui.endFrame();            // renders and waits for vsync
//     }
//
// This is the GUI thread. It must never be the audio thread: endFrame() blocks on vsync (~16 ms).
//
// The header is deliberately free of <windows.h> and <d3d11.h> (pimpl): those headers are huge, define
// macros like `near`/`far`, and would leak into every file that includes this one.
#pragma once

#include <memory>

namespace fss::devui {

class DevUi {
public:
    DevUi();
    ~DevUi(); // closes the window and releases D3D/ImGui (RAII), if open() succeeded
    DevUi(const DevUi&) = delete;
    DevUi& operator=(const DevUi&) = delete;

    // Creates and shows the window (client area width x height, in pixels). `title` is UTF-8.
    // Returns false (and prints the reason to stderr) if the window or D3D11 device can't be created.
    bool open(const char* title, int width, int height);

    // Pumps pending window messages and starts an ImGui frame. Returns false when the window has been
    // closed; then don't call endFrame(), just leave the loop.
    bool beginFrame();

    // Renders the frame's ImGui draw data and presents it, synchronized to the monitor refresh (vsync).
    void endFrame();

    // Asks the window to close; the next beginFrame() returns false. (Used by the demo's --frames N.)
    void requestClose();

private:
    struct Impl; // all the Win32/D3D11 state lives in dev_ui.cpp
    std::unique_ptr<Impl> impl_;
};

} // namespace fss::devui
