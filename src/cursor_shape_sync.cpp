#include "cursor_shape_sync.h"

using Microsoft::WRL::ComPtr;

namespace cursor_shape_sync {
namespace {

// Manual hit-test walking the top-level z-order, skipping `excludeHwnd`
// (our own overlay, which WindowFromPoint would otherwise always return
// since it's WS_EX_TOPMOST and covers the whole screen) and any OTHER
// input-transparent window (WS_EX_TRANSPARENT) found along the way -- e.g.
// hidden automation/helper windows some dev tools keep around, which are
// themselves meant to be invisible to input and would otherwise be
// mistaken for the real app underneath.
HWND FindWindowUnderPointSkipping(POINT pt, HWND excludeHwnd) {
    for (HWND hwnd = GetTopWindow(nullptr); hwnd;
         hwnd = GetWindow(hwnd, GW_HWNDNEXT)) {
        if (hwnd == excludeHwnd || !IsWindowVisible(hwnd)) {
            continue;
        }
        LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        if (exStyle & WS_EX_TRANSPARENT) {
            continue;
        }
        RECT rect;
        if (GetWindowRect(hwnd, &rect) && PtInRect(&rect, pt)) {
            return hwnd;
        }
    }
    return nullptr;
}

// Most real cursor-setting happens on the specific CHILD control under the
// pointer (an Edit control's class is registered with IDC_IBEAM, a
// Button's with IDC_ARROW, etc.) rather than on the top-level frame window,
// so drill down to the deepest child at `screenPt` before reading a class
// cursor.
HWND DrillDownToChild(HWND topLevel, POINT screenPt) {
    HWND current = topLevel;
    for (int i = 0; i < 32; ++i) {  // guards against any misbehaving child chain
        POINT clientPt = screenPt;
        if (!ScreenToClient(current, &clientPt)) {
            break;
        }
        HWND child = ChildWindowFromPointEx(
            current, clientPt,
            CWP_SKIPINVISIBLE | CWP_SKIPDISABLED | CWP_SKIPTRANSPARENT);
        if (!child || child == current) {
            break;
        }
        current = child;
    }
    return current;
}

// Renders hCursor into an off-screen 32bpp premultiplied-alpha DIB via
// DrawIconEx (which correctly composites AND/XOR masks or a true alpha
// channel regardless of the source cursor's internal format), then uploads
// that as a Direct2D bitmap.
bool ExtractBitmapFromCursor(ID2D1DeviceContext* d2dContext, HCURSOR hCursor,
                              ComPtr<ID2D1Bitmap>& outBitmap,
                              POINT& hotspotOut) {
    if (!hCursor) {
        return false;
    }

    ICONINFO info{};
    if (!GetIconInfo(hCursor, &info)) {
        return false;
    }
    struct IconInfoGuard {
        ICONINFO& info;
        ~IconInfoGuard() {
            if (info.hbmMask) DeleteObject(info.hbmMask);
            if (info.hbmColor) DeleteObject(info.hbmColor);
        }
    } guard{info};

    BITMAP maskBmp{};
    if (GetObject(info.hbmMask, sizeof(maskBmp), &maskBmp) == 0) {
        return false;
    }
    int width = maskBmp.bmWidth;
    // Monochrome cursors (no hbmColor) pack AND+XOR masks stacked in one
    // bitmap of double height.
    int height = info.hbmColor ? maskBmp.bmHeight : maskBmp.bmHeight / 2;
    if (width <= 0 || height <= 0 || width > 256 || height > 256) {
        return false;
    }

    HDC screenDc = GetDC(nullptr);
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP dib = CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &bits,
                                    nullptr, 0);
    ReleaseDC(nullptr, screenDc);
    if (!dib || !bits) {
        return false;
    }
    ZeroMemory(bits, static_cast<size_t>(width) * height * 4);

    HDC memDc = CreateCompatibleDC(nullptr);
    HBITMAP oldBmp = static_cast<HBITMAP>(SelectObject(memDc, dib));
    DrawIconEx(memDc, 0, 0, hCursor, width, height, 0, nullptr, DI_NORMAL);
    SelectObject(memDc, oldBmp);
    DeleteDC(memDc);

    auto* pixels = static_cast<BYTE*>(bits);
    size_t pixelCount = static_cast<size_t>(width) * height;
    bool anyOpaque = false;
    for (size_t i = 0; i < pixelCount; ++i) {
        BYTE* p = pixels + i * 4;
        BYTE a = p[3];
        if (a > 0) {
            anyOpaque = true;
        }
        // Premultiply -- our D2D bitmap is created as premultiplied.
        p[0] = static_cast<BYTE>(p[0] * a / 255);
        p[1] = static_cast<BYTE>(p[1] * a / 255);
        p[2] = static_cast<BYTE>(p[2] * a / 255);
    }

