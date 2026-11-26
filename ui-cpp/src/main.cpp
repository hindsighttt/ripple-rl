// Native ImGui / DirectX 11 frontend for the local Ripple RL backend.

#include "App.h"
#include "Backend.h"
#include "BackendHost.h"
#include "ImageCache.h"
#include "MenuTheme.h"

#include "Cupertino.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_win32.h"

#include <d3d11.h>
#include <dwmapi.h>
#include <windows.h>
#include <windowsx.h>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

static ID3D11Device* g_device = nullptr;
static ID3D11DeviceContext* g_context = nullptr;
static IDXGISwapChain* g_swapChain = nullptr;
static ID3D11RenderTargetView* g_renderTarget = nullptr;
static bool g_occluded = false;
static UINT g_resizeW = 0, g_resizeH = 0;
static float g_scale = 1.0f;
static bool g_titleHover = false;  // mouse over empty title-bar area this frame (set after render)

static void CreateRenderTarget() {
    ID3D11Texture2D* back = nullptr;
    g_swapChain->GetBuffer(0, IID_PPV_ARGS(&back));
    g_device->CreateRenderTargetView(back, nullptr, &g_renderTarget);
    back->Release();
}

static void DestroyRenderTarget() {
    if (g_renderTarget) { g_renderTarget->Release(); g_renderTarget = nullptr; }
}

static bool CreateDevice(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC desc = {};
    desc.BufferCount = 2;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferDesc.RefreshRate.Numerator = 60;
    desc.BufferDesc.RefreshRate.Denominator = 1;
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = hwnd;
    desc.SampleDesc.Count = 1;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    D3D_FEATURE_LEVEL level;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2,
                                               D3D11_SDK_VERSION, &desc, &g_swapChain, &g_device, &level, &g_context);
    if (hr == DXGI_ERROR_UNSUPPORTED)
        hr = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0, levels, 2,
                                           D3D11_SDK_VERSION, &desc, &g_swapChain, &g_device, &level, &g_context);
    if (hr != S_OK)
        return false;
    CreateRenderTarget();
    return true;
}

static void DestroyDevice() {
    DestroyRenderTarget();
    if (g_swapChain) { g_swapChain->Release(); g_swapChain = nullptr; }
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }
}

