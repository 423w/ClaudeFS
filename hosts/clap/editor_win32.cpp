// editor_win32.cpp: see editor_win32.h. The D3D11 + ImGui setup follows hosts/devui/dev_ui.cpp (itself ImGui's
// example_win32_directx11), with the differences a PLUGIN window needs:
//   - a CHILD window inside the host's window (WS_CHILD), not a top-level one, and no message loop of our own:
//     the host's loop delivers our messages; a WM_TIMER drives redraws;
//   - one ImGui CONTEXT PER EDITOR, made current at every entry point (ContextScope): two CFS instances can
//     have editors open at once, and ImGui's "current context" is a global inside this DLL;
//   - Present(0): never wait for vsync on the host's UI thread (a plugin must not stall the DAW's GUI);
//   - no imgui.ini (it would be written into whatever folder FL Studio runs from);
//   - the window class is registered against THIS DLL's module handle, and unregistered with the last editor.
#include "editor_win32.h"

#include <windows.h>
#include <d3d11.h>

#include <imgui.h>
#include <imgui_impl_dx11.h>
#include <imgui_impl_win32.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace fss::plugin {

namespace {

constexpr wchar_t kClassName[] = L"CFS_PluginEditor";
constexpr UINT_PTR kTimerId = 1;
constexpr UINT kFrameMs = 16; // ~60 Hz
int gClassUsers = 0;          // main thread only

HINSTANCE thisModule()
{
    // The DLL's own handle (not the host .exe's, which GetModuleHandle(nullptr) would give).
    HMODULE m = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&thisModule), &m);
    return m;
}

/// Makes `ctx` ImGui's current context for one scope, then restores whatever was current before.
class ContextScope {
public:
    explicit ContextScope(ImGuiContext* ctx) : saved_(ImGui::GetCurrentContext()) { ImGui::SetCurrentContext(ctx); }
    ~ContextScope() { ImGui::SetCurrentContext(saved_); }
    ContextScope(const ContextScope&) = delete;
    ContextScope& operator=(const ContextScope&) = delete;

private:
    ImGuiContext* saved_;
};

void applyStyle(float scale)
{
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle(); // ScaleAllSizes multiplies, so always start from the unscaled defaults
    ImGui::StyleColorsDark();
    s.WindowRounding = 0.0f;
    s.ChildRounding = 4.0f;
    s.FrameRounding = 3.0f;
    s.PopupRounding = 3.0f;
    s.ItemSpacing = ImVec2(6.0f, 5.0f);
    ImVec4* c = s.Colors;
    c[ImGuiCol_WindowBg] = ImVec4(0.11f, 0.12f, 0.14f, 1.0f);
    c[ImGuiCol_ChildBg] = ImVec4(0.14f, 0.15f, 0.17f, 1.0f);
    c[ImGuiCol_Border] = ImVec4(0.24f, 0.25f, 0.29f, 1.0f);
    c[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.21f, 0.25f, 1.0f);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.27f, 0.28f, 0.33f, 1.0f);
    c[ImGuiCol_Header] = ImVec4(0.45f, 0.33f, 0.15f, 1.0f);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.60f, 0.43f, 0.18f, 1.0f);
    c[ImGuiCol_Button] = ImVec4(0.22f, 0.23f, 0.27f, 1.0f);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.60f, 0.43f, 0.18f, 1.0f);
    c[ImGuiCol_CheckMark] = ImVec4(1.0f, 0.66f, 0.25f, 1.0f);
    c[ImGuiCol_Separator] = ImVec4(0.30f, 0.31f, 0.36f, 1.0f);
    s.ScaleAllSizes(scale);
}

void loadFonts(float scale)
{
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();
    // Segoe UI ships with every Windows since Vista: crisp at any size, unlike ImGui's built-in bitmap font,
    // which only looks right at 13 px. Read from the system font folder; nothing is downloaded or bundled.
    wchar_t dir[MAX_PATH]{};
    const UINT n = GetWindowsDirectoryW(dir, MAX_PATH);
    std::string path;
    for (UINT k = 0; k < n; ++k)
        path += static_cast<char>(dir[k] < 128 ? dir[k] : '?');
    path += "\\Fonts\\segoeui.ttf";
    ImFont* font = nullptr;
    if (GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES)
        font = io.Fonts->AddFontFromFileTTF(path.c_str(), 15.0f * scale);
    if (!font) {
        ImFontConfig cfg;
        cfg.SizePixels = 13.0f * scale;
        io.Fonts->AddFontDefault(&cfg);
    }
}

} // namespace

