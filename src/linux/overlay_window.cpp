#include "overlay_window.h"

#include <X11/extensions/shape.h>
#include <X11/Xatom.h>

namespace overlay_window {

Bounds GetVirtualScreenBounds(Display* display) {
    // Unlike Windows, X11's root window coordinate space is always
    // (0,0)-anchored at the combined virtual screen's top-left -- RandR
    // arranges monitor outputs within that single space, there's no
    // equivalent of a negative SM_XVIRTUALSCREEN origin to account for.
    int screen = DefaultScreen(display);
    Bounds b;
    b.x = 0;
    b.y = 0;
    b.width = DisplayWidth(display, screen);
    b.height = DisplayHeight(display, screen);
    return b;
}

Window Create(Display* display, const Bounds& bounds) {
    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);

    XVisualInfo visualInfo;
    if (!XMatchVisualInfo(display, screen, 32, TrueColor, &visualInfo)) {
        return 0;  // no ARGB visual available -- caller treats 0 as failure
    }

    XSetWindowAttributes attrs{};
    attrs.colormap = XCreateColormap(display, root, visualInfo.visual, AllocNone);
    attrs.border_pixel = 0;
    attrs.background_pixel = 0;
    attrs.override_redirect = True;

    Window win = XCreateWindow(
        display, root, bounds.x, bounds.y, bounds.width, bounds.height, 0,
        visualInfo.depth, InputOutput, visualInfo.visual,
        CWColormap | CWBorderPixel | CWBackPixel | CWOverrideRedirect,
        &attrs);
    if (!win) {
        return 0;
    }

    // Click-through: set the window's INPUT shape (distinct from its
    // visible/bounding shape) to an empty region, so every mouse/keyboard
    // event passes through to whatever is beneath -- the X11 analogue of
    // Windows' WS_EX_TRANSPARENT, and (unlike Windows, which additionally
    // needed WS_EX_LAYERED before click-through actually worked) this is
    // sufficient on its own.
    XShapeCombineRectangles(display, win, ShapeInput, 0, 0, nullptr, 0,
                             ShapeSet, 0);

    // Always-on-top via EWMH, since override-redirect stacking order isn't
    // strictly guaranteed by every window manager.
    Atom wmState = XInternAtom(display, "_NET_WM_STATE", False);
    Atom wmStateAbove = XInternAtom(display, "_NET_WM_STATE_ABOVE", False);
    XChangeProperty(display, win, wmState, XA_ATOM, 32, PropModeReplace,
                     reinterpret_cast<unsigned char*>(&wmStateAbove), 1);

    XMapWindow(display, win);
    XFlush(display);
    return win;
}

}  // namespace overlay_window
