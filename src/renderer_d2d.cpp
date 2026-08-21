#include "renderer_d2d.h"

#include <dxgi1_2.h>

#include <algorithm>

using Microsoft::WRL::ComPtr;

namespace renderer_d2d {

bool Renderer::Initialize(HWND hwnd, int width, int height) {
    UINT deviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

    D3D_FEATURE_LEVEL featureLevel{};
    HRESULT hr = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, deviceFlags, nullptr, 0,
        D3D11_SDK_VERSION, &d3dDevice_, &featureLevel, &d3dContext_);
    if (FAILED(hr)) {
        return false;
    }

    ComPtr<IDXGIDevice> dxgiDevice;
    hr = d3dDevice_.As(&dxgiDevice);
    if (FAILED(hr)) {
        return false;
    }

    ComPtr<IDXGIAdapter> dxgiAdapter;
    dxgiDevice->GetAdapter(&dxgiAdapter);

    ComPtr<IDXGIFactory2> dxgiFactory;
    hr = dxgiAdapter->GetParent(IID_PPV_ARGS(&dxgiFactory));
    if (FAILED(hr)) {
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 scDesc{};
    scDesc.Width = static_cast<UINT>(width);
    scDesc.Height = static_cast<UINT>(height);
    scDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scDesc.SampleDesc.Count = 1;
    scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scDesc.BufferCount = 2;
    scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scDesc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;

    hr = dxgiFactory->CreateSwapChainForComposition(d3dDevice_.Get(), &scDesc,
                                                      nullptr, &swapChain_);
    if (FAILED(hr)) {
        return false;
    }

    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                            d2dFactory_.GetAddressOf());
    if (FAILED(hr)) {
        return false;
    }

    hr = d2dFactory_->CreateDevice(dxgiDevice.Get(), &d2dDevice_);
    if (FAILED(hr)) {
        return false;
    }

    hr = d2dDevice_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
                                          &d2dContext_);
    if (FAILED(hr)) {
        return false;
    }

    ComPtr<IDXGISurface> backBufferSurface;
    hr = swapChain_->GetBuffer(0, IID_PPV_ARGS(&backBufferSurface));
    if (FAILED(hr)) {
        return false;
    }

    D2D1_BITMAP_PROPERTIES1 bitmapProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM,
                           D2D1_ALPHA_MODE_PREMULTIPLIED));
    hr = d2dContext_->CreateBitmapFromDxgiSurface(
        backBufferSurface.Get(), &bitmapProps, &targetBitmap_);
    if (FAILED(hr)) {
        return false;
    }

    d2dContext_->SetTarget(targetBitmap_.Get());

    hr = DCompositionCreateDevice(dxgiDevice.Get(),
                                   IID_PPV_ARGS(&dcompDevice_));
    if (FAILED(hr)) {
        return false;
    }

    hr = dcompDevice_->CreateTargetForHwnd(hwnd, TRUE, &dcompTarget_);
    if (FAILED(hr)) {
        return false;
    }

    hr = dcompDevice_->CreateVisual(&dcompVisual_);
    if (FAILED(hr)) {
        return false;
    }

    dcompVisual_->SetContent(swapChain_.Get());
    dcompTarget_->SetRoot(dcompVisual_.Get());
    dcompDevice_->Commit();

    return true;
}

namespace {
constexpr float kGhostOpacity = 0.85f;

// Blur is deliberately conservative by default -- accumulation-buffer blur
// at 60Hz can read as "smeared and laggy" rather than smooth if overdone,
// especially for viewers not used to high refresh rates. kBlurSubSteps
// copies are drawn between last frame's position and this frame's, with
// alpha ramping up to kBlurMaxAlpha, so blur length/intensity naturally
// scales with how far the ghost moved this frame (i.e. with speed) and
// nearly vanishes when the cursor is close to stationary.
constexpr int kBlurSubSteps = 5;
constexpr float kBlurMaxAlpha = 0.35f;

// Trail: fainter than blur's peak alpha and much longer-lived (default 24
// frames vs. blur's single-frame interpolation -- see config.h's
// trail_length for how a user changes the clone count/length), so the two
// read as visually distinct layers rather than one blob. Each trail point
// is a shrinking, fading CLONE of the actual cursor shape (not a plain
// dot), so the tail reads as "the cursor itself, trailing behind".
constexpr float kTrailMaxAlpha = 0.22f;
constexpr float kTrailMinScale = 0.35f;
constexpr float kTrailMaxScale = 0.9f;
}  // namespace

