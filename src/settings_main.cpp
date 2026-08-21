#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>

#include <cstdio>
#include <string>
#include <vector>

#include "config.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")

namespace {

constexpr int kIdTrackBlur = 101;
constexpr int kIdTrackTrail = 102;
constexpr int kIdTrackScale = 103;
constexpr int kIdTrackRotation = 105;
constexpr int kIdTrackSpeed = 106;
constexpr int kIdEditExclude = 104;
constexpr int kIdComboStyle = 107;
constexpr int kIdEditCustomPath = 108;
constexpr int kIdButtonBrowse = 109;
constexpr int kIdCheckInvert = 110;
constexpr int kIdLabelBlurValue = 201;
constexpr int kIdLabelTrailValue = 202;
constexpr int kIdLabelScaleValue = 203;
constexpr int kIdLabelRotationValue = 204;
constexpr int kIdLabelSpeedValue = 205;

// Trackbar ranges are integer steps; these convert to/from the actual
// float settings values.
constexpr int kBlurTrackMax = 200;      // 0..200 -> 0.00x..2.00x
constexpr int kTrailTrackMax = 100;     // 0..100 trail points
constexpr int kScaleTrackMin = 10;
constexpr int kScaleTrackMax = 400;     // 10..400 -> 0.10x..4.00x
constexpr int kRotationTrackMax = 300;  // 0..300 -> 0.00x..3.00x
constexpr int kSpeedTrackMin = 10;
constexpr int kSpeedTrackMax = 500;     // 10..500 -> 0.10x..5.00x

HBRUSH g_whiteBrush;
HFONT g_uiFont;
HFONT g_uiFontBold;

config::Settings g_settings;

// layer1CustomCursorPath and the exclude list can contain non-ASCII
// characters (a non-ASCII username in a path, for instance), so these get
// a proper UTF-8 round trip rather than a narrowing cast.
std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int size = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string result(static_cast<size_t>(size) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (size <= 0) return L"";
    std::wstring result(static_cast<size_t>(size) - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, result.data(), size);
    return result;
}

std::wstring JoinExcludeList(const config::Settings& s) {
    std::wstring result;
    for (size_t i = 0; i < s.extraExcludedProcesses.size(); ++i) {
        if (i > 0) result += L", ";
        result += Utf8ToWide(s.extraExcludedProcesses[i]);
    }
    return result;
}

std::vector<std::string> SplitExcludeList(const std::wstring& text) {
    std::vector<std::string> result;
    std::wstring current;
    auto flush = [&]() {
        size_t begin = current.find_first_not_of(L" \t");
        size_t end = current.find_last_not_of(L" \t");
        if (begin != std::wstring::npos) {
            std::wstring trimmed = current.substr(begin, end - begin + 1);
            if (!trimmed.empty()) {
                result.push_back(WideToUtf8(trimmed));
            }
        }
        current.clear();
    };
    for (wchar_t c : text) {
        if (c == L',') {
            flush();
        } else {
            current += c;
        }
    }
    flush();
    return result;
}

// Index <-> config string mapping for the style combo box.
//
// Turkish characters throughout this file are written as \u escapes (not
// literal UTF-8 bytes) because MSVC, without an explicit /utf-8 flag or a
// BOM, reinterprets the source file through the local ANSI code page --
// dotless-i/s-cedilla/g-breve/u-umlaut/etc. typed literally would come out
// as mojibake in the compiled wide string. \u is fixed-width (exactly 4
// hex digits) and encoding-independent, unlike \x (which is variable-
// width and can accidentally swallow a following hex-digit character).
// See overlay_window.cpp's tray menu for the same workaround applied
// earlier.
constexpr const wchar_t* kStyleNames[] = {
    L"\u0130nce Kesi\u015Fim", L"Kal\u0131n Kesi\u015Fim (Varsay\u0131lan)",
    L"Nokta", L"\u00D6zel...",
};
constexpr const char* kStyleValues[] = {
    "thin_cross", "thick_cross", "dot", "custom",
};
constexpr int kStyleCount = 4;

int StyleToIndex(const std::string& style) {
    for (int i = 0; i < kStyleCount; ++i) {
        if (style == kStyleValues[i]) return i;
    }
    return 0;
}

void UpdateValueLabel(HWND hwnd, int labelId, const wchar_t* fmt, double value) {
    wchar_t buf[64];
    swprintf_s(buf, fmt, value);
    SetDlgItemTextW(hwnd, labelId, buf);
}

void UpdateStyleControlsEnabled(HWND hwnd) {
    int styleIndex = static_cast<int>(
        SendDlgItemMessageW(hwnd, kIdComboStyle, CB_GETCURSEL, 0, 0));
    bool isCustom = (styleIndex == 3);  // "\u00D6zel..."
    EnableWindow(GetDlgItem(hwnd, kIdEditCustomPath), isCustom);
    EnableWindow(GetDlgItem(hwnd, kIdButtonBrowse), isCustom);
    // Invert only applies to the shapes we draw ourselves -- a custom
    // cursor file's own colors are used as-is (see cursor_scheme.h).
    EnableWindow(GetDlgItem(hwnd, kIdCheckInvert), !isCustom);
}

void SaveFromControls(HWND hwnd) {
    int blurPos = static_cast<int>(SendDlgItemMessageW(hwnd, kIdTrackBlur, TBM_GETPOS, 0, 0));
    int trailPos = static_cast<int>(SendDlgItemMessageW(hwnd, kIdTrackTrail, TBM_GETPOS, 0, 0));
    int scalePos = static_cast<int>(SendDlgItemMessageW(hwnd, kIdTrackScale, TBM_GETPOS, 0, 0));
    int rotationPos = static_cast<int>(SendDlgItemMessageW(hwnd, kIdTrackRotation, TBM_GETPOS, 0, 0));
    int speedPos = static_cast<int>(SendDlgItemMessageW(hwnd, kIdTrackSpeed, TBM_GETPOS, 0, 0));

    g_settings.blurIntensity = blurPos / 100.0f;
    g_settings.trailLength = trailPos;
    g_settings.ghostScale = scalePos / 100.0f;
    g_settings.rotationIntensity = rotationPos / 100.0f;
    g_settings.springSpeed = speedPos / 100.0f;

    int styleIndex = static_cast<int>(SendDlgItemMessageW(hwnd, kIdComboStyle, CB_GETCURSEL, 0, 0));
    if (styleIndex < 0) styleIndex = 0;
    g_settings.layer1Style = kStyleValues[styleIndex];

    g_settings.layer1Invert =
        SendDlgItemMessageW(hwnd, kIdCheckInvert, BM_GETCHECK, 0, 0) == BST_CHECKED;

    wchar_t pathBuf[MAX_PATH];
    GetDlgItemTextW(hwnd, kIdEditCustomPath, pathBuf, MAX_PATH);
    g_settings.layer1CustomCursorPath = WideToUtf8(pathBuf);

    wchar_t excludeBuf[1024];
    GetDlgItemTextW(hwnd, kIdEditExclude, excludeBuf, 1024);
    g_settings.extraExcludedProcesses = SplitExcludeList(excludeBuf);

    config::Save(g_settings);

    UpdateValueLabel(hwnd, kIdLabelBlurValue, L"%.2fx", g_settings.blurIntensity);
    UpdateValueLabel(hwnd, kIdLabelTrailValue, L"%.0f", static_cast<double>(g_settings.trailLength));
    UpdateValueLabel(hwnd, kIdLabelScaleValue, L"%.2fx", g_settings.ghostScale);
    UpdateValueLabel(hwnd, kIdLabelRotationValue, L"%.2fx", g_settings.rotationIntensity);
    UpdateValueLabel(hwnd, kIdLabelSpeedValue, L"%.2fx", g_settings.springSpeed);
    UpdateStyleControlsEnabled(hwnd);
}

void BrowseForCustomCursor(HWND hwnd) {
    wchar_t pathBuf[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Cursor dosyalar\u0131 (*.cur;*.ani;*.ico)\0*.cur;*.ani;*.ico\0T\u00FCm dosyalar\0*.*\0";
    ofn.lpstrFile = pathBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) {
        SetDlgItemTextW(hwnd, kIdEditCustomPath, pathBuf);
        SaveFromControls(hwnd);
    }
}

HWND CreateLabel(HWND parent, HINSTANCE hInst, const wchar_t* text, int x, int y,
                  int w, int h, bool bold = false) {
    HWND label = CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
                                  x, y, w, h, parent, nullptr, hInst, nullptr);
    SendMessageW(label, WM_SETFONT, (WPARAM)(bold ? g_uiFontBold : g_uiFont), TRUE);
    return label;
}

