#pragma once

#include <windows.h>
#include <d2d1_1.h>
#include <d3d11.h>
#include <dcomp.h>
#include <wrl/client.h>

#include <vector>

namespace renderer_d2d {

struct Point {
    float x;
    float y;
};

// Owns the D3D11 device, a DXGI composition swap chain, the DirectComposition
// visual tree that attaches it to a window, and a Direct2D device context
// bound to the swap chain's back buffer. GPU-composited and alpha-blended
// with the desktop -- no GDI, no UpdateLayeredWindow.
class Renderer {
public:
    bool Initialize(HWND hwnd, int width, int height);

    // Applies user-configurable tuning (see config.h). `blurIntensity` is a
    // multiplier on the built-in conservative blur alpha (1.0 = default,
    // 0.0 = off). `trailLength` resizes the trail ring buffer (0 = off).
    // `ghostScale` is an overall size multiplier for the ghost/blur/trail
    // (1.0 = the real cursor's actual size). Safe to call any time; takes
    // effect from the next RenderGhostAt.
    void Configure(float blurIntensity, int trailLength, float ghostScale);

    // Draws one frame: clears to fully transparent, draws a short
    // directional motion-blur streak between `blurTrailStart` (the ghost's
    // position at the start of this frame) and `position` (its position
    // now) -- a handful of the same shape at decreasing alpha, so blur
    // length naturally scales with how far the ghost moved this frame --
    // then draws the ghost itself at `position` at full opacity, rotated
    // by `rotationRadians` around that point (flick feel), and presents.
    // Vsync-locked (Present(1, 0)). If `shapeBitmap` is null, falls back to
    // a plain filled circle; otherwise draws `shapeBitmap` 1:1, anchored so
    // that `hotspot` (in the bitmap's own pixel space) lands on `position`.
    void RenderGhostAt(Point position, Point blurTrailStart,
                        float rotationRadians = 0.0f,
                        ID2D1Bitmap* shapeBitmap = nullptr,
                        Point hotspot = {0, 0});

    // Exposed so cursor_shape_sync can create bitmaps against the same
    // device/context this renderer draws with.
    ID2D1DeviceContext* GetContext() { return d2dContext_.Get(); }

private:
    void DrawShapeAt(Point position, float rotationRadians,
                      ID2D1Bitmap* shapeBitmap, Point hotspot, float opacity,
                      float scale = 1.0f);
    void PushTrailPoint(Point position);
    void DrawTrail(float rotationRadians, ID2D1Bitmap* shapeBitmap,
                    Point hotspot);

    // Ring buffer of recent ghost positions -- a separate, longer-lived,
    // fainter fading tail distinct from the single-frame motion blur above.
    // Sized by Configure(); empty (trail off) until then.
    std::vector<Point> trailPositions_;
    int trailWriteIndex_ = 0;
    int trailFilledCount_ = 0;

    float blurIntensityMultiplier_ = 1.0f;
    float ghostScaleMultiplier_ = 1.0f;


    Microsoft::WRL::ComPtr<ID3D11Device> d3dDevice_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> d3dContext_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swapChain_;
    Microsoft::WRL::ComPtr<ID2D1Factory1> d2dFactory_;
    Microsoft::WRL::ComPtr<ID2D1Device> d2dDevice_;
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> d2dContext_;
    Microsoft::WRL::ComPtr<ID2D1Bitmap1> targetBitmap_;
    Microsoft::WRL::ComPtr<IDCompositionDevice> dcompDevice_;
    Microsoft::WRL::ComPtr<IDCompositionTarget> dcompTarget_;
    Microsoft::WRL::ComPtr<IDCompositionVisual> dcompVisual_;
};

}  // namespace renderer_d2d
