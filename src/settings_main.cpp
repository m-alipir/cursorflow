#include <windows.h>
#include <commdlg.h>

// Two things GDI+ needs that this project's compile flags otherwise take
// away: <objidl.h> for IStream/PROPID (WIN32_LEAN_AND_MEAN drops them), and
// std's min/max (NOMINMAX drops the macros GDI+ headers call unqualified).
// Both are the conventional workarounds; neither weakens the flags anywhere
// else in the codebase.
#include <objidl.h>

#include <algorithm>
using std::max;
using std::min;
#include <gdiplus.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "config.h"

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "comdlg32.lib")

namespace {

using namespace Gdiplus;

// ---------------------------------------------------------------------------
// Design tokens -- these mirror the Claude Design mockup one-to-one so the
// shipped window and the design canvas stay recognizably the same thing.
// ---------------------------------------------------------------------------
constexpr COLORREF kColBg = RGB(0xff, 0xff, 0xff);
constexpr COLORREF kColText = RGB(0x1a, 0x1d, 0x21);
constexpr COLORREF kColMuted = RGB(0x8a, 0x90, 0x99);
constexpr COLORREF kColHelp = RGB(0x9a, 0xa0, 0xa8);
constexpr COLORREF kColDisabled = RGB(0xb2, 0xb7, 0xbd);

const Color kAccent(255, 0x2b, 0x6e, 0xf2);
const Color kTrackEmpty(255, 0xe6, 0xe9, 0xed);
const Color kBorder(255, 0xdd, 0xe1, 0xe6);
const Color kHairline(255, 0xec, 0xee, 0xf1);
const Color kPreviewBg(255, 0xfb, 0xfc, 0xfe);
const Color kFieldDisabledBg(255, 0xf4, 0xf5, 0xf7);
const Color kResetIdle(255, 0x9a, 0xa0, 0xa8);
const Color kResetHover(255, 0x5a, 0x61, 0x6b);
const Color kHoverWash(255, 0xee, 0xf1, 0xf4);
const Color kWhite(255, 255, 255, 255);
const Color kThumbRing(31, 0x14, 0x18, 0x1f);

constexpr int kPadX = 24;
constexpr int kContentW = 392;
constexpr int kWindowW = kPadX * 2 + kContentW;
constexpr int kPreviewH = 110;
constexpr int kFieldH = 34;
constexpr int kResetSize = 20;

// ---------------------------------------------------------------------------
// Sliders. Ranges and defaults mirror config.h's Settings so the reset
// button always restores the value the overlay itself falls back to.
// ---------------------------------------------------------------------------
enum SliderId { kSlBlur, kSlTrail, kSlGhost, kSlRotation, kSlSpeed, kSliderCount };

struct SliderSpec {
    const wchar_t* label;
    float minV;
    float maxV;
    float defV;
    bool integral;
};

const SliderSpec kSliders[kSliderCount] = {
    {L"Blur Intensity", 0.0f, 2.0f, 1.0f, false},
    {L"Trail Length", 0.0f, 100.0f, 24.0f, true},
    {L"Ghost Size", 0.1f, 4.0f, 1.0f, false},
    {L"Rotation Power", 0.0f, 3.0f, 1.0f, false},
    {L"Follow Speed (Snappiness)", 0.1f, 5.0f, 1.0f, false},
};

const wchar_t* kStyleNames[] = {
    L"Thin Cross",
    L"Thick Cross (Default)",
    L"Dot",
    L"Custom...",
};
const char* kStyleValues[] = {"thin_cross", "thick_cross", "dot", "custom"};
constexpr int kStyleCount = 4;
constexpr int kStyleCustom = 3;

// ---------------------------------------------------------------------------
// Layout: computed once, then shared by painting and hit-testing so the two
// can never disagree about where a control is.
// ---------------------------------------------------------------------------
struct Layout {
    RECT title, subtitle;
    RECT preview;
    RECT slLabel[kSliderCount];
    RECT slValue[kSliderCount];
    RECT slReset[kSliderCount];
    RECT slTrack[kSliderCount];
    RECT divider;
    RECT shapeLabel, shapeField;
    RECT invertBox, invertLabel;
    RECT startupBox, startupLabel;
    RECT customLabel, customHelp, customField, browseBtn, reloadBtn;
    RECT excludeLabel, excludeHelp, excludeField;
    RECT footer;
    int totalH;
};

RECT MakeRect(int x, int y, int w, int h) {
    RECT r{x, y, x + w, y + h};
    return r;
}

Layout ComputeLayout() {
    Layout L{};
    const int x = kPadX;
    int y = 20;

    L.title = MakeRect(x, y, kContentW, 22);
    y += 22;
    L.subtitle = MakeRect(x, y, kContentW, 18);
    y += 26;

    L.preview = MakeRect(x, y, kContentW, kPreviewH);
    y += kPreviewH + 18;

    for (int i = 0; i < kSliderCount; ++i) {
        L.slLabel[i] = MakeRect(x, y, 230, 20);
        L.slReset[i] = MakeRect(x + kContentW - kResetSize, y, kResetSize, kResetSize);
        L.slValue[i] = MakeRect(x + kContentW - kResetSize - 96, y, 90, 20);
        y += 24;
        L.slTrack[i] = MakeRect(x, y, kContentW, 18);
        y += 18 + 12;
    }

    y += 4;
    L.divider = MakeRect(x, y, kContentW, 1);
    y += 18;

    L.shapeLabel = MakeRect(x, y, kContentW, 20);
    y += 22;
    L.shapeField = MakeRect(x, y, kContentW, kFieldH);
    y += kFieldH + 14;

    L.invertBox = MakeRect(x, y + 2, 16, 16);
    L.invertLabel = MakeRect(x + 26, y, kContentW - 26, 20);
    y += 20 + 14;

    L.startupBox = MakeRect(x, y + 2, 16, 16);
    L.startupLabel = MakeRect(x + 26, y, kContentW - 26, 20);
    y += 20 + 16;

    L.customLabel = MakeRect(x, y, kContentW, 20);
    y += 21;
    L.customHelp = MakeRect(x, y, kContentW, 16);
    y += 20;
    constexpr int kCustomBtnW = 74;
    L.customField = MakeRect(x, y, kContentW - (kCustomBtnW * 2 + 16), kFieldH);
    L.reloadBtn = MakeRect(x + kContentW - (kCustomBtnW * 2 + 8), y, kCustomBtnW, kFieldH);
    L.browseBtn = MakeRect(x + kContentW - kCustomBtnW, y, kCustomBtnW, kFieldH);
    y += kFieldH + 16;

    L.excludeLabel = MakeRect(x, y, kContentW, 20);
    y += 21;
    L.excludeHelp = MakeRect(x, y, kContentW, 48);
    y += 52;
    L.excludeField = MakeRect(x, y, kContentW, kFieldH);
    y += kFieldH + 16;

    L.footer = MakeRect(x, y, kContentW, 18);
    y += 18 + 20;

    L.totalH = y;
    return L;
}

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
config::Settings g_settings;
Layout g_layout;
float g_sliderValues[kSliderCount];
int g_styleIndex = 1;

HWND g_excludeEdit = nullptr;
HBRUSH g_whiteBrush = nullptr;
HFONT g_fontTitle = nullptr;
HFONT g_fontSub = nullptr;
HFONT g_fontLabel = nullptr;
HFONT g_fontField = nullptr;
HFONT g_fontHelp = nullptr;

int g_dragSlider = -1;
int g_hoverReset = -1;
bool g_hoverBrowse = false;
bool g_hoverReload = false;
bool g_dirty = false;
bool g_mouseTracking = false;
bool g_runAtStartup = false;

constexpr UINT_PTR kTimerAnim = 1;
constexpr UINT_PTR kTimerSave = 2;

// Live-preview animation state, mirroring the design canvas's ghost-trail
// simulation: a target tracing a Lissajous path, a spring-lagged ghost
// chasing it, and a fading history of past ghost positions.
struct PreviewState {
    float ghostX = 196.0f;
    float ghostY = 55.0f;
    std::vector<PointF> history;
    ULONGLONG start = 0;
    float targetX = 196.0f;
    float targetY = 55.0f;
};
PreviewState g_preview;

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
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
            if (!trimmed.empty()) result.push_back(WideToUtf8(trimmed));
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

int StyleToIndex(const std::string& style) {
    for (int i = 0; i < kStyleCount; ++i) {
        if (style == kStyleValues[i]) return i;
    }
    return 1;  // thick_cross
}

constexpr wchar_t kRunKeyPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValueName[] = L"CursorFlow";

// The registry Run key is the actual persistent state here -- there's
// nothing to keep in config.ini, since Windows itself is the thing that
// reads this at login. Load queries it once at startup; Set updates it
// immediately when the checkbox is toggled, same as every other control.
bool QueryRunAtStartup() {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_QUERY_VALUE, &key) !=
        ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0;
    LONG result = RegQueryValueExW(key, kRunValueName, nullptr, &type, nullptr, nullptr);
    RegCloseKey(key);
    return result == ERROR_SUCCESS && type == REG_SZ;
}

