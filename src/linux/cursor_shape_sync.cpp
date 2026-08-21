#include "cursor_shape_sync.h"

#include <X11/Xcursor/Xcursor.h>

#include <cstdint>

namespace cursor_shape_sync {
namespace {

bool SurfaceFromXcursorImage(const XcursorImage* img, CapturedCursor& outCursor) {
    cairo_surface_t* surface = cairo_image_surface_create(
        CAIRO_FORMAT_ARGB32, static_cast<int>(img->width),
        static_cast<int>(img->height));
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        cairo_surface_destroy(surface);
        return false;
    }

    unsigned char* dst = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);

    for (unsigned int y = 0; y < img->height; ++y) {
        auto* row = reinterpret_cast<uint32_t*>(dst + y * stride);
        for (unsigned int x = 0; x < img->width; ++x) {
            // XcursorPixel is a premultiplied 32-bit ARGB value -- matches
            // Cairo's CAIRO_FORMAT_ARGB32 (also premultiplied) directly.
            row[x] = static_cast<uint32_t>(img->pixels[y * img->width + x]);
        }
    }
    cairo_surface_mark_dirty(surface);

    outCursor.surface = surface;
    outCursor.hotspotX = img->xhot;
    outCursor.hotspotY = img->yhot;
    return true;
}

}  // namespace

bool CaptureCurrentSystemCursor(Display* display, CapturedCursor& outCursor) {
    // Deliberately NOT XFixesGetCursorImage(): that reports whatever shape
    // is active at this exact instant, which if the pointer happens to be
    // over a text field or link right as the app starts, would freeze the
    // ghost's whole-session default as an ibeam/hand instead of the
    // user's normal arrow. XcursorLibraryLoadImage("left_ptr", ...)
    // instead loads the user's configured theme's arrow cursor directly
    // by name -- the X11 equivalent of the Windows port's
    // LoadCursor(NULL, IDC_ARROW) fix -- independent of what's under the
    // pointer right now, and (called here, before cursor_scheme's grab)
    // still independent of our own override.
    char* theme = XcursorGetTheme(display);  // nullptr is a valid "use default search" value
    int size = XcursorGetDefaultSize(display);

    XcursorImage* img = XcursorLibraryLoadImage("left_ptr", theme, size);
    if (!img) {
        // Some icon themes name the default pointer "default" instead.
        img = XcursorLibraryLoadImage("default", theme, size);
    }
    if (!img) {
        return false;
    }

    bool ok = SurfaceFromXcursorImage(img, outCursor);
    XcursorImageDestroy(img);
    return ok;
}

}  // namespace cursor_shape_sync
