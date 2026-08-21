#include "renderer_cairo.h"

#include <algorithm>
#include <cmath>

namespace renderer_cairo {
namespace {

constexpr double kGhostOpacity = 0.85;

// Blur is deliberately conservative by default -- see renderer_d2d.cpp's
// identical reasoning on the Windows side. kBlurSubSteps copies are drawn
// between last frame's position and this frame's, alpha ramping up to
// kBlurMaxAlpha, so blur length/intensity naturally scales with speed.
constexpr int kBlurSubSteps = 5;
constexpr double kBlurMaxAlpha = 0.35;

// Trail: fainter than blur's peak and much longer-lived (config.h's
// trail_length controls the clone count/length), so the two read as
// visually distinct layers. Each trail point is a shrinking, fading CLONE
// of the actual cursor shape (not a plain dot), so the tail reads as "the
// cursor itself, trailing behind".
constexpr double kTrailMaxAlpha = 0.22;
constexpr double kTrailMinScale = 0.35;
constexpr double kTrailMaxScale = 0.9;

}  // namespace

bool Renderer::Initialize(Display* display, Window window, int width,
                           int height) {
    display_ = display;

    XWindowAttributes attrs;
    XGetWindowAttributes(display, window, &attrs);

    surface_ = cairo_xlib_surface_create(display, window, attrs.visual,
                                          width, height);
    return surface_ != nullptr &&
           cairo_surface_status(surface_) == CAIRO_STATUS_SUCCESS;
}

void Renderer::Configure(double blurIntensity, int trailLength,
                          double ghostScale) {
    blurIntensityMultiplier_ = blurIntensity;
    ghostScaleMultiplier_ = ghostScale;
    trailLength = std::max(0, trailLength);
    trailPositions_.assign(static_cast<size_t>(trailLength), Point{});
    trailWriteIndex_ = 0;
    trailFilledCount_ = 0;
}

void Renderer::PushTrailPoint(Point position) {
    if (trailPositions_.empty()) {
        return;
    }
    int trailLength = static_cast<int>(trailPositions_.size());
    trailPositions_[trailWriteIndex_] = position;
    trailWriteIndex_ = (trailWriteIndex_ + 1) % trailLength;
    trailFilledCount_ =
        (trailFilledCount_ < trailLength) ? trailFilledCount_ + 1 : trailLength;
}

void Renderer::DrawTrail(cairo_t* cr, double rotationRadians,
                          cairo_surface_t* shapeSurface, Point hotspot) {
    if (trailFilledCount_ == 0) {
        return;
    }
    int trailLength = static_cast<int>(trailPositions_.size());

    for (int i = 0; i < trailFilledCount_; ++i) {
        int idx = (trailWriteIndex_ - trailFilledCount_ + i + trailLength) %
                  trailLength;
        double t = static_cast<double>(i + 1) / trailFilledCount_;
        double alpha = kTrailMaxAlpha * t * t;
        double scale = kTrailMinScale + (kTrailMaxScale - kTrailMinScale) * t;

        DrawShapeAt(cr, trailPositions_[idx], rotationRadians, shapeSurface,
                    hotspot, alpha, scale);
    }
}

void Renderer::DrawShapeAt(cairo_t* cr, Point position,
                            double rotationRadians,
                            cairo_surface_t* shapeSurface, Point hotspot,
                            double opacity, double scale) {
    double effectiveScale = scale * ghostScaleMultiplier_;

    // Rotate/scale around the shape's own visual (bounding-box) center,
    // not `position` (which is anchored to the cursor's hotspot -- often
    // the tip of an arrow, nowhere near its middle) -- see renderer_d2d.cpp's
    // identical fix on the Windows side for the full reasoning.
    Point pivot = position;
    if (shapeSurface) {
        double width = cairo_image_surface_get_width(shapeSurface);
        double height = cairo_image_surface_get_height(shapeSurface);
        pivot.x = position.x - hotspot.x + width * 0.5;
        pivot.y = position.y - hotspot.y + height * 0.5;
    }

    cairo_save(cr);
    cairo_translate(cr, pivot.x, pivot.y);
    cairo_rotate(cr, rotationRadians);
    cairo_scale(cr, effectiveScale, effectiveScale);
    cairo_translate(cr, -pivot.x, -pivot.y);

    if (shapeSurface) {
        cairo_set_source_surface(cr, shapeSurface, position.x - hotspot.x,
                                  position.y - hotspot.y);
        cairo_paint_with_alpha(cr, opacity);
    } else {
        constexpr double kRadius = 10.0;
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, opacity);
        cairo_arc(cr, position.x, position.y, kRadius, 0, 2 * M_PI);
        cairo_fill(cr);
    }

    cairo_restore(cr);
}

void Renderer::RenderGhostAt(Point position, Point blurTrailStart,
                              double rotationRadians,
                              cairo_surface_t* shapeSurface, Point hotspot) {
    if (!surface_) {
        return;
    }

    cairo_t* cr = cairo_create(surface_);

    // Clear to fully transparent.
    cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
    cairo_paint(cr);
    cairo_set_operator(cr, CAIRO_OPERATOR_OVER);

    PushTrailPoint(position);
    DrawTrail(cr, rotationRadians, shapeSurface, hotspot);

    for (int i = 1; i <= kBlurSubSteps; ++i) {
        double t = static_cast<double>(i) / (kBlurSubSteps + 1);
        Point interp{blurTrailStart.x + (position.x - blurTrailStart.x) * t,
                      blurTrailStart.y + (position.y - blurTrailStart.y) * t};
        DrawShapeAt(cr, interp, rotationRadians, shapeSurface, hotspot,
                    kBlurMaxAlpha * blurIntensityMultiplier_ * t);
    }

    DrawShapeAt(cr, position, rotationRadians, shapeSurface, hotspot,
                kGhostOpacity);

    cairo_destroy(cr);
    cairo_surface_flush(surface_);
    XFlush(display_);
}

}  // namespace renderer_cairo
