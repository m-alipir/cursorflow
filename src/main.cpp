#include <windows.h>
#include <shellapi.h>

#include <chrono>

#include "anticheat_watcher.h"
#include "config.h"
#include "crash_restore.h"
#include "cursor_scheme.h"
#include "cursor_shape_sync.h"
#include "overlay_window.h"
#include "physics.h"
#include "raw_input.h"
#include "renderer_d2d.h"

namespace {

// Proper UTF-8 -> UTF-16 conversion (not a narrowing char-by-char cast):
// layer1CustomCursorPath is a real filesystem path and can contain
// non-ASCII characters (e.g. a non-ASCII username in the path).
std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (size <= 0) return L"";
    std::wstring result(static_cast<size_t>(size) - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, result.data(), size);
    return result;
}

cursor_scheme::Style ParseLayer1Style(const std::string& s) {
    if (s == "thin_cross") return cursor_scheme::Style::kThinCross;
    if (s == "dot") return cursor_scheme::Style::kDot;
    if (s == "custom") return cursor_scheme::Style::kCustom;
    return cursor_scheme::Style::kThickCross;
}

void AddTrayIcon(HWND hwnd, NOTIFYICONDATAW& nid) {
    nid = {};
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = 1;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = overlay_window::kTrayIconMessage;
    nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(nid.szTip, L"Smooth Cursor Overlay");
    Shell_NotifyIconW(NIM_ADD, &nid);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    if (crash_restore::RunIfWatchdogMode()) {
        return 0;
    }

    crash_restore::Install();

    RECT bounds = overlay_window::GetVirtualDesktopBounds();
    HWND hwnd = overlay_window::Create(hInstance, bounds);
    if (!hwnd) {
        return 0;
    }

    renderer_d2d::Renderer renderer;
    if (!renderer.Initialize(hwnd, bounds.right - bounds.left,
                              bounds.bottom - bounds.top)) {
        return 0;
    }

    config::Settings settings = config::Load();

    // Capture the user's real/default cursor appearance BEFORE overriding
    // it below -- this is the ghost's default shape (instead of a generic
    // placeholder circle), so Layer 2 reads as "your actual cursor,
    // delayed and blurred" rather than an unrelated shape. Must happen
    // before ApplyOverride(): afterward, GetCursorInfo() only ever reports
    // our own fixed Layer 1 shape.
    Microsoft::WRL::ComPtr<ID2D1Bitmap> defaultCursorBitmap;
    POINT defaultCursorHotspot{};
    bool haveDefaultCursor = cursor_shape_sync::CaptureCurrentSystemCursor(
        renderer.GetContext(), defaultCursorBitmap, defaultCursorHotspot);

    // Pristine bitmaps for a handful of stock cursor roles (ibeam, hand,
    // resize, ...), captured now for the same reason as above -- once
    // ApplyOverride() runs below, SetSystemCursor has overwritten every
    // role's content with Layer 1's fixed shape and there's nothing
    // distinct left to read back. See cursor_shape_sync.h's DetectActiveRole
    // for how these get selected per-frame despite that.
    cursor_shape_sync::RoleCursors roleCursors;
    cursor_shape_sync::CaptureAllRoleCursors(renderer.GetContext(), roleCursors);

    cursor_scheme::ApplyOverride(
        ParseLayer1Style(settings.layer1Style), settings.layer1Invert,
        Utf8ToWide(settings.layer1CustomCursorPath));
    std::string appliedLayer1Style = settings.layer1Style;
    bool appliedLayer1Invert = settings.layer1Invert;
    std::string appliedLayer1Path = settings.layer1CustomCursorPath;

    renderer.Configure(settings.blurIntensity, settings.trailLength,
                        settings.ghostScale);

    overlay_window::WindowContext windowContext;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                       reinterpret_cast<LONG_PTR>(&windowContext));
    raw_input::Register(hwnd);

    NOTIFYICONDATAW trayIcon;
    AddTrayIcon(hwnd, trayIcon);

    physics::SpringState springState;
    auto lastFrameTime = std::chrono::steady_clock::now();
    auto lastConfigReload = std::chrono::steady_clock::now();
    bool suspended = false;

    bool running = true;
    MSG msg{};
    while (running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running || windowContext.exitRequested) {
            break;
        }

        // Reload config.ini roughly once a second, so a settings window
        // (or hand-editing the file) takes effect live without needing to
        // restart the overlay. Load() is a handful of small file reads --
        // cheap enough to not bother with mtime tracking.
        auto nowForReload = std::chrono::steady_clock::now();
        if (nowForReload - lastConfigReload >= std::chrono::seconds(1)) {
            lastConfigReload = nowForReload;
            settings = config::Load();
            renderer.Configure(settings.blurIntensity, settings.trailLength,
                                settings.ghostScale);

            // Layer 1's shape only needs rebuilding (SetSystemCursor for
            // all 13 roles) when its style/path actually changed -- unlike
            // the renderer knobs above, this isn't cheap enough to redo
            // unconditionally every reload, and doing it while suspended
            // would needlessly show the override right before it gets
            // restored again.
            if (!suspended && (settings.layer1Style != appliedLayer1Style ||
                                settings.layer1Invert != appliedLayer1Invert ||
                                settings.layer1CustomCursorPath != appliedLayer1Path)) {
                cursor_scheme::ApplyOverride(
                    ParseLayer1Style(settings.layer1Style), settings.layer1Invert,
                    Utf8ToWide(settings.layer1CustomCursorPath));
                appliedLayer1Style = settings.layer1Style;
                appliedLayer1Invert = settings.layer1Invert;
                appliedLayer1Path = settings.layer1CustomCursorPath;
            }
        }

        bool shouldSuspend = windowContext.manuallyDisabled ||
                              anticheat_watcher::ShouldSuspend(
                                  hwnd, settings.extraExcludedProcesses);
        if (shouldSuspend != suspended) {
            suspended = shouldSuspend;
            if (suspended) {
                // Hand the real cursor back and disappear entirely rather
                // than just skip-drawing -- an exclude-listed/fullscreen
                // foreground app (or a manually-disabled overlay) should
                // see no trace of it at all.
                cursor_scheme::Restore();
                ShowWindow(hwnd, SW_HIDE);
            } else {
                cursor_scheme::ApplyOverride(
                    ParseLayer1Style(settings.layer1Style), settings.layer1Invert,
                    Utf8ToWide(settings.layer1CustomCursorPath));
                ShowWindow(hwnd, SW_SHOWNOACTIVATE);
                lastFrameTime = std::chrono::steady_clock::now();
            }
        }

        if (suspended) {
            raw_input::ResetFrame(windowContext.rawInput);
            Sleep(200);  // cheap low-frequency poll while suspended
            continue;
        }

        auto now = std::chrono::steady_clock::now();
        float dtSeconds =
            std::chrono::duration<float>(now - lastFrameTime).count();
        lastFrameTime = now;

        POINT cursorPos;
        GetCursorPos(&cursorPos);
        physics::Vec2 frameStartPos = springState.position;

        physics::Vec2 target{
            static_cast<float>(cursorPos.x - bounds.left),
            static_cast<float>(cursorPos.y - bounds.top)};
        physics::UpdateSpring(springState, target, dtSeconds, settings.springSpeed);
        physics::UpdateTilt(springState, dtSeconds, settings.rotationIntensity);

        // Context-specific shape, in priority order: (1) the active-role
        // handle-identity trick, which catches dynamically-set cursors
        // (WM_SETCURSOR) like a standard EDIT control's I-beam -- the case
        // the class-cursor approach below cannot see; (2) the class-cursor
        // fallback, which still catches some cases the role table doesn't
        // cover; (3) the user's own real cursor appearance captured at
        // startup, rather than a generic placeholder.
        cursor_shape_sync::Role activeRole = cursor_shape_sync::DetectActiveRole();
        int roleIdx = static_cast<int>(activeRole);
        bool haveRoleShape = activeRole != cursor_shape_sync::Role::kArrow &&
                              roleCursors.valid[roleIdx];

        Microsoft::WRL::ComPtr<ID2D1Bitmap> classShapeBitmap;
        POINT classHotspot{};
        bool haveClassShape =
            !haveRoleShape && cursor_shape_sync::CaptureCursorAtPoint(
                                   renderer.GetContext(), cursorPos, hwnd,
                                   classShapeBitmap, classHotspot);

        ID2D1Bitmap* ghostShape = nullptr;
        POINT ghostHotspot{};
        if (haveRoleShape) {
            ghostShape = roleCursors.bitmaps[roleIdx].Get();
            ghostHotspot = roleCursors.hotspots[roleIdx];
        } else if (haveClassShape) {
            ghostShape = classShapeBitmap.Get();
            ghostHotspot = classHotspot;
        } else if (haveDefaultCursor) {
            ghostShape = defaultCursorBitmap.Get();
            ghostHotspot = defaultCursorHotspot;
        }

        renderer.RenderGhostAt(
            {springState.position.x, springState.position.y},
            {frameStartPos.x, frameStartPos.y}, springState.tiltAngle,
            ghostShape,
            {static_cast<float>(ghostHotspot.x),
             static_cast<float>(ghostHotspot.y)});

        // Raw deltas aren't consumed yet (velocity/rotation lands with the
        // spring physics milestone) -- reset so eventCount reflects only
        // this frame's activity.
        raw_input::ResetFrame(windowContext.rawInput);
    }

    Shell_NotifyIconW(NIM_DELETE, &trayIcon);
    cursor_scheme::Restore();
    return static_cast<int>(msg.wParam);
}
