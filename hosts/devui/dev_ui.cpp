// dev_ui.cpp: Win32 window + D3D11 + Dear ImGui. Closely follows ImGui's own example
// (examples/example_win32_directx11/main.cpp in the ImGui tarball), wrapped in a RAII class.
#include "dev_ui.h"

#include <windows.h>
#include <d3d11.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <algorithm>
#include <cstdio>
#include <string>

// The Win32 backend declares this behind `#if 0` so that imgui_impl_win32.h doesn't need <windows.h>;
// every app that uses it re-declares it like this.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace fss::devui {

struct DevUi::Impl {
    HWND hwnd = nullptr;
    WNDCLASSEXW wc{};
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    IDXGISwapChain* swapChain = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    bool imguiReady = false;
    bool closed = false;
    bool occluded = false;      // window minimized/hidden: skip rendering instead of spinning the GPU
    UINT resizeW = 0, resizeH = 0; // pending resize, applied at the start of the next frame

    // The render target view wraps the swap chain's back buffer; it must be released before the
    // swap chain can resize its buffers, then recreated.
    void createRenderTarget()
    {
        ID3D11Texture2D* backBuffer = nullptr;
        if (SUCCEEDED(swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
            device->CreateRenderTargetView(backBuffer, nullptr, &rtv);
            backBuffer->Release();
        }
    }
    void releaseRenderTarget()
    {
        if (rtv) { rtv->Release(); rtv = nullptr; }
    }

    bool createDevice()
    {
        DXGI_SWAP_CHAIN_DESC sd{};
        sd.BufferCount = 2;
        sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.OutputWindow = hwnd;
        sd.SampleDesc.Count = 1;
        sd.Windowed = TRUE;
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
        // Width/height 0 = "use the window's client size".

        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
        D3D_FEATURE_LEVEL got{};
        HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                                   D3D11_SDK_VERSION, &sd, &swapChain, &device, &got, &context);
        if (hr == DXGI_ERROR_UNSUPPORTED) // no usable GPU (e.g. a VM or remote session): software rasterizer
            hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2,
                                               D3D11_SDK_VERSION, &sd, &swapChain, &device, &got, &context);
        if (FAILED(hr)) {
            std::fprintf(stderr, "devui: D3D11CreateDeviceAndSwapChain failed (0x%08lx)\n",
                         static_cast<unsigned long>(hr));
            return false;
        }
        createRenderTarget();
        return true;
    }

    void destroy()
    {
        if (imguiReady) {
            ImGui_ImplDX11_Shutdown();
            ImGui_ImplWin32_Shutdown();
            ImGui::DestroyContext();
            imguiReady = false;
        }
        releaseRenderTarget();
        if (swapChain) { swapChain->Release(); swapChain = nullptr; }
        if (context) { context->Release(); context = nullptr; }
        if (device) { device->Release(); device = nullptr; }
        if (hwnd) { DestroyWindow(hwnd); hwnd = nullptr; }
        if (wc.hInstance) { UnregisterClassW(wc.lpszClassName, wc.hInstance); wc.hInstance = nullptr; }
    }

    static LRESULT WINAPI wndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        // ImGui sees every message first (mouse, keyboard, focus, DPI...).
        if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
            return 1;
        // We stored `this` in the window's user data in open(), so the static callback can reach it.
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        switch (msg) {
        case WM_SIZE:
            if (self && wParam != SIZE_MINIMIZED) {
                self->resizeW = LOWORD(lParam);
                self->resizeH = HIWORD(lParam);
            }
            return 0;
        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU) // Alt alone would open the (nonexistent) window menu and
                return 0;                        // swallow the next key: annoying when playing QWERTY notes
            break;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
        }
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
};

DevUi::DevUi() : impl_(std::make_unique<Impl>()) {}

DevUi::~DevUi() { impl_->destroy(); }