struct EditorWindow::Impl {
    EditorModel& model;
    EditorState state;
    HWND hwnd = nullptr;
    ImGuiContext* ctx = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* dc = nullptr;
    IDXGISwapChain* swapChain = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;
    double scale = 1.0;
    bool scaleFromHost = false, visible = false, fontsDirty = true;
    std::uint32_t width = 0, height = 0;   // physical pixels
    bool resizePending = false;

    explicit Impl(EditorModel& m) : model(m)
    {
        width = static_cast<std::uint32_t>(kEditorWidth);
        height = static_cast<std::uint32_t>(kEditorHeight);
    }

    void createTarget()
    {
        ID3D11Texture2D* back = nullptr;
        if (SUCCEEDED(swapChain->GetBuffer(0, IID_PPV_ARGS(&back)))) {
            device->CreateRenderTargetView(back, nullptr, &rtv);
            back->Release();
        }
    }
    void releaseTarget()
    {
        if (rtv) {
            rtv->Release();
            rtv = nullptr;
        }
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
        const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
        D3D_FEATURE_LEVEL got{};
        HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                                   D3D11_SDK_VERSION, &sd, &swapChain, &device, &got, &dc);
        if (FAILED(hr)) // no usable GPU (VM, remote desktop): the software rasterizer still works
            hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2, D3D11_SDK_VERSION,
                                               &sd, &swapChain, &device, &got, &dc);
        if (FAILED(hr))
            return false;
        createTarget();
        return true;
    }

    bool create(HWND parent)
    {
        const HINSTANCE inst = thisModule();
        if (gClassUsers++ == 0) {
            WNDCLASSEXW wc{sizeof wc};
            wc.style = CS_DBLCLKS; // ImGui wants double-click messages (knob: double-click = default)
            wc.lpfnWndProc = &Impl::wndProc;
            wc.hInstance = inst;
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            wc.lpszClassName = kClassName;
            RegisterClassExW(&wc);
        }
        if (!scaleFromHost) // the host didn't call set_scale: use the parent window's monitor DPI
            scale = std::max(1.0, GetDpiForWindow(parent) / 96.0);
        width = static_cast<std::uint32_t>(kEditorWidth * scale);
        height = static_cast<std::uint32_t>(kEditorHeight * scale);
        hwnd = CreateWindowExW(0, kClassName, L"CFS", WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN, 0, 0,
                               static_cast<int>(width), static_cast<int>(height), parent, nullptr, inst, nullptr);
        if (!hwnd)
            return false;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        if (!createDevice())
            return false;
        ctx = ImGui::CreateContext();
        const ContextScope scope(ctx);
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr; // no imgui.ini next to the DAW
        io.LogFilename = nullptr;
        ImGui_ImplWin32_Init(hwnd);
        ImGui_ImplDX11_Init(device, dc);
        return true;
    }

    void destroy()
    {
        if (hwnd)
            KillTimer(hwnd, kTimerId);
        if (ctx) {
            {
                const ContextScope scope(ctx);
                ImGui_ImplDX11_Shutdown();
                ImGui_ImplWin32_Shutdown();
            }
            ImGui::DestroyContext(ctx);
            ctx = nullptr;
        }
        releaseTarget();
        if (swapChain) {
            swapChain->Release();
            swapChain = nullptr;
        }
        if (dc) {
            dc->Release();
            dc = nullptr;
        }
        if (device) {
            device->Release();
            device = nullptr;
        }
        if (hwnd) {
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0); // late messages must not reach a dead object
            DestroyWindow(hwnd);
            hwnd = nullptr;
            if (--gClassUsers == 0)
                UnregisterClassW(kClassName, thisModule());
        }
    }

    /// One frame. `snapshotPath`: also write the frame to a BMP before presenting it.
    bool render(bool force, const char* snapshotPath)
    {
        if (!ctx || (!visible && !force))
            return false;
        const ContextScope scope(ctx);
        if (resizePending && swapChain) {
            resizePending = false;
            releaseTarget();
            swapChain->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
            createTarget();
        }
        if (fontsDirty) {
            fontsDirty = false;
            loadFonts(static_cast<float>(scale));
            applyStyle(static_cast<float>(scale));
            ImGui_ImplDX11_InvalidateDeviceObjects(); // the font texture is rebuilt on the next NewFrame
        }
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        drawEditor(model, state);
        ImGui::Render();
        const float clear[4] = {0.11f, 0.12f, 0.14f, 1.0f};
        dc->OMSetRenderTargets(1, &rtv, nullptr);
        dc->ClearRenderTargetView(rtv, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        const bool ok = snapshotPath ? writeSnapshot(snapshotPath) : true;
        swapChain->Present(0, 0); // 0 = don't wait for vsync on the host's UI thread
        return ok;
    }

    bool writeSnapshot(const char* path)
    {
        // Copy the back buffer into a CPU-readable texture, then write it as a top-down 32-bit BMP.
        ID3D11Texture2D* back = nullptr;
        if (FAILED(swapChain->GetBuffer(0, IID_PPV_ARGS(&back))))
            return false;
        D3D11_TEXTURE2D_DESC desc{};
        back->GetDesc(&desc);
        desc.Usage = D3D11_USAGE_STAGING;
        desc.BindFlags = 0;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        desc.MiscFlags = 0;
        ID3D11Texture2D* staging = nullptr;
        bool ok = SUCCEEDED(device->CreateTexture2D(&desc, nullptr, &staging));
        if (ok) {
            dc->CopyResource(staging, back);
            D3D11_MAPPED_SUBRESOURCE map{};
            ok = SUCCEEDED(dc->Map(staging, 0, D3D11_MAP_READ, 0, &map));
            if (ok) {
                std::vector<std::uint8_t> pixels(static_cast<std::size_t>(desc.Width) * desc.Height * 4);
                for (UINT y = 0; y < desc.Height; ++y) {
                    const auto* row = static_cast<const std::uint8_t*>(map.pData) + static_cast<std::size_t>(y) * map.RowPitch;
                    for (UINT x = 0; x < desc.Width; ++x) {
                        std::uint8_t* o = &pixels[(static_cast<std::size_t>(y) * desc.Width + x) * 4];
                        o[0] = row[x * 4 + 2]; // RGBA → BGRA
                        o[1] = row[x * 4 + 1];
                        o[2] = row[x * 4 + 0];
                        o[3] = 255;
                    }
                }
                dc->Unmap(staging, 0);
                BITMAPFILEHEADER fh{};
                BITMAPINFOHEADER ih{};
                ih.biSize = sizeof ih;
                ih.biWidth = static_cast<LONG>(desc.Width);
                ih.biHeight = -static_cast<LONG>(desc.Height); // negative = rows top to bottom
                ih.biPlanes = 1;
                ih.biBitCount = 32;
                ih.biCompression = BI_RGB;
                fh.bfType = 0x4D42; // "BM"
                fh.bfOffBits = sizeof fh + sizeof ih;
                fh.bfSize = fh.bfOffBits + static_cast<DWORD>(pixels.size());
                std::FILE* f = nullptr;
                ok = fopen_s(&f, path, "wb") == 0 && f;
                if (ok) {
                    std::fwrite(&fh, sizeof fh, 1, f);
                    std::fwrite(&ih, sizeof ih, 1, f);
                    std::fwrite(pixels.data(), 1, pixels.size(), f);
                    std::fclose(f);
                }
            }
            staging->Release();
        }
        back->Release();
        return ok;
    }

    static LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
    {
        auto* self = reinterpret_cast<Impl*>(GetWindowLongPtrW(h, GWLP_USERDATA));
        if (self && self->ctx) {
            const ContextScope scope(self->ctx); // ImGui's handler works on the CURRENT context
            if (ImGui_ImplWin32_WndProcHandler(h, msg, wp, lp))
                return 1;
        }
        switch (msg) {
        case WM_TIMER:
            if (self && wp == kTimerId)
                self->render(false, nullptr);
            return 0;
        case WM_SIZE:
            if (self && wp != SIZE_MINIMIZED) {
                self->width = LOWORD(lp);
                self->height = HIWORD(lp);
                self->resizePending = true;
            }
            return 0;
        case WM_LBUTTONDOWN:
        case WM_RBUTTONDOWN:
            SetFocus(h); // so typing (the preset name) comes to us, if the host lets it
            break;
        case WM_GETDLGCODE:
            return DLGC_WANTALLKEYS; // hosts that use dialog-style key handling: give us the keys
        case WM_ERASEBKGND:
            return 1; // D3D paints everything; erasing first would flicker
        default:
            break;
        }
        return DefWindowProcW(h, msg, wp, lp);
    }
};

