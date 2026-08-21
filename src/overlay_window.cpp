#include "overlay_window.h"

#include <shellapi.h>

#include <string>

namespace overlay_window {
namespace {

constexpr wchar_t kClassName[] = L"SmoothCursorOverlayGhostWindow";

// Launches the standalone settings GUI, expected to sit next to this exe
// as "SmoothCursorOverlaySettings.exe". It edits config.ini directly; the
// overlay picks up changes within about a second (see main.cpp's periodic
// reload), so no IPC between the two processes is needed.
void LaunchSettingsWindow() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    std::wstring path(exePath);
    size_t slash = path.find_last_of(L"\\/");
    std::wstring dir = slash == std::wstring::npos ? L"." : path.substr(0, slash);
    std::wstring settingsExe = dir + L"\\SmoothCursorOverlaySettings.exe";

    ShellExecuteW(nullptr, L"open", settingsExe.c_str(), nullptr, dir.c_str(),
                  SW_SHOWNORMAL);
}

void ShowTrayMenu(HWND hwnd, WindowContext* ctx) {
    POINT pt;
    GetCursorPos(&pt);

    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu,
                MF_STRING | ((ctx && ctx->manuallyDisabled) ? 0 : MF_CHECKED),
                kMenuIdToggle, L"Etkin");
    AppendMenuW(menu, MF_STRING, kMenuIdSettings, L"Ayarlar");
    AppendMenuW(menu, MF_STRING, kMenuIdExit, L"\xC7\x131k\x131\x15f");  // "Çıkış"

    // Standard tray-menu dismiss-on-click-away workaround (documented by
    // Microsoft): the window must be foreground while the menu is tracked,
    // and needs a following WM_NULL or the menu can get stuck open.
    SetForegroundWindow(hwnd);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
    PostMessageW(hwnd, WM_NULL, 0, 0);

    DestroyMenu(menu);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* ctx = reinterpret_cast<WindowContext*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_NCHITTEST:
            // Explicit HTTRANSPARENT as a belt-and-suspenders measure
            // alongside WS_EX_TRANSPARENT: the standard, more bulletproof
            // way to build a fully click-through overlay, including
            // resize-border drags on windows underneath.
            return HTTRANSPARENT;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_INPUT:
            if (ctx) {
                raw_input::HandleWmInput(lParam, ctx->rawInput);
            }
            return 0;
        case kTrayIconMessage:
            // Left click is reserved for double-click (opens settings,
            // the conventional tray-icon shortcut -- including from the
            // Windows 11 "hidden icons" flyout). A single WM_LBUTTONUP no
            // longer also opens the context menu: since a double-click's
            // first click still fires its own WM_LBUTTONUP, having both
            // wired raced TrackPopupMenu's blocking message pump against
            // the second click. Right click keeps the context menu.
            if (LOWORD(lParam) == WM_LBUTTONDBLCLK) {
                LaunchSettingsWindow();
            } else if (LOWORD(lParam) == WM_RBUTTONUP) {
                ShowTrayMenu(hwnd, ctx);
            }
            return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == kMenuIdSettings) {
                LaunchSettingsWindow();
            } else if (ctx) {
                if (LOWORD(wParam) == kMenuIdToggle) {
                    ctx->manuallyDisabled = !ctx->manuallyDisabled;
                } else if (LOWORD(wParam) == kMenuIdExit) {
                    ctx->exitRequested = true;
                }
            }
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

}  // namespace

RECT GetVirtualDesktopBounds() {
    RECT r;
    r.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
    r.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
    r.right = r.left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    r.bottom = r.top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    return r;
}

HWND Create(HINSTANCE hInstance, const RECT& bounds) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kClassName;
    wc.hCursor = nullptr;  // click-through window; never shows its own cursor
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOPMOST |
            WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kClassName, L"Smooth Cursor Overlay", WS_POPUP,
        bounds.left, bounds.top, bounds.right - bounds.left,
        bounds.bottom - bounds.top, nullptr, nullptr, hInstance, nullptr);

    if (hwnd) {
        ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    }
    return hwnd;
}

}  // namespace overlay_window
