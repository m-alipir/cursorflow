#pragma once

#include <windows.h>

#include <d2d1_1.h>
#include <wrl/client.h>

namespace cursor_shape_sync {

// Best-effort detection of the cursor shape the window under `screenPoint`
// intends to show, captured as a Direct2D bitmap for the renderer to use
// as Layer 2's texture instead of the default circle placeholder.
//
// IMPORTANT CAVEAT: Layer 1 (cursor_scheme::ApplyOverride) replaces every
// OCR_* system cursor with a single fixed shape, so GetCursorInfo() can no
// longer tell us what the app under the pointer "really" wants to show --
// it only ever returns our own override. To work around that, this reads
// the CLASS cursor (GCLP_HCURSOR) of the real window under the pointer
// (skipping our own overlay window in the z-order search) instead of
// asking the OS for the "current" cursor. That correctly catches cursors
// registered on a window class (many native controls, resize borders,
// Explorer) but CANNOT see per-widget cursors an app sets dynamically in
// its own WM_SETCURSOR handler -- which is how browsers and many
// custom-drawn UIs implement hover cursors (one window class for the whole
// surface, shape picked per element at runtime). Closing that gap would
// need a system-wide WH_CALLWNDPROC hook DLL injected into every GUI
// process, which is a substantially larger subsystem and directly at odds
// with the anti-cheat-safety goal of the M8 milestone, so it is
// deliberately out of scope. When detection doesn't apply -- dynamic
// cursor, unrecognized/animated cursor, or extraction fails for any
// reason -- this returns false and the caller keeps its default shape.
bool CaptureCursorAtPoint(ID2D1DeviceContext* d2dContext, POINT screenPoint,
                           HWND excludeHwnd,
                           Microsoft::WRL::ComPtr<ID2D1Bitmap>& outBitmap,
                           POINT& hotspotOut);

// Captures whatever cursor GetCursorInfo() reports as active RIGHT NOW.
// Only meaningful if called BEFORE cursor_scheme::ApplyOverride() -- once
// Layer 1's override is applied, GetCursorInfo() only ever reports our own
// fixed cross shape. Call this once at startup, before applying the
// override, to grab the user's real/default cursor appearance (their
// actual arrow, or whatever cursor theme they have set) so the ghost can
// use it as its default shape instead of a generic placeholder circle.
bool CaptureCurrentSystemCursor(ID2D1DeviceContext* d2dContext,
                                 Microsoft::WRL::ComPtr<ID2D1Bitmap>& outBitmap,
                                 POINT& hotspotOut);

// A handful of well-known stock cursor roles, mirroring the OCR_* system
// cursor set. Used by CaptureAllRoleCursors/DetectActiveRole below to give
// Layer 2 (the ghost) a context-appropriate shape -- e.g. an I-beam while
// hovering a text field -- despite CaptureCursorAtPoint's per-widget
// caveat above.
enum class Role {
    kArrow,
    kIBeam,
    kHand,
    kSizeAll,
    kSizeNWSE,
    kSizeNESW,
    kSizeWE,
    kSizeNS,
    kCross,
    kWait,
};
constexpr int kRoleCount = static_cast<int>(Role::kWait) + 1;

struct RoleCursors {
    Microsoft::WRL::ComPtr<ID2D1Bitmap> bitmaps[kRoleCount];
    POINT hotspots[kRoleCount]{};
    bool valid[kRoleCount]{};
};

// Captures the TRUE pristine bitmap for each role above, by reading
// LoadCursorW(nullptr, IDC_*)'s content for each one. Just like
// CaptureCurrentSystemCursor, this MUST be called before
// cursor_scheme::ApplyOverride() -- afterward, every role's content has
// been overwritten with Layer 1's single fixed shape via SetSystemCursor,
// so there is nothing distinct left to extract.
bool CaptureAllRoleCursors(ID2D1DeviceContext* d2dContext, RoleCursors& out);

// Identifies which of the roles above the foreground app under the
// pointer actually intends to show RIGHT NOW, via a handle-identity
// trick: GetCursorInfo() reports whichever per-role system-cursor HANDLE
// was last passed to SetCursor() -- e.g. a standard EDIT control's
// default WM_SETCURSOR handling calls SetCursor(LoadCursor(NULL,
// IDC_IBEAM)) while the pointer is over its text area -- and that
// HANDLE'S IDENTITY is unaffected by cursor_scheme::ApplyOverride()
// overwriting its underlying BITMAP CONTENT via SetSystemCursor (the same
// separation Windows' own Control Panel cursor schemes rely on: swapping
// a scheme doesn't invalidate cursors already loaded against an IDC_*
// name). This is what makes context detection work for cursors an app
// sets dynamically in WM_SETCURSOR -- CaptureCursorAtPoint's class-cursor
// approach above cannot see those -- as long as the app used an OS stock
// cursor role rather than a fully custom cursor resource. Returns
// Role::kArrow (the safe "no special context" default) when the pointer
// isn't showing or nothing matches a known role.
Role DetectActiveRole();

}  // namespace cursor_shape_sync