EditorWindow::EditorWindow(EditorModel& model) : impl_(std::make_unique<Impl>(model)) {}

EditorWindow::~EditorWindow() { impl_->destroy(); }

bool EditorWindow::setParent(void* parentHwnd)
{
    const auto parent = static_cast<HWND>(parentHwnd);
    if (!parent)
        return false;
    if (impl_->hwnd) { // already created: move it under the new parent
        SetParent(impl_->hwnd, parent);
        return true;
    }
    return impl_->create(parent);
}

void EditorWindow::setScale(double scale)
{
    if (scale <= 0.0)
        return;
    impl_->scaleFromHost = true;
    if (scale != impl_->scale) {
        impl_->scale = scale;
        impl_->fontsDirty = true;
    }
}

void EditorWindow::size(std::uint32_t& width, std::uint32_t& height) const
{
    if (impl_->hwnd) {
        width = impl_->width;
        height = impl_->height;
    } else {
        width = static_cast<std::uint32_t>(kEditorWidth * impl_->scale);
        height = static_cast<std::uint32_t>(kEditorHeight * impl_->scale);
    }
}

void EditorWindow::adjustSize(std::uint32_t& width, std::uint32_t& height) const
{
    width = std::max(width, static_cast<std::uint32_t>(kEditorMinWidth * impl_->scale));
    height = std::max(height, static_cast<std::uint32_t>(kEditorMinHeight * impl_->scale));
}