void Renderer::Configure(float blurIntensity, int trailLength,
                          float ghostScale) {
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
    trailFilledCount_ = (trailFilledCount_ < trailLength) ? trailFilledCount_ + 1
                                                           : trailLength;
}

void Renderer::DrawTrail(float rotationRadians, ID2D1Bitmap* shapeBitmap,
                          Point hotspot) {
    if (trailFilledCount_ == 0) {
        return;
    }
    int trailLength = static_cast<int>(trailPositions_.size());

    for (int i = 0; i < trailFilledCount_; ++i) {
        int idx =
            (trailWriteIndex_ - trailFilledCount_ + i + trailLength) % trailLength;
        // i=0 is the oldest sample in the buffer, higher i is more recent.
        float t = static_cast<float>(i + 1) / static_cast<float>(trailFilledCount_);
        float alpha = kTrailMaxAlpha * t * t;  // fade faster for older points
        float scale = kTrailMinScale + (kTrailMaxScale - kTrailMinScale) * t;

        DrawShapeAt(trailPositions_[idx], rotationRadians, shapeBitmap,
                    hotspot, alpha, scale);
    }
}

void Renderer::DrawShapeAt(Point position, float rotationRadians,
                            ID2D1Bitmap* shapeBitmap, Point hotspot,
                            float opacity, float scale) {
    constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;
    float effectiveScale = scale * ghostScaleMultiplier_;

    // Rotate/scale around the shape's own visual (bounding-box) center,
    // not `position` (which is anchored to the cursor's hotspot -- often
    // the tip of an arrow, nowhere near its middle). Pivoting on the
    // hotspot made the tail swing through a wide arc for a small lean
    // angle; pivoting on the true center reads as the whole shape
    // leaning, like a compass needle or a dragged flag.
    D2D1_POINT_2F pivot;
    if (shapeBitmap) {
        D2D1_SIZE_F size = shapeBitmap->GetSize();
        pivot = D2D1::Point2F(position.x - hotspot.x + size.width * 0.5f,
                               position.y - hotspot.y + size.height * 0.5f);
    } else {
        pivot = D2D1::Point2F(position.x, position.y);
    }

    D2D1::Matrix3x2F transform =
        D2D1::Matrix3x2F::Scale(D2D1::SizeF(effectiveScale, effectiveScale),
                                 pivot) *
        D2D1::Matrix3x2F::Rotation(rotationRadians * kRadToDeg, pivot);
    d2dContext_->SetTransform(transform);

    if (shapeBitmap) {
        D2D1_SIZE_F size = shapeBitmap->GetSize();
        float left = position.x - hotspot.x;
        float top = position.y - hotspot.y;
        D2D1_RECT_F destRect =
            D2D1::RectF(left, top, left + size.width, top + size.height);
        d2dContext_->DrawBitmap(shapeBitmap, destRect, opacity,
                                 D2D1_INTERPOLATION_MODE_LINEAR, nullptr,
                                 nullptr);
    } else {
        ComPtr<ID2D1SolidColorBrush> brush;
        d2dContext_->CreateSolidColorBrush(
            D2D1::ColorF(1.0f, 1.0f, 1.0f, opacity), &brush);

        constexpr float kRadius = 10.0f;
        D2D1_ELLIPSE ellipse = D2D1::Ellipse(
            D2D1::Point2F(position.x, position.y), kRadius, kRadius);
        d2dContext_->FillEllipse(ellipse, brush.Get());
    }
}

void Renderer::RenderGhostAt(Point position, Point blurTrailStart,
                              float rotationRadians, ID2D1Bitmap* shapeBitmap,
                              Point hotspot) {
    if (!d2dContext_) {
        return;
    }

    d2dContext_->BeginDraw();
    d2dContext_->Clear(D2D1::ColorF(0, 0, 0, 0));  // fully transparent

    PushTrailPoint(position);
    DrawTrail(rotationRadians, shapeBitmap, hotspot);

    for (int i = 1; i <= kBlurSubSteps; ++i) {
        float t = static_cast<float>(i) / (kBlurSubSteps + 1);
        Point interp{
            blurTrailStart.x + (position.x - blurTrailStart.x) * t,
            blurTrailStart.y + (position.y - blurTrailStart.y) * t};
        DrawShapeAt(interp, rotationRadians, shapeBitmap, hotspot,
                    kBlurMaxAlpha * blurIntensityMultiplier_ * t);
    }

    DrawShapeAt(position, rotationRadians, shapeBitmap, hotspot,
                kGhostOpacity);

    d2dContext_->SetTransform(D2D1::Matrix3x2F::Identity());
    d2dContext_->EndDraw();
    swapChain_->Present(1, 0);
}

}  // namespace renderer_d2d
