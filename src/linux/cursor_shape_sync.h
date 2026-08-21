#pragma once

#include <X11/Xlib.h>
#include <cairo/cairo.h>

namespace cursor_shape_sync {

struct CapturedCursor {
    cairo_surface_t* surface = nullptr;
    double hotspotX = 0;
    double hotspotY = 0;
};

// Loads the user's configured cursor theme's arrow ("left_ptr") as a
// Cairo image surface, via XcursorLibraryLoadImage -- NOT
// XFixesGetCursorImage(), which reports whatever shape is active at this
// exact instant (wrong if the pointer happens to be over a text field or
// link right as the app starts) and, once cursor_scheme::Initialize()'s
// grab is active, only ever reports our own fixed cross anyway (the same
// self-referential problem the Windows port hit with GetCursorInfo()
// after SetSystemCursor). Loading by name sidesteps both issues. Call
// once at startup, before the grab, to capture the user's real/default
// cursor appearance so the ghost can use it as its shape instead of a
// generic placeholder circle.
//
// Per-context dynamic shape detection (link/text/resize) is intentionally
// NOT attempted on Linux: X11 has no per-window "what cursor is this
// window using" query (XDefineCursor is write-only from another client's
// perspective), so replicating even the Windows port's limited
// class-cursor fallback isn't achievable here without a much more
// involved and fragile ungrab/query/regrab dance. A single captured
// "user's real cursor" shape, used consistently, matches the current
// product direction anyway.
bool CaptureCurrentSystemCursor(Display* display, CapturedCursor& outCursor);

}  // namespace cursor_shape_sync