bool EditorWindow::setSize(std::uint32_t width, std::uint32_t height)
{
    adjustSize(width, height);
    impl_->width = width;
    impl_->height = height;
    impl_->resizePending = true;
    if (impl_->hwnd)
        SetWindowPos(impl_->hwnd, nullptr, 0, 0, static_cast<int>(width), static_cast<int>(height),
                     SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    return true;
}

bool EditorWindow::show()
{
    if (!impl_->hwnd)
        return false;
    impl_->visible = true;
    ShowWindow(impl_->hwnd, SW_SHOWNA);
    SetTimer(impl_->hwnd, kTimerId, kFrameMs, nullptr);
    impl_->render(false, nullptr); // first frame now, not 16 ms later
    return true;
}

bool EditorWindow::hide()
{
    if (!impl_->hwnd)
        return false;
    impl_->visible = false;
    KillTimer(impl_->hwnd, kTimerId); // a hidden editor costs nothing
    ShowWindow(impl_->hwnd, SW_HIDE);
    return true;
}

bool EditorWindow::snapshot(const char* bmpPath) { return impl_->render(true, bmpPath); }

void EditorWindow::renderNow() { impl_->render(true, nullptr); }

bool EditorWindow::controlRect(clap_id paramId, int rect[4]) const
{
    const int i = indexOf(paramId);
    if (i < 0 || !impl_->state.drawn[static_cast<std::size_t>(i)])
        return false;
    for (int k = 0; k < 4; ++k)
        rect[k] = static_cast<int>(impl_->state.rects[static_cast<std::size_t>(i)][static_cast<std::size_t>(k)]);
    return true;
}

} // namespace fss::plugin