    HRESULT hr = E_FAIL;
    if (anyOpaque) {
        D2D1_BITMAP_PROPERTIES bmpProps = D2D1::BitmapProperties(D2D1::PixelFormat(
            DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        hr = d2dContext->CreateBitmap(
            D2D1::SizeU(static_cast<UINT32>(width), static_cast<UINT32>(height)),
            pixels, static_cast<UINT32>(width) * 4, bmpProps,
            outBitmap.GetAddressOf());
    }

    DeleteObject(dib);
    if (FAILED(hr)) {
        return false;
    }

    hotspotOut.x = static_cast<LONG>(info.xHotspot);
    hotspotOut.y = static_cast<LONG>(info.yHotspot);
    return true;
}

struct RoleMapping {
    Role role;
    LPCWSTR idc;
};

// Not constexpr: MAKEINTRESOURCE (which IDC_* expands to) casts an integer
// to a pointer, which isn't a valid constant expression -- this is a
// plain namespace-scope const instead, dynamically initialized once at
// startup.
const RoleMapping kRoleMappings[] = {
    {Role::kArrow, IDC_ARROW},     {Role::kIBeam, IDC_IBEAM},
    {Role::kHand, IDC_HAND},       {Role::kSizeAll, IDC_SIZEALL},
    {Role::kSizeNWSE, IDC_SIZENWSE}, {Role::kSizeNESW, IDC_SIZENESW},
    {Role::kSizeWE, IDC_SIZEWE},   {Role::kSizeNS, IDC_SIZENS},
    {Role::kCross, IDC_CROSS},     {Role::kWait, IDC_WAIT},
};

}  // namespace

bool CaptureCursorAtPoint(ID2D1DeviceContext* d2dContext, POINT screenPoint,
                           HWND excludeHwnd, ComPtr<ID2D1Bitmap>& outBitmap,
                           POINT& hotspotOut) {
    HWND topLevel = FindWindowUnderPointSkipping(screenPoint, excludeHwnd);
    if (!topLevel) {
        return false;
    }
    HWND target = DrillDownToChild(topLevel, screenPoint);

    HCURSOR classCursor =
        reinterpret_cast<HCURSOR>(GetClassLongPtrW(target, GCLP_HCURSOR));
    return ExtractBitmapFromCursor(d2dContext, classCursor, outBitmap,
                                    hotspotOut);
}

bool CaptureCurrentSystemCursor(ID2D1DeviceContext* d2dContext,
                                 ComPtr<ID2D1Bitmap>& outBitmap,
                                 POINT& hotspotOut) {
    // Deliberately NOT GetCursorInfo(): that reports whatever shape is
    // active at this exact instant, which if the pointer happens to be
    // over a text field or link right as the app starts, would freeze the
    // ghost's whole-session default as an ibeam/hand instead of the
    // user's normal arrow. LoadCursor(NULL, IDC_ARROW) instead asks
    // directly for the currently-configured "Arrow" role -- the user's
    // real resting cursor shape, independent of what's under the pointer
    // right now. NULL hInstance + a stock IDC_* name is what makes this
    // resolve against the live system cursor table rather than loading a
    // fixed built-in resource, so it still works uninstrumented before
    // cursor_scheme::ApplyOverride() runs.
    HCURSOR hCursor = LoadCursorW(nullptr, IDC_ARROW);
    return ExtractBitmapFromCursor(d2dContext, hCursor, outBitmap, hotspotOut);
}

bool CaptureAllRoleCursors(ID2D1DeviceContext* d2dContext, RoleCursors& out) {
    bool anySucceeded = false;
    for (const RoleMapping& mapping : kRoleMappings) {
        int idx = static_cast<int>(mapping.role);
        HCURSOR hCursor = LoadCursorW(nullptr, mapping.idc);
        out.valid[idx] = ExtractBitmapFromCursor(d2dContext, hCursor,
                                                  out.bitmaps[idx], out.hotspots[idx]);
        anySucceeded = anySucceeded || out.valid[idx];
    }
    return anySucceeded;
}

Role DetectActiveRole() {
    CURSORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetCursorInfo(&info) || !(info.flags & CURSOR_SHOWING) || !info.hCursor) {
        return Role::kArrow;
    }
    for (const RoleMapping& mapping : kRoleMappings) {
        if (LoadCursorW(nullptr, mapping.idc) == info.hCursor) {
            return mapping.role;
        }
    }
    return Role::kArrow;
}

}  // namespace cursor_shape_sync
