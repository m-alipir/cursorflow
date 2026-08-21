#pragma once

#include <X11/Xlib.h>

namespace overlay_window {

struct Bounds {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

// Bounds of the full virtual screen (all monitors combined), via RandR if
// available, falling back to the default screen's reported size.
Bounds GetVirtualScreenBounds(Display* display);

// Creates a borderless, click-through, always-on-top window covering
// `bounds`:
//  - override_redirect=True so the window manager never decorates,
//    resizes, or otherwise manages it (the X11 analogue of a topmost
//    unmanaged popup).
//  - a 32-bit ARGB visual so per-pixel alpha composites correctly -- this
//    REQUIRES a compositing window manager to be running (picom, or a
//    desktop environment's built-in compositor such as KWin/Mutter); most
//    modern setups including CachyOS's default KDE Plasma session
//    composite by default, but a bare non-compositing WM will show this
//    window as opaque.
//  - XShapeCombineRegion(..., ShapeInput, ...) with an empty region, so
//    the window is fully click-through -- the X11 equivalent of Windows'
//    WS_EX_TRANSPARENT, and (unlike the Windows port's WS_EX_LAYERED
//    surprise) this alone is sufficient; no extra flag needed.
//  - _NET_WM_STATE_ABOVE set via EWMH for always-on-top, since
//    override-redirect stacking order isn't strictly guaranteed by every
//    window manager.
Window Create(Display* display, const Bounds& bounds);

}  // namespace overlay_window