bool DevUi::open(const char* title, int width, int height)
{
    Impl& s = *impl_;
    // Per-monitor DPI awareness: otherwise Windows bitmap-stretches the whole window on high-DPI screens
    // and everything is blurry. We then scale fonts/sizes ourselves (below).
    ImGui_ImplWin32_EnableDpiAwareness();

    s.wc = {sizeof(s.wc), CS_CLASSDC, Impl::wndProc, 0, 0, GetModuleHandleW(nullptr),
            nullptr, LoadCursorW(nullptr, IDC_ARROW), nullptr, nullptr, L"fss_devui", nullptr};
    if (!RegisterClassExW(&s.wc)) {
        std::fprintf(stderr, "devui: RegisterClassExW failed\n");
        s.wc.hInstance = nullptr;
        return false;
    }

    // Win32 wants UTF-16 titles.
    std::wstring wtitle(static_cast<size_t>(MultiByteToWideChar(CP_UTF8, 0, title, -1, nullptr, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle.data(), static_cast<int>(wtitle.size()));

    // `width`/`height` are the client area; AdjustWindowRect adds the title bar and borders.
    RECT r{0, 0, width, height};
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    s.hwnd = CreateWindowExW(0, s.wc.lpszClassName, wtitle.c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                             CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, s.wc.hInstance,
                             nullptr);
    if (!s.hwnd) {
        std::fprintf(stderr, "devui: CreateWindowExW failed\n");
        return false;
    }
    SetWindowLongPtrW(s.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&s));

    // The window is per-monitor DPI aware, so `width`/`height` are physical pixels, but fonts and widgets
    // are scaled by the DPI factor below. Scale the window too, or a 150 % screen gets a UI that doesn't fit.
    // Capped at 90 % of the monitor's work area. Done before the swap chain exists, which takes its size.
    if (const float dpi0 = ImGui_ImplWin32_GetDpiScaleForHwnd(s.hwnd); dpi0 > 1.0f) {
        MONITORINFO mi{};
        mi.cbSize = sizeof mi;
        GetMonitorInfoW(MonitorFromWindow(s.hwnd, MONITOR_DEFAULTTONEAREST), &mi);
        const LONG maxW = (mi.rcWork.right - mi.rcWork.left) * 9 / 10, maxH = (mi.rcWork.bottom - mi.rcWork.top) * 9 / 10;
        RECT rs{0, 0, static_cast<LONG>(static_cast<float>(width) * dpi0), static_cast<LONG>(static_cast<float>(height) * dpi0)};
        AdjustWindowRect(&rs, WS_OVERLAPPEDWINDOW, FALSE);
        SetWindowPos(s.hwnd, nullptr, mi.rcWork.left + 20, mi.rcWork.top + 20, (std::min)(rs.right - rs.left, maxW),
                     (std::min)(rs.bottom - rs.top, maxH), SWP_NOZORDER); // (std::min): dodge the windows.h min macro
    }

    if (!s.createDevice())
        return false;

    ShowWindow(s.hwnd, SW_SHOWDEFAULT);
    UpdateWindow(s.hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // No imgui.ini: a dev tool that silently writes files into whatever folder it runs from is a surprise.
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();
    const float dpi = ImGui_ImplWin32_GetDpiScaleForHwnd(s.hwnd); // 1.0 at 96 DPI, 1.5 at 144...
    ImGui::GetStyle().ScaleAllSizes(dpi);

    // ImGui's built-in font (ProggyClean) is a 13 px bitmap font: crisp but small. Segoe UI at 16 px is
    // easier to read; it ships with every Windows install. Fall back to the built-in one if missing.
    const float fontPx = 16.0f * dpi;
    if (GetFileAttributesW(L"C:\\Windows\\Fonts\\segoeui.ttf") == INVALID_FILE_ATTRIBUTES ||
        !io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\segoeui.ttf", fontPx)) {
        ImFontConfig cfg;
        cfg.SizePixels = fontPx;
        io.Fonts->AddFontDefault(&cfg);
    }

    ImGui_ImplWin32_Init(s.hwnd);
    ImGui_ImplDX11_Init(s.device, s.context);
    s.imguiReady = true;
    return true;
}

bool DevUi::beginFrame()
{
    Impl& s = *impl_;
    if (!s.imguiReady || s.closed)
        return false;

    // Drain every pending message, without blocking (PeekMessage, not GetMessage): we redraw every frame.
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        if (msg.message == WM_QUIT)
            s.closed = true;
    }
    if (s.closed)
        return false;

    // While minimized, Present would return immediately and we'd spin at 100% CPU; nap instead.
    if (s.occluded && s.swapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
        Sleep(10);
    s.occluded = false;

    if (s.resizeW != 0 && s.resizeH != 0) {
        s.releaseRenderTarget();
        s.swapChain->ResizeBuffers(0, s.resizeW, s.resizeH, DXGI_FORMAT_UNKNOWN, 0);
        s.resizeW = s.resizeH = 0;
        s.createRenderTarget();
    }

    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
    return true;
}

void DevUi::endFrame()
{
    Impl& s = *impl_;
    ImGui::Render();
    const float clear[4] = {0.08f, 0.08f, 0.10f, 1.0f};
    s.context->OMSetRenderTargets(1, &s.rtv, nullptr);
    s.context->ClearRenderTargetView(s.rtv, clear);
    ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    // SyncInterval 1 = wait for the next vertical blank: caps the loop at the monitor rate (60-144 fps)
    // so an idle GUI doesn't burn a CPU core the audio thread might want.
    const HRESULT hr = s.swapChain->Present(1, 0);
    s.occluded = (hr == DXGI_STATUS_OCCLUDED);
}

void DevUi::requestClose()
{
    if (impl_->hwnd)
        PostMessageW(impl_->hwnd, WM_CLOSE, 0, 0); // same path as clicking the X: WM_CLOSE -> WM_DESTROY -> WM_QUIT
}

} // namespace fss::devui