HWND CreateTrack(HWND parent, HINSTANCE hInst, int id, int x, int y, int w,
                  int minVal, int maxVal, int startVal) {
    HWND track = CreateWindowExW(
        0, TRACKBAR_CLASSW, L"", WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_NOTICKS,
        x, y, w, 30, parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        hInst, nullptr);
    SendMessageW(track, TBM_SETRANGE, TRUE, MAKELPARAM(minVal, maxVal));
    SendMessageW(track, TBM_SETPOS, TRUE, startVal);
    return track;
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            HINSTANCE hInst = reinterpret_cast<HINSTANCE>(
                GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));

            constexpr int kLeft = 24;
            constexpr int kWidth = 340;
            constexpr int kValueLabelX = kLeft + kWidth - 50;
            int y = 20;

            auto section = [&](const wchar_t* title, int valueLabelId, int trackId,
                                int trackMin, int trackMax, int trackStart) {
                CreateLabel(hwnd, hInst, title, kLeft, y, 250, 22, true);
                CreateWindowExW(
                    0, L"STATIC", L"", WS_CHILD | WS_VISIBLE, kValueLabelX, y,
                    50, 22, hwnd,
                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(valueLabelId)),
                    hInst, nullptr);
                SendMessageW(GetDlgItem(hwnd, valueLabelId), WM_SETFONT,
                             (WPARAM)g_uiFontBold, TRUE);
                y += 26;
                CreateTrack(hwnd, hInst, trackId, kLeft, y, kWidth, trackMin,
                            trackMax, trackStart);
                y += 46;
            };

            section(L"Blur Yo\u011Funlu\u011Fu", kIdLabelBlurValue, kIdTrackBlur, 0,
                    kBlurTrackMax, static_cast<int>(g_settings.blurIntensity * 100));
            section(L"Trail Uzunlu\u011Fu", kIdLabelTrailValue, kIdTrackTrail, 0,
                    kTrailTrackMax, g_settings.trailLength);
            section(L"Ghost Boyutu", kIdLabelScaleValue, kIdTrackScale,
                    kScaleTrackMin, kScaleTrackMax,
                    static_cast<int>(g_settings.ghostScale * 100));
            section(L"Rotasyon G\u00FCc\u00FC", kIdLabelRotationValue, kIdTrackRotation, 0,
                    kRotationTrackMax,
                    static_cast<int>(g_settings.rotationIntensity * 100));
            section(L"Gelme H\u0131z\u0131 (Snappiness)", kIdLabelSpeedValue, kIdTrackSpeed,
                    kSpeedTrackMin, kSpeedTrackMax,
                    static_cast<int>(g_settings.springSpeed * 100));

            CreateLabel(hwnd, hInst, L"\u00D6n \u0130mle\u00E7 (Layer 1) \u015Eekli", kLeft, y, 250, 22, true);
            y += 26;
            HWND combo = CreateWindowExW(
                0, L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST,
                kLeft, y, kWidth, 200, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdComboStyle)),
                hInst, nullptr);
            SendMessageW(combo, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
            for (const wchar_t* name : kStyleNames) {
                SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)name);
            }
            SendMessageW(combo, CB_SETCURSEL, StyleToIndex(g_settings.layer1Style), 0);
            y += 36;

            HWND invertCheck = CreateWindowExW(
                0, L"BUTTON", L"Ters Renkler (Invert)",
                WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                kLeft, y, kWidth, 22, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdCheckInvert)),
                hInst, nullptr);
            SendMessageW(invertCheck, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
            SendMessageW(invertCheck, BM_SETCHECK,
                         g_settings.layer1Invert ? BST_CHECKED : BST_UNCHECKED, 0);
            y += 32;

            HWND customPathEdit = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT",
                Utf8ToWide(g_settings.layer1CustomCursorPath).c_str(),
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_READONLY,
                kLeft, y, kWidth - 90, 26, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdEditCustomPath)),
                hInst, nullptr);
            SendMessageW(customPathEdit, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
            HWND browseButton = CreateWindowExW(
                0, L"BUTTON", L"G\u00F6zat...", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                kLeft + kWidth - 82, y, 82, 26, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdButtonBrowse)),
                hInst, nullptr);
            SendMessageW(browseButton, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
            y += 44;

            CreateLabel(hwnd, hInst, L"Hari\u00E7 Tutulan \u0130\u015Flemler (virg\u00FClle ayr\u0131l\u0131\u015F)",
                        kLeft, y, kWidth, 22, true);
            y += 26;
            HWND edit = CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", JoinExcludeList(g_settings).c_str(),
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                kLeft, y, kWidth, 26, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdEditExclude)),
                hInst, nullptr);
            SendMessageW(edit, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
            y += 44;

            CreateLabel(hwnd, hInst,
                        L"De\u011Fi\u015Fiklikler yakla\u015F\u0131k 1 saniye i\u00E7inde otomatik uygulan\u0131r.",
                        kLeft, y, kWidth, 40, false);

            UpdateValueLabel(hwnd, kIdLabelBlurValue, L"%.2fx", g_settings.blurIntensity);
            UpdateValueLabel(hwnd, kIdLabelTrailValue, L"%.0f", static_cast<double>(g_settings.trailLength));
            UpdateValueLabel(hwnd, kIdLabelScaleValue, L"%.2fx", g_settings.ghostScale);
            UpdateValueLabel(hwnd, kIdLabelRotationValue, L"%.2fx", g_settings.rotationIntensity);
            UpdateValueLabel(hwnd, kIdLabelSpeedValue, L"%.2fx", g_settings.springSpeed);
            UpdateStyleControlsEnabled(hwnd);
            return 0;
        }
        case WM_HSCROLL:
            SaveFromControls(hwnd);
            return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == kIdButtonBrowse && HIWORD(wParam) == BN_CLICKED) {
                BrowseForCustomCursor(hwnd);
            } else if (LOWORD(wParam) == kIdComboStyle && HIWORD(wParam) == CBN_SELCHANGE) {
                SaveFromControls(hwnd);
            } else if (LOWORD(wParam) == kIdCheckInvert && HIWORD(wParam) == BN_CLICKED) {
                SaveFromControls(hwnd);
            } else if (HIWORD(wParam) == EN_CHANGE && LOWORD(wParam) == kIdEditExclude) {
                SaveFromControls(hwnd);
            }
            return 0;
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORDLG: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(30, 30, 30));
            return reinterpret_cast<LRESULT>(g_whiteBrush);
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);

    g_settings = config::Load();
    g_whiteBrush = CreateSolidBrush(RGB(255, 255, 255));
    g_uiFont = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                            CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    g_uiFontBold = CreateFontW(-16, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_SWISS, L"Segoe UI");

    constexpr wchar_t kClassName[] = L"SmoothCursorOverlaySettingsWindow";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = g_whiteBrush;
    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        0, kClassName, L"Smooth Cursor Overlay \u2014 Ayarlar",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 460, 730, nullptr, nullptr, hInstance,
        nullptr);
    if (!hwnd) {
        return 0;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}