void SetRunAtStartup(bool enabled) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_SET_VALUE, &key) !=
        ERROR_SUCCESS) {
        return;
    }
    if (enabled) {
        // Point at the overlay itself (CursorFlow.exe), not this
        // settings process -- it's expected to sit next to this exe, same
        // assumption overlay_window.cpp's LaunchSettingsWindow makes in
        // the other direction.
        wchar_t selfPath[MAX_PATH];
        GetModuleFileNameW(nullptr, selfPath, MAX_PATH);
        std::wstring path(selfPath);
        size_t slash = path.find_last_of(L"\\/");
        std::wstring dir = slash == std::wstring::npos ? L"." : path.substr(0, slash);
        std::wstring value = L"\"" + dir + L"\\CursorFlow.exe\"";
        RegSetValueExW(key, kRunValueName, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(value.c_str()),
                       static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    } else {
        RegDeleteValueW(key, kRunValueName);
    }
    RegCloseKey(key);
}

bool PtIn(const RECT& r, POINT p) { return PtInRect(&r, p) != 0; }

// Signed extraction of mouse coords from an LPARAM. During a captured drag
// the pointer can leave the window, making these negative -- a plain
// LOWORD/HIWORD would wrap those to ~65000 and fling the slider.
int MouseX(LPARAM lp) { return static_cast<int>(static_cast<short>(LOWORD(lp))); }
int MouseY(LPARAM lp) { return static_cast<int>(static_cast<short>(HIWORD(lp))); }