static LRESULT WINAPI WindowProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam))
        return true;
    switch (msg) {
        case WM_GETMINMAXINFO: {
            auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
            limits->ptMinTrackSize.x = static_cast<LONG>(620 * g_scale);
            limits->ptMinTrackSize.y = static_cast<LONG>(440 * g_scale);
            return 0;
        }
        case WM_DPICHANGED: {
            g_scale = HIWORD(wparam) / 96.0f;
            const auto* rect = reinterpret_cast<const RECT*>(lparam);
            SetWindowPos(hwnd, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            return 0;
        }
        case WM_SIZE:
            if (wparam != SIZE_MINIMIZED) { g_resizeW = LOWORD(lparam); g_resizeH = HIWORD(lparam); }
            return 0;
        case WM_SYSCOMMAND:
            if ((wparam & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_NCCALCSIZE:
            // Make the client area cover the whole window: no OS title bar or borders,
            // while DWM still gives the window its shadow, rounded corners and snap.
            if (wparam)
                return 0;
            break;
        case WM_NCHITTEST: {
            POINT pt = {GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
            ScreenToClient(hwnd, &pt);
            RECT rc;
            GetClientRect(hwnd, &rc);
            const int border = int(6 * g_scale);
            const int titleBar = int(MenuTheme::HeaderHeight * g_scale);
            const bool left = pt.x < border, right = pt.x >= rc.right - border;
            const bool top = pt.y < border, bottom = pt.y >= rc.bottom - border;
            if (top && left) return HTTOPLEFT;
            if (top && right) return HTTOPRIGHT;
            if (bottom && left) return HTBOTTOMLEFT;
            if (bottom && right) return HTBOTTOMRIGHT;
            if (left) return HTLEFT;
            if (right) return HTRIGHT;
            if (top) return HTTOP;
            if (bottom) return HTBOTTOM;
            if (pt.y < titleBar && g_titleHover) return HTCAPTION;
            return HTCLIENT;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    ImGui_ImplWin32_EnableDpiAwareness();
    const float scale = ImGui_ImplWin32_GetDpiScaleForMonitor(MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY));
    g_scale = scale;
    const WNDCLASSEXW wc = {sizeof(wc), CS_CLASSDC, WindowProc, 0, 0, instance, nullptr, nullptr, nullptr, nullptr, L"RippleRL", nullptr};
    RegisterClassExW(&wc);
    RECT workArea{};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
    const int width = ImMin(static_cast<int>(940 * scale), static_cast<int>(workArea.right - workArea.left));
    const int height = ImMin(static_cast<int>(620 * scale), static_cast<int>(workArea.bottom - workArea.top));
    HWND hwnd = CreateWindowW(wc.lpszClassName, L"Ripple RL", WS_OVERLAPPEDWINDOW,
                              workArea.left + (workArea.right - workArea.left - width) / 2,
                              workArea.top + (workArea.bottom - workArea.top - height) / 2,
                              width, height, nullptr, nullptr, instance, nullptr);
    // Frameless custom chrome: hide the OS title bar (WM_NCCALCSIZE) but keep DWM's
    // rounded corners, shadow and snap.
    const DWORD DWMWA_WINDOW_CORNER_PREFERENCE_ = 33;
    const int DWMWCP_ROUND_ = 2;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE_, &DWMWCP_ROUND_, sizeof(DWMWCP_ROUND_));
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    if (!CreateDevice(hwnd)) {
        DestroyDevice();
        UnregisterClassW(wc.lpszClassName, instance);
        return 1;
    }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_device, g_context);
    if (!Cupertino::Initialize({})) {
        MessageBoxW(hwnd, L"CupertinoUi failed to initialize (missing baked fonts).", L"Ripple RL", MB_ICONERROR);
        return 2;
    }

    if (const auto resource = FindResourceW(instance, MAKEINTRESOURCEW(101), MAKEINTRESOURCEW(10))) {
        if (void* data = LockResource(LoadResource(instance, resource))) {
            ImFontConfig font;
            font.FontDataOwnedByAtlas = false;
            ImGui::GetIO().FontDefault = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
                data, static_cast<int>(SizeofResource(instance, resource)), 0.0f, &font);
        }
    }
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Host the native backend in-process (single exe): the socket client below
    // then connects to it over loopback.
    BackendHost backendHost;
    backendHost.Start();

    Backend backend;
    ImageCache images;
    images.Init(g_device, 6);
    App app(backend, images);
    backend.OnEvent([&](const std::string& type, const nlohmann::json& payload) { app.OnEvent(type, payload); });
    double lastConnectTry = -10.0;

    bool quit = false;
    while (!quit) {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT)
                quit = true;
        }
        if (quit)
            break;
        if (g_occluded && g_swapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            Sleep(10);
            continue;
        }
        g_occluded = false;
        if (g_resizeW != 0 && g_resizeH != 0) {
            DestroyRenderTarget();
            g_swapChain->ResizeBuffers(0, g_resizeW, g_resizeH, DXGI_FORMAT_UNKNOWN, 0);
            g_resizeW = g_resizeH = 0;
            CreateRenderTarget();
        }

        const double now = ImGui::GetTime();
        if (!backend.Connected() && now - lastConnectTry > 1.0) {
            lastConnectTry = now;
            backend.Connect("127.0.0.1", 49327);
        }
        backend.Poll();
        images.Poll();

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        Cupertino::EnvironmentValues& env = Cupertino::Environment();
        env.displayScale = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
        env.appearance = Cupertino::Appearance::Dark;

        switch (app.Render()) {
            case App::WindowCommand::Close: quit = true; break;
            case App::WindowCommand::Minimize: ShowWindow(hwnd, SW_MINIMIZE); break;
            case App::WindowCommand::ToggleMaximize: ShowWindow(hwnd, IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE); break;
            case App::WindowCommand::None: break;
        }
        // The empty part of the header acts as the native window drag area.
        {
            const ImGuiIO& io = ImGui::GetIO();
            g_titleHover = io.MousePos.y >= 0.0f && io.MousePos.y < MenuTheme::HeaderHeight * g_scale
                           && !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive();
        }

        ImGui::Render();
        const float clear[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        g_context->OMSetRenderTargets(1, &g_renderTarget, nullptr);
        g_context->ClearRenderTargetView(g_renderTarget, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_occluded = g_swapChain->Present(1, 0) == DXGI_STATUS_OCCLUDED;
    }

    images.Shutdown();
    backend.Disconnect();
    backendHost.Stop();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    DestroyDevice();
    DestroyWindow(hwnd);
    UnregisterClassW(wc.lpszClassName, instance);
    return 0;
}
