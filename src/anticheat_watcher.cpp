#include "anticheat_watcher.h"

#include <dwmapi.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cwctype>
#include <string>

namespace anticheat_watcher {
namespace {

// Small built-in default exclude-list of well-known anti-cheat-sensitive
// process names. A real config file (see the M10 milestone) lets users
// extend this without a rebuild; this is just a safe MVP default so the
// watcher does something useful out of the box.
constexpr std::array<const wchar_t*, 8> kExcludedProcessNames = {
    L"csgo.exe",          L"cs2.exe",     L"valorant.exe",
    L"valorant-win64-shipping.exe", L"beamng.drive.exe",
    L"eac_launcher.exe",  L"beservice.exe", L"acs.exe",
};

std::wstring GetForegroundProcessExeName() {
    HWND fg = GetForegroundWindow();
    if (!fg) {
        return L"";
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (pid == 0) {
        return L"";
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        return L"";
    }

    wchar_t path[MAX_PATH];
    DWORD size = MAX_PATH;
    bool ok = QueryFullProcessImageNameW(process, 0, path, &size);
    CloseHandle(process);
    if (!ok) {
        return L"";
    }

    std::wstring fullPath(path);
    size_t slash = fullPath.find_last_of(L"\\/");
    return slash == std::wstring::npos ? fullPath : fullPath.substr(slash + 1);
}

// exeName/excluded lists are always plain ASCII process names, so a
// narrowing cast (rather than full UTF-16 -> UTF-8 conversion) is safe
// here and lets this compare directly against config.h's plain
// std::string exclude-list.
bool IsExcludedName(const std::wstring& exeName,
                     const std::vector<std::string>& extraExcludedNames) {
    std::wstring lowerW = exeName;
    std::transform(lowerW.begin(), lowerW.end(), lowerW.begin(),
                    [](wchar_t c) { return std::towlower(c); });
    std::string lower;
    lower.reserve(lowerW.size());
    for (wchar_t c : lowerW) {
        lower.push_back(static_cast<char>(c));
    }

    for (const wchar_t* excludedW : kExcludedProcessNames) {
        std::wstring excludedWStr(excludedW);
        if (lowerW == excludedWStr) {
            return true;
        }
    }
    for (const std::string& excluded : extraExcludedNames) {
        if (lower == excluded) {
            return true;
        }
    }
    return false;
}

bool IsDesktopShellWindow(HWND hwnd) {
    // Explorer's desktop/wallpaper windows ("Progman", plus one or more
    // "WorkerW" siblings it creates alongside it) are WS_POPUP, cover the
    // entire monitor, and have no WS_CAPTION/WS_THICKFRAME -- i.e. they
    // look exactly like exclusive-fullscreen borderless windows by the
    // same heuristic. GetForegroundWindow() returns one of these whenever
    // the user's focus is simply "the desktop" (clicked empty desktop,
    // Win+D, nothing else focused), which is common, so without this
    // exclusion the overlay would wrongly suspend itself constantly.
    wchar_t className[64];
    if (!GetClassNameW(hwnd, className, 64)) {
        return false;
    }
    return wcscmp(className, L"Progman") == 0 ||
           wcscmp(className, L"WorkerW") == 0;
}

// DWM-cloaked windows exist but aren't actually being shown to the user
// (minimized-to-tray UWP apps, windows parked on an inactive virtual
// desktop, and -- found during testing -- some dev tools' own hidden
// full-screen automation/helper windows, which are exactly the kind of
// borderless-and-monitor-sized window this heuristic is trying to spot in
// the first place). GetForegroundWindow() can still return one of these
// in some setups, so treat a cloaked window as never "the app the user is
// looking at" -- a more general fix than excluding specific class names
// one at a time (see IsDesktopShellWindow above for the first such case).
bool IsCloaked(HWND hwnd) {
    BOOL cloaked = FALSE;
    HRESULT hr = DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked,
                                        sizeof(cloaked));
    return SUCCEEDED(hr) && cloaked;
}

bool IsLikelyExclusiveFullscreen(HWND fgHwnd, HWND overlayHwnd) {
    if (!fgHwnd || fgHwnd == overlayHwnd || IsDesktopShellWindow(fgHwnd) ||
        IsCloaked(fgHwnd)) {
        return false;
    }

    RECT windowRect;
    if (!GetWindowRect(fgHwnd, &windowRect)) {
        return false;
    }

    HMONITOR monitor = MonitorFromWindow(fgHwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(monitor, &mi)) {
        return false;
    }

    bool coversMonitor = windowRect.left <= mi.rcMonitor.left &&
                          windowRect.top <= mi.rcMonitor.top &&
                          windowRect.right >= mi.rcMonitor.right &&
                          windowRect.bottom >= mi.rcMonitor.bottom;
    if (!coversMonitor) {
        return false;
    }

    LONG_PTR style = GetWindowLongPtrW(fgHwnd, GWL_STYLE);
    bool hasChrome = (style & (WS_CAPTION | WS_THICKFRAME)) != 0;
    return !hasChrome;
}

}  // namespace

bool ShouldSuspend(HWND overlayHwnd,
                    const std::vector<std::string>& extraExcludedNames) {
    HWND fg = GetForegroundWindow();
    if (!fg || fg == overlayHwnd) {
        return false;
    }

    std::wstring exeName = GetForegroundProcessExeName();
    if (!exeName.empty() && IsExcludedName(exeName, extraExcludedNames)) {
        return true;
    }

    return IsLikelyExclusiveFullscreen(fg, overlayHwnd);
}

}  // namespace anticheat_watcher