RectF ToRectF(const RECT& r) {
    return RectF(static_cast<float>(r.left), static_cast<float>(r.top),
                 static_cast<float>(r.right - r.left),
                 static_cast<float>(r.bottom - r.top));
}

void MakeRoundPath(GraphicsPath& path, RectF r, float radius) {
    float d = radius * 2.0f;
    path.Reset();
    if (radius <= 0.01f) {
        path.AddRectangle(r);
        return;
    }
    path.AddArc(r.X, r.Y, d, d, 180.0f, 90.0f);
    path.AddArc(r.GetRight() - d, r.Y, d, d, 270.0f, 90.0f);
    path.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0.0f, 90.0f);
    path.AddArc(r.X, r.GetBottom() - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

void FillRound(Graphics& g, RectF r, float radius, const Color& c) {
    GraphicsPath p;
    MakeRoundPath(p, r, radius);
    SolidBrush b(c);
    g.FillPath(&b, &p);
}

void StrokeRound(Graphics& g, RectF r, float radius, const Color& c, float width) {
    GraphicsPath p;
    // Inset by half the stroke so the outline lands inside the rect.
    RectF inset(r.X + width / 2, r.Y + width / 2, r.Width - width, r.Height - width);
    MakeRoundPath(p, inset, radius);
    Pen pen(c, width);
    g.DrawPath(&pen, &p);
}

// The circular "reset to default" arrow, drawn as vectors rather than a font
// glyph so it can't fall back to a different-looking character.
void DrawResetIcon(Graphics& g, const RECT& box, const Color& c) {
    float cx = (box.left + box.right) / 2.0f;
    float cy = (box.top + box.bottom) / 2.0f;
    const float r = 4.7f;

    const float startDeg = 20.0f;

    Pen pen(c, 1.7f);
    pen.SetStartCap(LineCapRound);
    pen.SetEndCap(LineCapRound);
    g.DrawArc(&pen, cx - r, cy - r, r * 2, r * 2, startDeg, 300.0f);

    // Arrowhead on the arc's open end, aimed along the tangent so it reads
    // as "wind back", not as a stray dot stuck to the circle. GDI+ sweeps
    // clockwise from 3 o'clock, so the backwards tangent at the start angle
    // is (sin, -cos) and the radial normal is (cos, sin).
    const float a = startDeg * 3.14159265f / 180.0f;
    const float ca = cosf(a), sa = sinf(a);
    const float px = cx + ca * r, py = cy + sa * r;
    const float tx = sa, ty = -ca;   // tangent, pointing backwards along the arc
    const float nx = ca, ny = sa;    // radial normal

    PointF head[3] = {
        PointF(px + tx * 4.6f, py + ty * 4.6f),
        PointF(px - tx * 1.2f + nx * 3.1f, py - ty * 1.2f + ny * 3.1f),
        PointF(px - tx * 1.2f - nx * 3.1f, py - ty * 1.2f - ny * 3.1f),
    };
    SolidBrush br(c);
    g.FillPolygon(&br, head, 3);
}

void DrawCheckbox(Graphics& g, const RECT& box, bool checked, bool disabled) {
    RectF r = ToRectF(box);
    if (disabled) {
        FillRound(g, r, 4.0f, kFieldDisabledBg);
        StrokeRound(g, r, 4.0f, kBorder, 1.0f);
    } else if (checked) {
        FillRound(g, r, 4.0f, kAccent);
        Pen pen(kWhite, 2.0f);
        pen.SetStartCap(LineCapRound);
        pen.SetEndCap(LineCapRound);
        g.DrawLine(&pen, r.X + 3.5f, r.Y + 8.0f, r.X + 6.5f, r.Y + 11.5f);
        g.DrawLine(&pen, r.X + 6.5f, r.Y + 11.5f, r.X + 12.5f, r.Y + 4.5f);
    } else {
        FillRound(g, r, 4.0f, kWhite);
        StrokeRound(g, r, 4.0f, kBorder, 1.5f);
    }
}

void DrawTextLine(HDC dc, const RECT& r, const wchar_t* text, HFONT font,
                   COLORREF color, UINT format) {
    HFONT old = static_cast<HFONT>(SelectObject(dc, font));
    SetTextColor(dc, color);
    SetBkMode(dc, TRANSPARENT);
    RECT rc = r;
    DrawTextW(dc, text, -1, &rc, format);
    SelectObject(dc, old);
}

// ---------------------------------------------------------------------------
// Values <-> settings
// ---------------------------------------------------------------------------
float SliderFraction(int i) {
    const SliderSpec& s = kSliders[i];
    float f = (g_sliderValues[i] - s.minV) / (s.maxV - s.minV);
    return f < 0.0f ? 0.0f : (f > 1.0f ? 1.0f : f);
}

void FormatSliderValue(int i, wchar_t* buf, size_t count) {
    if (kSliders[i].integral) {
        swprintf_s(buf, count, L"%d points", static_cast<int>(g_sliderValues[i] + 0.5f));
    } else {
        swprintf_s(buf, count, L"%.2fx", g_sliderValues[i]);
    }
}

void PullFromSettings() {
    g_sliderValues[kSlBlur] = g_settings.blurIntensity;
    g_sliderValues[kSlTrail] = static_cast<float>(g_settings.trailLength);
    g_sliderValues[kSlGhost] = g_settings.ghostScale;
    g_sliderValues[kSlRotation] = g_settings.rotationIntensity;
    g_sliderValues[kSlSpeed] = g_settings.springSpeed;
    g_styleIndex = StyleToIndex(g_settings.layer1Style);
}

void PushToSettings(HWND hwnd) {
    g_settings.blurIntensity = g_sliderValues[kSlBlur];
    g_settings.trailLength = static_cast<int>(g_sliderValues[kSlTrail] + 0.5f);
    g_settings.ghostScale = g_sliderValues[kSlGhost];
    g_settings.rotationIntensity = g_sliderValues[kSlRotation];
    g_settings.springSpeed = g_sliderValues[kSlSpeed];
    g_settings.layer1Style = kStyleValues[g_styleIndex];

    if (g_excludeEdit) {
        wchar_t buf[1024];
        GetWindowTextW(g_excludeEdit, buf, 1024);
        g_settings.extraExcludedProcesses = SplitExcludeList(buf);
    }
    (void)hwnd;
}

void SaveNow(HWND hwnd) {
    PushToSettings(hwnd);
    config::Save(g_settings);
    g_dirty = false;
}

// Slider drags fire many times a second; writing config.ini on every one of
// them would be pointless churn (the overlay only re-reads it about once a
// second anyway), so drags just mark the state dirty and a timer flushes it.
void MarkDirty() { g_dirty = true; }

// ---------------------------------------------------------------------------
// Live preview
// ---------------------------------------------------------------------------
void StepPreview() {
    ULONGLONG now = GetTickCount64();
    if (g_preview.start == 0) g_preview.start = now;
    float t = (now - g_preview.start) / 1000.0f;

    const float cx = kContentW / 2.0f, cy = kPreviewH / 2.0f;
    const float rx = 120.0f, ry = 30.0f;
    float angle = t * 1.1f;
    g_preview.targetX = cx + cosf(angle) * rx;
    g_preview.targetY = cy + sinf(angle * 1.7f) * ry;

    float lag = 0.02f + g_sliderValues[kSlSpeed] * 0.16f;
    if (lag > 0.92f) lag = 0.92f;
    if (lag < 0.02f) lag = 0.02f;
    g_preview.ghostX += (g_preview.targetX - g_preview.ghostX) * lag;
    g_preview.ghostY += (g_preview.targetY - g_preview.ghostY) * lag;

    g_preview.history.insert(g_preview.history.begin(),
                              PointF(g_preview.ghostX, g_preview.ghostY));
    size_t maxHist = static_cast<size_t>(g_sliderValues[kSlTrail] + 0.5f);
    if (maxHist < 2) maxHist = 2;
    if (g_preview.history.size() > maxHist) g_preview.history.resize(maxHist);
}

void PaintPreview(Graphics& g, HDC dc, const RECT& box) {
    RectF frame = ToRectF(box);
    FillRound(g, frame, 10.0f, kPreviewBg);
    StrokeRound(g, frame, 10.0f, kHairline, 1.0f);

    GraphicsPath clipPath;
    MakeRoundPath(clipPath, frame, 10.0f);
    g.SetClip(&clipPath);

    const float ox = static_cast<float>(box.left);
    const float oy = static_cast<float>(box.top);

    const int n = static_cast<int>(g_preview.history.size());
    const float ghostScale = g_sliderValues[kSlGhost];
    const float blur = g_sliderValues[kSlBlur];
    const float rot = g_sliderValues[kSlRotation];

    for (int i = n - 1; i >= 0; --i) {
        const PointF& p = g_preview.history[i];
        const PointF& nxt = g_preview.history[i > 0 ? i - 1 : i];
        float fade = 1.0f - static_cast<float>(i) / n;

        // Blur is approximated by spreading the tail wider and thinner as it
        // recedes -- GDI+ has no cheap per-shape gaussian, and the visual
        // read (a softening smear) is what matters here.
        float alpha = fade * 0.65f * (1.0f - 0.25f * blur * (static_cast<float>(i) / n));
        if (alpha < 0.0f) alpha = 0.0f;
        float size = ghostScale * 4.5f * (1.0f - static_cast<float>(i) / (n * 1.4f));
        size *= 1.0f + blur * 0.4f * (static_cast<float>(i) / n);
        if (size < 1.5f) size = 1.5f;

        float dx = nxt.X - p.X, dy = nxt.Y - p.Y;
        float dirDeg = atan2f(dy, dx) * (180.0f / 3.14159265f) * rot * 0.4f;

        Color c(static_cast<BYTE>(alpha * 255), 0x2b, 0x6e, 0xf2);
        SolidBrush br(c);

        GraphicsState st = g.Save();
        g.TranslateTransform(ox + p.X, oy + p.Y);
        g.RotateTransform(dirDeg);
        g.FillEllipse(&br, -size, -size * 0.62f, size * 2, size * 1.24f);
        g.Restore(st);
    }

    g.ResetClip();
    g.Flush(FlushIntentionSync);

    // The front cursor is drawn with GDI region ops rather than GDI+ so the
    // "Invert Colors" mode can use a true pixel inversion (InvertRgn), the
    // same effect the real Layer 1 cursor gets from its AND/XOR masks --
    // most visible where it crosses the blue trail.
    int fx = static_cast<int>(ox + g_preview.targetX + 0.5f);
    int fy = static_cast<int>(oy + g_preview.targetY + 0.5f);

    HRGN shape = nullptr;
    if (g_styleIndex == 0) {  // thin cross
        HRGN h = CreateRectRgn(fx - 9, fy, fx + 10, fy + 1);
        HRGN v = CreateRectRgn(fx, fy - 9, fx + 1, fy + 10);
        CombineRgn(h, h, v, RGN_OR);
        DeleteObject(v);
        shape = h;
    } else if (g_styleIndex == 2) {  // dot
        shape = CreateEllipticRgn(fx - 5, fy - 5, fx + 6, fy + 6);
    } else if (g_styleIndex == kStyleCustom) {
        shape = nullptr;  // drawn as a dashed placeholder below
    } else {  // thick cross
        HRGN h = CreateRectRgn(fx - 10, fy - 1, fx + 11, fy + 2);
        HRGN v = CreateRectRgn(fx - 1, fy - 10, fx + 2, fy + 11);
        CombineRgn(h, h, v, RGN_OR);
        DeleteObject(v);
        shape = h;
    }

    HRGN clip = CreateRoundRectRgn(box.left, box.top, box.right + 1, box.bottom + 1, 20, 20);
    if (shape) {
        CombineRgn(shape, shape, clip, RGN_AND);
        if (g_settings.layer1Invert) {
            InvertRgn(dc, shape);
        } else {
            HBRUSH br = CreateSolidBrush(kColText);
            FillRgn(dc, shape, br);
            DeleteObject(br);
        }
        DeleteObject(shape);
    } else {
        HRGN oldClip = clip;
        SelectClipRgn(dc, oldClip);
        HPEN pen = CreatePen(PS_DOT, 1, RGB(0x9a, 0xa0, 0xa8));
        HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
        HBRUSH oldBr = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
        Rectangle(dc, fx - 8, fy - 8, fx + 9, fy + 9);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBr);
        DeleteObject(pen);
        SelectClipRgn(dc, nullptr);
    }
    DeleteObject(clip);
}

