#pragma once

#include <X11/Xlib.h>
#include <cairo/cairo-xlib.h>
#include <cairo/cairo.h>

#include <vector>

namespace renderer_cairo {

struct Point {
    double x;
    double y;
};

// Owns a Cairo Xlib surface bound directly to the overlay window and
// draws each frame into it. Unlike the Windows port's DXGI/DirectComposition
// swap chain, this has no compositor-synced present call -- the caller
// (main.cpp) paces frames with a fixed-rate sleep loop instead, and actual
// smoothness/tear-freedom depends on the window manager's compositor
// (picom, KWin, Mutter, ...) doing its usual job of compositing this
// window like any other. A future improvement could move to GLX for true
// vsync + double buffering; out of scope for this first pass.
class Renderer {
public:
    bool Initialize(Display* display, Window window, int width, int height);

    // Applies user-configurable tuning (see config.h). `blurIntensity` is
    // a multiplier on the built-in conservative blur alpha (1.0 =
    // default, 0.0 = off). `trailLength` resizes the trail ring buffer
    // (0 = off). `ghostScale` is an overall size multiplier for the
    // ghost/blur/trail (1.0 = real cursor size).
    void Configure(double blurIntensity, int trailLength,
                    double ghostScale = 1.0);

    // Draws one frame: clears to fully transparent, draws the trail, then
    // a short directional motion-blur streak between `blurTrailStart` and
    // `position`, then the ghost itself at `position` at full opacity,
    // rotated by `rotationRadians` (flick feel), and flushes to the
    // window. If `shapeSurface` is null, falls back to a plain filled
    // circle; otherwise draws `shapeSurface` 1:1, anchored so that
    // `hotspot` (in the surface's own pixel space) lands on `position`.
    void RenderGhostAt(Point position, Point blurTrailStart,
                        double rotationRadians = 0.0,
                        cairo_surface_t* shapeSurface = nullptr,
                        Point hotspot = {0, 0});

private:
    void DrawShapeAt(cairo_t* cr, Point position, double rotationRadians,
                      cairo_surface_t* shapeSurface, Point hotspot,
                      double opacity, double scale = 1.0);
    void PushTrailPoint(Point position);
    void DrawTrail(cairo_t* cr, double rotationRadians,
                    cairo_surface_t* shapeSurface, Point hotspot);

    Display* display_ = nullptr;
    cairo_surface_t* surface_ = nullptr;

    std::vector<Point> trailPositions_;
    int trailWriteIndex_ = 0;
    int trailFilledCount_ = 0;
    double blurIntensityMultiplier_ = 1.0;
    double ghostScaleMultiplier_ = 1.0;
};

}  // namespace renderer_cairo
