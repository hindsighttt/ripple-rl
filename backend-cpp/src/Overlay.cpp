#include <windows.h>

#include "Overlay.h"

#include "WinUtil.h"

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
constexpr UINT OVL_REFRESH = WM_APP + 1;
constexpr UINT OVL_VIS = WM_APP + 2;
constexpr wchar_t kClass[] = L"RippleOverlayWindow";

enum class RosterStyle { Compact, Glassmorph, Pill, Sidebar, Vivid };

struct RosterRow {
    std::wstring name;
    std::wstring score;
    COLORREF accent;
};

void DrawRoster(HDC dc, const RECT& area, const std::vector<RosterRow>& rows, RosterStyle style) {
    const int height = style == RosterStyle::Compact ? 19 : 24;
    int y = 4;
    for (const auto& row : rows) {
        if (y + height > area.bottom)
            break;
        if (style != RosterStyle::Compact) {
            const COLORREF panel = style == RosterStyle::Vivid
                ? RGB(GetRValue(row.accent) / 3, GetGValue(row.accent) / 3, GetBValue(row.accent) / 3)
                : style == RosterStyle::Glassmorph ? RGB(43, 52, 70) : RGB(24, 30, 43);
            HBRUSH brush = CreateSolidBrush(panel);
            auto oldBrush = SelectObject(dc, brush);
            auto oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
            const int radius = style == RosterStyle::Pill ? height : 6;
            RoundRect(dc, 4, y, area.right - 4, y + height - 2, radius, radius);
            SelectObject(dc, oldPen);
            SelectObject(dc, oldBrush);
            DeleteObject(brush);
            if (style == RosterStyle::Sidebar) {
                RECT stripe{4, y + 2, 8, y + height - 4};
                HBRUSH accent = CreateSolidBrush(row.accent);
                FillRect(dc, &stripe, accent);
                DeleteObject(accent);
            }
        }
        RECT name{14, y, area.right - 58, y + height - 2};
        SetTextColor(dc, style == RosterStyle::Compact ? row.accent : RGB(250, 251, 255));
        DrawTextW(dc, row.name.c_str(), -1, &name, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        RECT score{area.right - 56, y, area.right - 14, y + height - 2};
        SetTextColor(dc, style == RosterStyle::Vivid ? RGB(250, 251, 255) : row.accent);
        DrawTextW(dc, row.score.c_str(), -1, &score, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        y += height;
    }
}
}  // namespace

struct Overlays::Impl {
    std::thread thread;
    std::atomic<bool> ready{false};
    DWORD tid = 0;

    HWND hBall = nullptr, hFps = nullptr, hRoster = nullptr;
    HFONT fBall = nullptr, fFps = nullptr, fRoster = nullptr;

    std::mutex mtx;
    std::wstring ballText, fpsText;
    std::vector<RosterRow> roster;
    RosterStyle rosterStyle = RosterStyle::Compact;
    std::atomic<bool> vBall{false}, vFps{false}, vRoster{false};

    // Per-window tag stored in GWLP_USERDATA.
    struct Win {
        Impl* impl;
        int kind;  // 0 ball, 1 fps, 2 roster
    };
    Win wBall{this, 0}, wFps{this, 1}, wRoster{this, 2};

    static HFONT MakeFont(int pt, bool bold) {
        return CreateFontW(-(pt * 96 / 72), 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, 0, 0, 0,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    }

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        Win* w = (Win*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
        if (!w) return DefWindowProcW(hwnd, msg, wp, lp);
        Impl* self = w->impl;
        switch (msg) {
            case OVL_REFRESH:
                InvalidateRect(hwnd, nullptr, TRUE);
                return 0;
            case OVL_VIS: {
                bool on = w->kind == 0 ? self->vBall.load() : w->kind == 1 ? self->vFps.load()
                                                                           : self->vRoster.load();
                ShowWindow(hwnd, on ? SW_SHOWNOACTIVATE : SW_HIDE);
                return 0;
            }
            case WM_PAINT: {
                PAINTSTRUCT ps;
                HDC hdc = BeginPaint(hwnd, &ps);
                RECT rc;
                GetClientRect(hwnd, &rc);
                FillRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));  // colour-keyed away
                SetBkMode(hdc, TRANSPARENT);
                std::lock_guard<std::mutex> lk(self->mtx);
                if (w->kind == 0) {
                    SelectObject(hdc, self->fBall);
                    SetTextColor(hdc, RGB(0x7C, 0xFC, 0x00));
                    RECT r = {10, 0, rc.right - 10, rc.bottom};
                    DrawTextW(hdc, self->ballText.c_str(), -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                } else if (w->kind == 1) {
                    SelectObject(hdc, self->fFps);
                    SetTextColor(hdc, RGB(0x7C, 0xFC, 0x00));
                    RECT r = {10, 0, rc.right - 10, rc.bottom};
                    DrawTextW(hdc, self->fpsText.c_str(), -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                } else {
                    SelectObject(hdc, self->fRoster);
                    DrawRoster(hdc, rc, self->roster, self->rosterStyle);
                }
                EndPaint(hwnd, &ps);
                return 0;
            }
        }
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    HWND MakeWin(Win* w, int width, int height, int x, int y) {
        HWND hwnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            kClass, L"", WS_POPUP, x, y, width, height, nullptr, nullptr, GetModuleHandleW(nullptr),
            nullptr);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)w);
        SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);
        return hwnd;
    }

    void Pump() {
        tid = GetCurrentThreadId();
        WNDCLASSW wc{};
        wc.lpfnWndProc = WndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClass;
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        RegisterClassW(&wc);

        fBall = MakeFont(20, true);
        fFps = MakeFont(14, true);
        fRoster = MakeFont(13, false);
        hBall = MakeWin(&wBall, 160, 40, 40, 40);
        hFps = MakeWin(&wFps, 260, 34, 40, 90);
        RECT workArea{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &workArea, 0);
        hRoster = MakeWin(&wRoster, 300, 310, (std::max)(workArea.left, workArea.right - 330), workArea.top + 130);
        ready = true;

        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        DestroyWindow(hBall);
        DestroyWindow(hFps);
        DestroyWindow(hRoster);
        DeleteObject(fBall);
        DeleteObject(fFps);
        DeleteObject(fRoster);
        UnregisterClassW(kClass, GetModuleHandleW(nullptr));
    }

    void Refresh(HWND h) { if (h) PostMessageW(h, OVL_REFRESH, 0, 0); }
    void ApplyVis(HWND h) { if (h) PostMessageW(h, OVL_VIS, 0, 0); }
};

