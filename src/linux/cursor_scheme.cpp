#include "cursor_scheme.h"

#include <X11/Xcursor/Xcursor.h>

#include <cstdint>

namespace cursor_scheme {
namespace {

constexpr int kSize = 32;
constexpr int kArmLength = 6;
constexpr int kHalfThickness = 1;   // white core thickness = 2*1+1 = 3px
constexpr int kOutlineExtra = 1;    // +1px black border all around the core
constexpr int kDotRadius = 4;

Cursor g_cursor = None;

XcursorImage* NewBlankImage() {
    XcursorImage* image = XcursorImageCreate(kSize, kSize);
    if (!image) {
        return nullptr;
    }
    image->xhot = kSize / 2;
    image->yhot = kSize / 2;
    for (int i = 0; i < kSize * kSize; ++i) {
        image->pixels[i] = 0x00000000;  // transparent
    }
    return image;
}

void SetPixel(XcursorImage* image, int x, int y, uint32_t argb) {
    if (x < 0 || x >= kSize || y < 0 || y >= kSize) return;
    image->pixels[y * kSize + x] = argb;
}

// White core with a black outline -- X11 has no true screen-invert
// compositing primitive (Xcursor/XRender cursors are plain alpha-blended
// sprites), so this stands in for the Windows port's AND=1/XOR=1 invert
// cross as the closest visually-contrasting equivalent, and is the
// default style.
XcursorImage* BuildInvertCrossImage() {
    XcursorImage* image = NewBlankImage();
    if (!image) return nullptr;
    int center = kSize / 2;
    // Black first (outline + core footprint), then white on top for just
    // the core, leaving a visible black border.
    for (int i = -kArmLength; i <= kArmLength; ++i) {
        for (int t = -(kHalfThickness + kOutlineExtra);
             t <= kHalfThickness + kOutlineExtra; ++t) {
            SetPixel(image, center + i, center + t, 0xFF000000u);
            SetPixel(image, center + t, center + i, 0xFF000000u);
        }
    }
    for (int i = -kArmLength; i <= kArmLength; ++i) {
        for (int t = -kHalfThickness; t <= kHalfThickness; ++t) {
            SetPixel(image, center + i, center + t, 0xFFFFFFFFu);
            SetPixel(image, center + t, center + i, 0xFFFFFFFFu);
        }
    }
    return image;
}

// Plain solid black cross, no white core -- the original/"eski" look from
// before the invert-style redesign.
XcursorImage* BuildSolidCrossImage() {
    XcursorImage* image = NewBlankImage();
    if (!image) return nullptr;
    int center = kSize / 2;
    for (int i = -kArmLength; i <= kArmLength; ++i) {
        for (int t = -kHalfThickness; t <= kHalfThickness; ++t) {
            SetPixel(image, center + i, center + t, 0xFF000000u);
            SetPixel(image, center + t, center + i, 0xFF000000u);
        }
    }
    return image;
}

XcursorImage* BuildDotImage() {
    XcursorImage* image = NewBlankImage();
    if (!image) return nullptr;
    int center = kSize / 2;
    for (int y = -kDotRadius; y <= kDotRadius; ++y) {
        for (int x = -kDotRadius; x <= kDotRadius; ++x) {
            if (x * x + y * y <= kDotRadius * kDotRadius) {
                SetPixel(image, center + x, center + y, 0xFFFFFFFFu);
            }
        }
    }
    return image;
}

Cursor BuildStyledCursor(Display* display, Style style,
                          const std::string& customCursorPath) {
    if (style == Style::kCustom && !customCursorPath.empty()) {
        Cursor custom = XcursorFilenameLoadCursor(display, customCursorPath.c_str());
        if (custom != None) {
            return custom;
        }
        // Fall through to the default look on load failure (bad path,
        // unsupported format -- Xcursor only understands the Xcursor
        // binary format, not Windows .cur/.ani/.ico).
    }

    XcursorImage* image = nullptr;
    switch (style) {
        case Style::kSolidCross:
            image = BuildSolidCrossImage();
            break;
        case Style::kDot:
            image = BuildDotImage();
            break;
        case Style::kInvertCross:
        case Style::kCustom:
        default:
            image = BuildInvertCrossImage();
            break;
    }
    if (!image) {
        return None;
    }
    Cursor cursor = XcursorImageLoadCursor(display, image);
    XcursorImageDestroy(image);
    return cursor;
}

}  // namespace

bool Initialize(Display* display, Window grabWindow, Style style,
                 const std::string& customCursorPath) {
    g_cursor = BuildStyledCursor(display, style, customCursorPath);
    if (g_cursor == None) {
        return false;
    }
    ReassertGrab(display, grabWindow);
    return true;
}

void SetStyle(Display* display, Window grabWindow, Style style,
              const std::string& customCursorPath) {
    Cursor newCursor = BuildStyledCursor(display, style, customCursorPath);
    if (newCursor == None) {
        return;
    }
    Cursor oldCursor = g_cursor;
    g_cursor = newCursor;
    // Re-grabbing with the new cursor (even while already holding the
    // grab) updates the displayed image immediately.
    ReassertGrab(display, grabWindow);
    if (oldCursor != None) {
        XFreeCursor(display, oldCursor);
    }
}

void ReassertGrab(Display* display, Window grabWindow) {
    if (g_cursor == None) {
        return;
    }
    // owner_events=True: events still reach whatever window is actually
    // under the pointer, so we never steal input -- only the displayed
    // cursor image is affected. Empty event_mask: we don't want the grab
    // to also report anything to us directly (raw input for physics comes
    // from XInput2 separately; see raw_input.h).
    XGrabPointer(display, grabWindow, True, 0, GrabModeAsync, GrabModeAsync,
                 None, g_cursor, CurrentTime);
}

void Release(Display* display) {
    XUngrabPointer(display, CurrentTime);
    XFlush(display);
}

}  // namespace cursor_scheme
