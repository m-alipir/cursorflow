#include "cursor_scheme.h"

#include <X11/Xcursor/Xcursor.h>

#include <cstdint>

namespace cursor_scheme {
namespace {

constexpr int kSize = 32;
constexpr int kArmLength = 6;
constexpr int kOutlineExtra = 1;    // +1px black border all around a white fill
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

// `halfThickness`: 0 = 1px-thick arms (kThinCross), 1 = 3px-thick
// (kThickCross). `invert`: true draws a white fill with a black outline
// (X11 has no true screen-invert compositing primitive, so this stands
// in for the Windows port's AND=1/XOR=1 invert as the closest visually-
// contrasting equivalent); false draws a plain solid black fill, no
// outline.
XcursorImage* BuildCrossImage(bool invert, int halfThickness) {
    XcursorImage* image = NewBlankImage();
    if (!image) return nullptr;
    int center = kSize / 2;
    if (invert) {
        // Black first (outline + fill footprint), then white on top for
        // just the fill, leaving a visible black border.
        for (int i = -kArmLength; i <= kArmLength; ++i) {
            for (int t = -(halfThickness + kOutlineExtra);
                 t <= halfThickness + kOutlineExtra; ++t) {
                SetPixel(image, center + i, center + t, 0xFF000000u);
                SetPixel(image, center + t, center + i, 0xFF000000u);
            }
        }
        for (int i = -kArmLength; i <= kArmLength; ++i) {
            for (int t = -halfThickness; t <= halfThickness; ++t) {
                SetPixel(image, center + i, center + t, 0xFFFFFFFFu);
                SetPixel(image, center + t, center + i, 0xFFFFFFFFu);
            }
        }
    } else {
        for (int i = -kArmLength; i <= kArmLength; ++i) {
            for (int t = -halfThickness; t <= halfThickness; ++t) {
                SetPixel(image, center + i, center + t, 0xFF000000u);
                SetPixel(image, center + t, center + i, 0xFF000000u);
            }
        }
    }
    return image;
}

XcursorImage* BuildDotImage(bool invert) {
    XcursorImage* image = NewBlankImage();
    if (!image) return nullptr;
    int center = kSize / 2;
    uint32_t fillColor = invert ? 0xFFFFFFFFu : 0xFF000000u;
    int outlineRadius = invert ? kDotRadius + kOutlineExtra : 0;
    if (invert) {
        for (int y = -outlineRadius; y <= outlineRadius; ++y) {
            for (int x = -outlineRadius; x <= outlineRadius; ++x) {
                if (x * x + y * y <= outlineRadius * outlineRadius) {
                    SetPixel(image, center + x, center + y, 0xFF000000u);
                }
            }
        }
    }
    for (int y = -kDotRadius; y <= kDotRadius; ++y) {
        for (int x = -kDotRadius; x <= kDotRadius; ++x) {
            if (x * x + y * y <= kDotRadius * kDotRadius) {
                SetPixel(image, center + x, center + y, fillColor);
            }
        }
    }
    return image;
}

Cursor BuildStyledCursor(Display* display, Style style, bool invert,
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
        case Style::kThinCross:
            image = BuildCrossImage(invert, /*halfThickness=*/0);
            break;
        case Style::kDot:
            image = BuildDotImage(invert);
            break;
        case Style::kCustom:
        case Style::kThickCross:
        default:
            image = BuildCrossImage(style == Style::kCustom ? true : invert,
                                     /*halfThickness=*/1);
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

bool Initialize(Display* display, Window grabWindow, Style style, bool invert,
                 const std::string& customCursorPath) {
    g_cursor = BuildStyledCursor(display, style, invert, customCursorPath);
    if (g_cursor == None) {
        return false;
    }
    ReassertGrab(display, grabWindow);
    return true;
}

void SetStyle(Display* display, Window grabWindow, Style style, bool invert,
              const std::string& customCursorPath) {
    Cursor newCursor = BuildStyledCursor(display, style, invert, customCursorPath);
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