Overlays::Overlays() : p_(new Impl) {
    p_->thread = std::thread(&Impl::Pump, p_);
    for (int i = 0; i < 2000 && !p_->ready.load(); ++i) Sleep(1);  // wait for window creation
}

Overlays::~Overlays() {
    if (p_->tid) PostThreadMessageW(p_->tid, WM_QUIT, 0, 0);
    if (p_->thread.joinable()) p_->thread.join();
    delete p_;
}

void Overlays::ShowBall(bool on) { p_->vBall = on; p_->ApplyVis(p_->hBall); }
void Overlays::ShowRoster(bool on) { p_->vRoster = on; p_->ApplyVis(p_->hRoster); }
void Overlays::ShowFps(bool on) { p_->vFps = on; p_->ApplyVis(p_->hFps); }
bool Overlays::BallVisible() const { return p_->vBall.load(); }
bool Overlays::RosterVisible() const { return p_->vRoster.load(); }
bool Overlays::FpsVisible() const { return p_->vFps.load(); }
void Overlays::SetFpsText(std::wstring text) {
    {
        std::lock_guard<std::mutex> lk(p_->mtx);
        if (p_->fpsText == text)
            return;
        p_->fpsText = std::move(text);
    }
    p_->Refresh(p_->hFps);
}

void Overlays::ClearBall() {
    {
        std::lock_guard<std::mutex> lk(p_->mtx);
        if (p_->ballText.empty())
            return;
        p_->ballText.clear();
    }
    p_->Refresh(p_->hBall);
}

void Overlays::SetBall(double speedUu, const std::string& unit, int decimals) {
    double factor = 0.036;
    const char* label = "km/h";
    if (unit == "mph") {
        factor = 0.0223693629;
        label = "mph";
    } else if (unit == "ms" || unit == "mps" || unit == "m/s") {
        factor = 0.01;
        label = "m/s";
    }
    int d = (std::max)(0, (std::min)(3, decimals));
    double v = (std::max)(0.0, speedUu) * factor;
    wchar_t buf[64];
    swprintf(buf, 64, L"%.*f %hs", d, v, label);
    {
        std::lock_guard<std::mutex> lk(p_->mtx);
        p_->ballText = buf;
    }
    p_->Refresh(p_->hBall);
}

void Overlays::SetRoster(const json& players) {
    std::vector<RosterRow> rows;
    if (players.is_array())
        for (auto& pl : players) {
            std::string name = pl.is_object() && pl.contains("Name") && pl["Name"].is_string()
                                   ? pl["Name"].get<std::string>()
                                   : "";
            if (name.empty())
                continue;
            const auto team = pl.find("TeamNum");
            COLORREF color = RGB(232, 236, 245);
            if (team != pl.end() && *team == 0)
                color = RGB(122, 180, 255);
            else if (team != pl.end() && *team == 1)
                color = RGB(255, 167, 105);
            const auto score = pl.find("Score");
            rows.push_back({win::Widen(name), score != pl.end() && score->is_number_integer()
                                               ? std::to_wstring(score->get<int>()) : L"", color});
        }
    {
        std::lock_guard<std::mutex> lk(p_->mtx);
        p_->roster = std::move(rows);
    }
    p_->Refresh(p_->hRoster);
}

void Overlays::SetRosterStyle(const std::string& style) {
    RosterStyle value = RosterStyle::Compact;
    if (style == "glassmorph") value = RosterStyle::Glassmorph;
    else if (style == "pill") value = RosterStyle::Pill;
    else if (style == "sidebar") value = RosterStyle::Sidebar;
    else if (style == "vivid") value = RosterStyle::Vivid;
    {
        std::lock_guard<std::mutex> lk(p_->mtx);
        p_->rosterStyle = value;
    }
    p_->Refresh(p_->hRoster);
}