// ---------------------------------------------------------------------------
// Painting
// ---------------------------------------------------------------------------
void PaintWindow(HWND hwnd, HDC target) {
    RECT client;
    GetClientRect(hwnd, &client);
    int w = client.right, h = client.bottom;

    HDC dc = CreateCompatibleDC(target);
    HBITMAP bmp = CreateCompatibleBitmap(target, w, h);
    HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(dc, bmp));

    FillRect(dc, &client, g_whiteBrush);

    const bool isCustom = (g_styleIndex == kStyleCustom);

    {
        Graphics g(dc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);

        PaintPreview(g, dc, g_layout.preview);
    }

    {
        Graphics g(dc);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(PixelOffsetModeHalf);

        // Sliders
        for (int i = 0; i < kSliderCount; ++i) {
            const RECT& tr = g_layout.slTrack[i];
            float cy = (tr.top + tr.bottom) / 2.0f;
            float x0 = static_cast<float>(tr.left);
            float fullW = static_cast<float>(tr.right - tr.left);
            const float thumbR = 7.5f;
            float usable = fullW - thumbR * 2;
            float frac = SliderFraction(i);
            float thumbX = x0 + thumbR + usable * frac;

            FillRound(g, RectF(x0, cy - 2.0f, fullW, 4.0f), 2.0f, kTrackEmpty);
            if (thumbX - x0 > 0.5f) {
                FillRound(g, RectF(x0, cy - 2.0f, thumbX - x0, 4.0f), 2.0f, kAccent);
            }

            SolidBrush ring(kThumbRing), white(kWhite), accent(kAccent);
            g.FillEllipse(&ring, thumbX - 8.5f, cy - 8.5f, 17.0f, 17.0f);
            g.FillEllipse(&white, thumbX - 7.5f, cy - 7.5f, 15.0f, 15.0f);
            g.FillEllipse(&accent, thumbX - 5.5f, cy - 5.5f, 11.0f, 11.0f);

            if (g_hoverReset == i) {
                FillRound(g, ToRectF(g_layout.slReset[i]), 5.0f, kHoverWash);
            }
            DrawResetIcon(g, g_layout.slReset[i],
                          g_hoverReset == i ? kResetHover : kResetIdle);
        }

        // Divider
        {
            SolidBrush b(kHairline);
            g.FillRectangle(&b, ToRectF(g_layout.divider));
        }

        // Shape field (a custom "select" -- clicking it opens a popup menu)
        FillRound(g, ToRectF(g_layout.shapeField), 8.0f, kWhite);
        StrokeRound(g, ToRectF(g_layout.shapeField), 8.0f, kBorder, 1.0f);
        {
            const RECT& f = g_layout.shapeField;
            float cx = static_cast<float>(f.right - 18);
            float cy = static_cast<float>((f.top + f.bottom) / 2);
            Pen pen(Color(255, 0x6b, 0x72, 0x7b), 1.6f);
            pen.SetStartCap(LineCapRound);
            pen.SetEndCap(LineCapRound);
            g.DrawLine(&pen, cx - 4.0f, cy - 2.0f, cx, cy + 2.5f);
            g.DrawLine(&pen, cx, cy + 2.5f, cx + 4.0f, cy - 2.0f);
        }

        // Invert and run-at-startup checkboxes
        DrawCheckbox(g, g_layout.invertBox, g_settings.layer1Invert, isCustom);
        DrawCheckbox(g, g_layout.startupBox, g_runAtStartup, false);

        // Custom cursor path field + browse button
        FillRound(g, ToRectF(g_layout.customField), 8.0f,
                  isCustom ? kWhite : kFieldDisabledBg);
        StrokeRound(g, ToRectF(g_layout.customField), 8.0f, kBorder, 1.0f);

        Color browseBg = isCustom ? (g_hoverBrowse ? Color(255, 0x1f, 0x5c, 0xd8) : kAccent)
                                   : kTrackEmpty;
        FillRound(g, ToRectF(g_layout.browseBtn), 8.0f, browseBg);

        // Reload always stays enabled, regardless of style -- it's a
        // recovery button for when Layer 1 gets stuck (see its comment at
        // the click handler), so it should work even if the underlying
        // apply silently failed.
        FillRound(g, ToRectF(g_layout.reloadBtn), 8.0f,
                  g_hoverReload ? kHoverWash : kWhite);
        StrokeRound(g, ToRectF(g_layout.reloadBtn), 8.0f, kBorder, 1.0f);

        // Excluded-processes field (a real EDIT child sits inside this frame)
        FillRound(g, ToRectF(g_layout.excludeField), 8.0f, kWhite);
        StrokeRound(g, ToRectF(g_layout.excludeField), 8.0f, kBorder, 1.0f);
    }

    // ---- Text (always on top of the shapes above) ----
    DrawTextLine(dc, g_layout.title, L"CursorFlow", g_fontTitle, kColText,
                 DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextLine(dc, g_layout.subtitle, L"Settings", g_fontSub, kColMuted,
                 DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    for (int i = 0; i < kSliderCount; ++i) {
        DrawTextLine(dc, g_layout.slLabel[i], kSliders[i].label, g_fontLabel, kColText,
                     DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        wchar_t buf[64];
        FormatSliderValue(i, buf, 64);
        DrawTextLine(dc, g_layout.slValue[i], buf, g_fontLabel, RGB(0x2b, 0x6e, 0xf2),
                     DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
    }

    DrawTextLine(dc, g_layout.shapeLabel, L"Front Cursor (Layer 1) Shape", g_fontLabel,
                 kColText, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    {
        RECT r = g_layout.shapeField;
        r.left += 12;
        r.right -= 30;
        DrawTextLine(dc, r, kStyleNames[g_styleIndex], g_fontField, kColText,
                     DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    }

    DrawTextLine(dc, g_layout.invertLabel, L"Invert Colors", g_fontField,
                 isCustom ? kColDisabled : kColText,
                 DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    DrawTextLine(dc, g_layout.startupLabel, L"Run at Windows Startup", g_fontField,
                 kColText, DT_LEFT | DT_SINGLELINE | DT_VCENTER);

    DrawTextLine(dc, g_layout.customLabel, L"Custom Cursor File", g_fontLabel,
                 isCustom ? kColText : kColDisabled,
                 DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextLine(dc, g_layout.customHelp,
                 L".cur, .ani, or .ico \u2014 ideally 32\u00D732px (up to 256\u00D7256)",
                 g_fontHelp, kColHelp, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    {
        RECT r = g_layout.customField;
        r.left += 12;
        r.right -= 12;
        std::wstring path = Utf8ToWide(g_settings.layer1CustomCursorPath);
        const wchar_t* shown = path.empty() ? L"No file selected" : path.c_str();
        COLORREF col = !isCustom ? kColDisabled : (path.empty() ? kColHelp : kColText);
        DrawTextLine(dc, r, shown, g_fontField, col,
                     DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_PATH_ELLIPSIS);
    }
    DrawTextLine(dc, g_layout.browseBtn, L"Browse...", g_fontLabel,
                 isCustom ? RGB(255, 255, 255) : kColDisabled,
                 DT_CENTER | DT_SINGLELINE | DT_VCENTER);
    DrawTextLine(dc, g_layout.reloadBtn, L"Reload", g_fontLabel, kColText,
                 DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    DrawTextLine(dc, g_layout.excludeLabel, L"Excluded Processes", g_fontLabel,
                 kColText, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    DrawTextLine(dc, g_layout.excludeHelp,
                 L"The overlay automatically disables itself while these "
                 L"programs (.exe) are in the foreground \u2014 e.g. fullscreen "
                 L"games or anti-cheat-protected apps. Separate with commas.",
                 g_fontHelp, kColHelp, DT_LEFT | DT_WORDBREAK);

    DrawTextLine(dc, g_layout.footer,
                 L"Changes apply automatically within about a second.",
                 g_fontHelp, kColHelp, DT_CENTER | DT_SINGLELINE | DT_VCENTER);

    BitBlt(target, 0, 0, w, h, dc, 0, 0, SRCCOPY);

    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    DeleteDC(dc);
}

// ---------------------------------------------------------------------------
// Interaction
// ---------------------------------------------------------------------------
RECT TrackHitRect(int i) {
    RECT r = g_layout.slTrack[i];
    InflateRect(&r, 0, 4);
    return r;
}

void SetSliderFromX(int i, int mouseX) {
    const RECT& tr = g_layout.slTrack[i];
    const float thumbR = 7.5f;
    float usable = (tr.right - tr.left) - thumbR * 2;
    if (usable <= 1.0f) return;
    float frac = (mouseX - (tr.left + thumbR)) / usable;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;

    const SliderSpec& s = kSliders[i];
    float v = s.minV + frac * (s.maxV - s.minV);
    if (s.integral) v = floorf(v + 0.5f);
    g_sliderValues[i] = v;
}

void OpenShapeMenu(HWND hwnd) {
    HMENU menu = CreatePopupMenu();
    for (int i = 0; i < kStyleCount; ++i) {
        AppendMenuW(menu, MF_STRING | (i == g_styleIndex ? MF_CHECKED : 0),
                    static_cast<UINT_PTR>(1000 + i), kStyleNames[i]);
    }
    POINT pt{g_layout.shapeField.left, g_layout.shapeField.bottom + 2};
    ClientToScreen(hwnd, &pt);
    int chosen = TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD,
                                 pt.x, pt.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    if (chosen >= 1000 && chosen < 1000 + kStyleCount) {
        g_styleIndex = chosen - 1000;
        SaveNow(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

void BrowseForCustomCursor(HWND hwnd) {
    wchar_t pathBuf[MAX_PATH] = L"";
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"Cursor files (*.cur;*.ani;*.ico)\0*.cur;*.ani;*.ico\0All files\0*.*\0";
    ofn.lpstrFile = pathBuf;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) {
        g_settings.layer1CustomCursorPath = WideToUtf8(pathBuf);
        SaveNow(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
    }
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        case WM_CREATE: {
            HINSTANCE hInst = reinterpret_cast<HINSTANCE>(
                GetWindowLongPtrW(hwnd, GWLP_HINSTANCE));

            // The only stock control left: real text input is not worth
            // reimplementing, so an EDIT sits borderless inside the rounded
            // frame we paint for it.
            const RECT& f = g_layout.excludeField;
            g_excludeEdit = CreateWindowExW(
                0, L"EDIT", JoinExcludeList(g_settings).c_str(),
                WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                f.left + 12, f.top + 9, (f.right - f.left) - 24, 18, hwnd,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(1)), hInst, nullptr);
            SendMessageW(g_excludeEdit, WM_SETFONT, (WPARAM)g_fontField, TRUE);

            SetTimer(hwnd, kTimerAnim, 16, nullptr);
            SetTimer(hwnd, kTimerSave, 400, nullptr);
            return 0;
        }

        case WM_TIMER:
            if (wParam == kTimerAnim) {
                StepPreview();
                RECT r = g_layout.preview;
                InvalidateRect(hwnd, &r, FALSE);
            } else if (wParam == kTimerSave) {
                if (g_dirty) SaveNow(hwnd);
            }
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            PaintWindow(hwnd, dc);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            POINT pt{MouseX(lParam), MouseY(lParam)};
            const bool isCustom = (g_styleIndex == kStyleCustom);

            for (int i = 0; i < kSliderCount; ++i) {
                if (PtIn(g_layout.slReset[i], pt)) {
                    g_sliderValues[i] = kSliders[i].defV;
                    SaveNow(hwnd);
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }
            for (int i = 0; i < kSliderCount; ++i) {
                RECT hit = TrackHitRect(i);
                if (PtIn(hit, pt)) {
                    g_dragSlider = i;
                    SetCapture(hwnd);
                    SetSliderFromX(i, pt.x);
                    MarkDirty();
                    InvalidateRect(hwnd, nullptr, FALSE);
                    return 0;
                }
            }
            if (PtIn(g_layout.shapeField, pt)) {
                OpenShapeMenu(hwnd);
                return 0;
            }
            if (!isCustom && (PtIn(g_layout.invertBox, pt) || PtIn(g_layout.invertLabel, pt))) {
                g_settings.layer1Invert = !g_settings.layer1Invert;
                SaveNow(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (PtIn(g_layout.startupBox, pt) || PtIn(g_layout.startupLabel, pt)) {
                g_runAtStartup = !g_runAtStartup;
                SetRunAtStartup(g_runAtStartup);
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }
            if (isCustom && PtIn(g_layout.browseBtn, pt)) {
                BrowseForCustomCursor(hwnd);
                return 0;
            }
            if (PtIn(g_layout.reloadBtn, pt)) {
                // Forces the overlay to fully rebuild Layer 1 from
                // scratch (all system cursor roles re-applied) even
                // though style/invert/path haven't changed -- recovery
                // for when a custom cursor file occasionally fails to
                // reapply cleanly. See config.h's layer1ReloadToken.
                g_settings.layer1ReloadToken++;
                SaveNow(hwnd);
                return 0;
            }
            SetFocus(hwnd);
            return 0;
        }

        case WM_MOUSEMOVE: {
            POINT pt{MouseX(lParam), MouseY(lParam)};

            if (g_dragSlider >= 0) {
                SetSliderFromX(g_dragSlider, pt.x);
                MarkDirty();
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }

            int hoverReset = -1;
            for (int i = 0; i < kSliderCount; ++i) {
                if (PtIn(g_layout.slReset[i], pt)) {
                    hoverReset = i;
                    break;
                }
            }
            bool hoverBrowse = (g_styleIndex == kStyleCustom) &&
                                PtIn(g_layout.browseBtn, pt);
            bool hoverReload = PtIn(g_layout.reloadBtn, pt);
            if (hoverReset != g_hoverReset || hoverBrowse != g_hoverBrowse ||
                hoverReload != g_hoverReload) {
                g_hoverReset = hoverReset;
                g_hoverBrowse = hoverBrowse;
                g_hoverReload = hoverReload;
                InvalidateRect(hwnd, nullptr, FALSE);
            }

            if (!g_mouseTracking) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd, 0};
                TrackMouseEvent(&tme);
                g_mouseTracking = true;
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            g_mouseTracking = false;
            if (g_hoverReset != -1 || g_hoverBrowse || g_hoverReload) {
                g_hoverReset = -1;
                g_hoverBrowse = false;
                g_hoverReload = false;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;

        case WM_LBUTTONUP:
            if (g_dragSlider >= 0) {
                g_dragSlider = -1;
                ReleaseCapture();
                SaveNow(hwnd);
            }
            return 0;

        case WM_SETCURSOR: {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            bool hand = PtIn(g_layout.shapeField, pt) ||
                        (g_styleIndex != kStyleCustom &&
                          (PtIn(g_layout.invertBox, pt) || PtIn(g_layout.invertLabel, pt))) ||
                        PtIn(g_layout.startupBox, pt) || PtIn(g_layout.startupLabel, pt) ||
                        PtIn(g_layout.reloadBtn, pt) ||
                        ((g_styleIndex == kStyleCustom) && PtIn(g_layout.browseBtn, pt));
            for (int i = 0; i < kSliderCount && !hand; ++i) {
                if (PtIn(g_layout.slReset[i], pt) || PtIn(TrackHitRect(i), pt)) hand = true;
            }
            if (hand) {
                SetCursor(LoadCursorW(nullptr, IDC_HAND));
                return TRUE;
            }
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }

        case WM_COMMAND:
            if (HIWORD(wParam) == EN_CHANGE && LOWORD(wParam) == 1) {
                MarkDirty();
            }
            return 0;

        case WM_CTLCOLOREDIT: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetTextColor(dc, kColText);
            SetBkColor(dc, kColBg);
            return reinterpret_cast<LRESULT>(g_whiteBrush);
        }

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORDLG: {
            HDC dc = reinterpret_cast<HDC>(wParam);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, kColText);
            return reinterpret_cast<LRESULT>(g_whiteBrush);
        }

        case WM_DESTROY:
            if (g_dirty) SaveNow(hwnd);
            KillTimer(hwnd, kTimerAnim);
            KillTimer(hwnd, kTimerSave);
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    GdiplusStartupInput gdiIn;
    ULONG_PTR gdiToken = 0;
    GdiplusStartup(&gdiToken, &gdiIn, nullptr);

    g_settings = config::Load();
    PullFromSettings();
    g_runAtStartup = QueryRunAtStartup();
    g_layout = ComputeLayout();

    g_whiteBrush = CreateSolidBrush(kColBg);
    auto mkFont = [](int height, int weight) {
        return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    };
    g_fontTitle = mkFont(-17, FW_BOLD);
    g_fontSub = mkFont(-13, FW_NORMAL);
    g_fontLabel = mkFont(-14, FW_SEMIBOLD);
    g_fontField = mkFont(-14, FW_NORMAL);
    g_fontHelp = mkFont(-12, FW_NORMAL);

    constexpr wchar_t kClassName[] = L"CursorFlowSettingsWindow";
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = g_whiteBrush;
    wc.hIcon = LoadIconW(hInstance, L"MAINICON");
    wc.hIconSm = static_cast<HICON>(LoadImageW(
        hInstance, L"MAINICON", IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    RegisterClassExW(&wc);

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT want{0, 0, kWindowW, g_layout.totalH};
    AdjustWindowRect(&want, style, FALSE);

    HWND hwnd = CreateWindowExW(
        0, kClassName, L"CursorFlow \u2014 Settings", style,
        CW_USEDEFAULT, CW_USEDEFAULT, want.right - want.left, want.bottom - want.top,
        nullptr, nullptr, hInstance, nullptr);
    if (!hwnd) {
        GdiplusShutdown(gdiToken);
        return 0;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    GdiplusShutdown(gdiToken);
    return static_cast<int>(msg.wParam);
}
