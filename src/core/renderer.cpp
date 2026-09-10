#include "renderer.h"
#include "log.h"
#include "lumatext_bridge.h"
#include "text_service.h"
#include "lumen/Core.h"
#include "lumen/App.h"
#include <dwrite.h>
#include <dwmapi.h>
#include <wrl/client.h>  // 仅用 IID_PPV_ARGS 辅助
#include <algorithm>
#include <cmath>

namespace lumen {

LONG Renderer::flyout_depth_ = 0;

Renderer::Renderer() = default;
Renderer::~Renderer() { StopFrameTimer(); }

void Renderer::StopFrameTimer() {
    if (frame_timer_armed_ && hwnd_) KillTimer(hwnd_, kFrameTimerId);
    frame_timer_armed_ = false;
}

void Renderer::RequestHostFrame() {
    if (!hwnd_ || !IsWindowVisible(hwnd_) || IsIconic(GetAncestor(hwnd_, GA_ROOT))) {
        StopFrameTimer();
        return;
    }
    if (frame_timer_armed_) return;
    const ULONGLONG now = GetTickCount64();
    const UINT delay = next_frame_ms_ > now ? static_cast<UINT>(next_frame_ms_ - now) : 1u;
    frame_timer_armed_ = SetTimer(hwnd_, kFrameTimerId, delay, nullptr) != 0;
}

bool Renderer::DeferHostFrame() {
    if (!App::HostMode()) return false;
    if (!hwnd_ || !IsWindowVisible(hwnd_) || IsIconic(GetAncestor(hwnd_, GA_ROOT))) {
        StopFrameTimer();
        next_frame_ms_ = 0;
        return true;
    }
    const ULONGLONG now = GetTickCount64();
    if (now < next_frame_ms_) { RequestHostFrame(); return true; }
    StopFrameTimer();
    next_frame_ms_ = now + kHostFrameIntervalMs;
    return false;
}

bool Renderer::HandleFrameTimer(UINT_PTR id) {
    if (id != kFrameTimerId) return false;
    StopFrameTimer();
    if (hwnd_ && IsWindowVisible(hwnd_) && !IsIconic(GetAncestor(hwnd_, GA_ROOT)))
        InvalidateRect(hwnd_, nullptr, FALSE);
    return true;
}

void Renderer::FlyoutEnter() { InterlockedIncrement(&flyout_depth_); }
void Renderer::FlyoutLeave() { InterlockedDecrement(&flyout_depth_); }
bool Renderer::FlyoutOpen() noexcept { return flyout_depth_ > 0; }

bool Renderer::IsDeviceLost(HRESULT hr) noexcept {
    return hr == D2DERR_RECREATE_TARGET || hr == DXGI_ERROR_DEVICE_REMOVED ||
           hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_HUNG;
}

bool Renderer::Init(HWND hwnd, int width_px, int height_px, HWND composition_hwnd) {
    // 重入（Recover、或 CreateWindow 期间的首次 Paint 抢在构造函数 Init 之前）必须先放掉
    // 上一套设备链，否则窗口打开瞬间同时挂两套 D3D/DComp 设备，显存吃紧时第二套
    // D3D11CreateDevice 会 E_OUTOFMEMORY 退化到 WARP。
    ReleaseDeviceResources();
    hwnd_ = hwnd;
    composition_hwnd_ = composition_hwnd ? composition_hwnd : hwnd;
    width_ = width_px;
    height_ = height_px;
    device_lost_ = false;
    // 没有 HWND 或客户区为空时建不出 DComp 目标 / 保留位图，不要白建整套设备。
    if (!hwnd_ || width_ <= 0 || height_ <= 0) return false;
    ready_ = CreateDeviceResources();
    if (!ready_) ReleaseDeviceResources();
    return ready_;
}

bool Renderer::CreateDeviceResources() {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL feature_level{};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
                                   D3D11_SDK_VERSION, &d3d_, &feature_level, nullptr);
    bool warp = false;
    const HRESULT hardware_hr = hr;
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0,
                               D3D11_SDK_VERSION, &d3d_, &feature_level, nullptr);
        warp = SUCCEEDED(hr);
    }
    if (FAILED(hr)) return false;
    if (warp) {
        Log(LogLevel::Warn, L"D3D11 device is WARP (hardware hr=0x%08lx)",
            static_cast<unsigned long>(hardware_hr));
    }

    if (FAILED(d3d_->QueryInterface(IID_PPV_ARGS(&dxgi_)))) return false;

    ComPtr<IDXGIAdapter> adapter;
    if (FAILED(dxgi_->GetAdapter(&adapter))) return false;
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(adapter->GetParent(IID_PPV_ARGS(&factory)))) return false;

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = static_cast<UINT>(width_ > 0 ? width_ : 1);
    desc.Height = static_cast<UINT>(height_ > 0 ? height_ : 1);
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    // Present1 脏区要求后缓冲保留上一帧；FLIP_DISCARD 会丢掉未更新的像素。
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    desc.Scaling = DXGI_SCALING_STRETCH;
    // 组合交换链要求 PREMULTIPLIED（内容不透明时呈现效果与 IGNORE 相同）
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    hr = factory->CreateSwapChainForComposition(d3d_.get(), &desc, nullptr, &swapchain_);
    if (FAILED(hr)) return false;
    factory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
    dxgi_->SetMaximumFrameLatency(1);

    hr = DCompositionCreateDevice(dxgi_.get(), IID_PPV_ARGS(&comp_));
    if (FAILED(hr)) return false;
    hr = comp_->CreateTargetForHwnd(composition_hwnd_, TRUE, &comp_target_);
    if (FAILED(hr)) {
        Log(LogLevel::Error, L"Renderer composition target creation failed input=%p target=%p hr=0x%08lX",
            hwnd_, composition_hwnd_, hr);
        return false;
    }
    if (FAILED(comp_->CreateVisual(&comp_visual_))) return false;
    comp_visual_->SetContent(swapchain_.get());
    if (!UpdateCornerClip()) return false;
    const bool visible = composition_hwnd_ == hwnd_ || (GetWindowLongPtrW(hwnd_, GWL_STYLE) & WS_VISIBLE);
    comp_target_->SetRoot(visible ? comp_visual_.get() : nullptr);
    comp_->Commit();

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&d2d_factory_))))
        return false;
    hr = d2d_factory_->CreateDevice(dxgi_.get(), &d2d_device_);
    if (FAILED(hr)) return false;
    ComPtr<ID2D1DeviceContext> context;
    if (FAILED(d2d_device_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context)))
        return false;
    if (FAILED(context->QueryInterface(IID_PPV_ARGS(&dc_)))) return false;
    const bool bmp_ok = CreateTargetBitmap();
    if (!bmp_ok) return false;

    dc_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    dc_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    if (IDWriteRenderingParams* params = UiText().GrayscaleParams()) {
        dc_->SetTextRenderingParams(params);
    }

    if (!luma_) luma_ = std::make_unique<LumaTextBridge>();
    if (!luma_->Init(UiText().Factory(), dc_.get())) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            Log(LogLevel::Warn, L"LumaText unavailable; using DirectWrite");
        }
    }
    return true;
}

bool Renderer::CreateTargetBitmap() {
    if (!dc_ || !swapchain_) return false;
    dc_->SetTarget(nullptr);
    target_.reset();
    retain_.reset();
    ComPtr<IDXGISurface> surface;
    if (FAILED(swapchain_->GetBuffer(0, IID_PPV_ARGS(&surface)))) return false;
    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f,
        96.0f);
    if (FAILED(dc_->CreateBitmapFromDxgiSurface(surface.get(), &props, &target_))) return false;
    return EnsureRetain();
}

bool Renderer::EnsureRetain() {
    if (!dc_ || width_ <= 0 || height_ <= 0) return false;
    if (retain_) {
        const D2D1_SIZE_U size = retain_->GetPixelSize();
        if (static_cast<int>(size.width) == width_ && static_cast<int>(size.height) == height_) {
            return true;
        }
        retain_.reset();
    }
    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f,
        96.0f);
    const D2D1_SIZE_U px{static_cast<UINT32>(width_), static_cast<UINT32>(height_)};
    return SUCCEEDED(dc_->CreateBitmap(px, nullptr, 0, props, &retain_));
}

void Renderer::ReleaseDeviceResources() {
    present_pending_ = false;
    ready_ = false;
    if (luma_) luma_->Shutdown();
    if (dc_) dc_->SetTarget(nullptr);
    target_.reset();
    retain_.reset();
    dc_.reset();
    d2d_device_.reset();
    d2d_factory_.reset();
    if (comp_target_) comp_target_->SetRoot(nullptr);
    if (comp_) comp_->Commit();
    comp_visual_.reset();
    corner_clip_.reset();
    comp_target_.reset();
    comp_.reset();
    swapchain_.reset();
    dxgi_.reset();
    d3d_.reset();
}

void Renderer::Shutdown() {
    StopFrameTimer();
    ReleaseDeviceResources();
    hwnd_ = nullptr;
    composition_hwnd_ = nullptr;
}

void Renderer::SetCompositionVisible(bool visible) {
    // 内容挂在父窗后不会随子窗自动隐藏，显式同步；计时器和输入仍归子窗。
    if (composition_hwnd_ != hwnd_ && comp_target_ && comp_visual_ && comp_) {
        comp_target_->SetRoot(visible ? comp_visual_.get() : nullptr);
        comp_->Commit();
    }
}

bool Renderer::SetCornerRadius(float radius_px) {
    const float radius = std::isfinite(radius_px) ? std::max(0.0f, radius_px) : 0.0f;
    if (corner_radius_ == radius) return true;
    corner_radius_ = radius;
    return UpdateCornerClip();
}

bool Renderer::UpdateCornerClip() {
    if (!comp_ || !comp_visual_) return false;
    HRESULT hr = S_OK;
    if (corner_radius_ <= 0.0f) {
        hr = comp_visual_->SetClip(static_cast<IDCompositionClip*>(nullptr));
    } else {
        if (!corner_clip_) hr = comp_->CreateRectangleClip(&corner_clip_);
        const float radius = std::min(corner_radius_, std::min(width_, height_) * 0.5f);
        if (SUCCEEDED(hr)) hr = corner_clip_->SetLeft(0.0f);
        if (SUCCEEDED(hr)) hr = corner_clip_->SetTop(0.0f);
        if (SUCCEEDED(hr)) hr = corner_clip_->SetRight(static_cast<float>(width_));
        if (SUCCEEDED(hr)) hr = corner_clip_->SetBottom(static_cast<float>(height_));
        if (SUCCEEDED(hr)) hr = corner_clip_->SetTopLeftRadiusX(radius);
        if (SUCCEEDED(hr)) hr = corner_clip_->SetTopLeftRadiusY(radius);
        if (SUCCEEDED(hr)) hr = corner_clip_->SetTopRightRadiusX(radius);
        if (SUCCEEDED(hr)) hr = corner_clip_->SetTopRightRadiusY(radius);
        if (SUCCEEDED(hr)) hr = corner_clip_->SetBottomLeftRadiusX(radius);
        if (SUCCEEDED(hr)) hr = corner_clip_->SetBottomLeftRadiusY(radius);
        if (SUCCEEDED(hr)) hr = corner_clip_->SetBottomRightRadiusX(radius);
        if (SUCCEEDED(hr)) hr = corner_clip_->SetBottomRightRadiusY(radius);
        if (SUCCEEDED(hr)) hr = comp_visual_->SetClip(corner_clip_.get());
    }
    if (SUCCEEDED(hr)) hr = comp_->Commit();
    if (FAILED(hr))
        Log(LogLevel::Error, L"Renderer corner clip failed target=%p size=%dx%d radiusPx=%.2f hr=0x%08lX",
            composition_hwnd_, width_, height_, corner_radius_, hr);
    return SUCCEEDED(hr);
}

void Renderer::Resize(int width_px, int height_px) {
    if (!swapchain_ || !dc_ || width_px <= 0 || height_px <= 0) return;
    if (width_px == width_ && height_px == height_ && target_) return;   // 同尺寸：无需重建
    width_ = width_px;
    height_ = height_px;
    if (!UpdateCornerClip()) device_lost_ = true;
    // 目标位图仍绑在 DC 上时 ResizeBuffers 会失败或丢掉后备缓冲，客户区变空。
    dc_->SetTarget(nullptr);
    target_.reset();
    retain_.reset();
    present_pending_ = false;
    const HRESULT hr = swapchain_->ResizeBuffers(0, static_cast<UINT>(width_px),
                                                 static_cast<UINT>(height_px),
                                                 DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        Log(L"ResizeBuffers failed hr=0x%08X size=%dx%d", static_cast<unsigned>(hr), width_px,
            height_px);
        device_lost_ = true;
        return;
    }
    if (!CreateTargetBitmap()) {
        Log(L"CreateTargetBitmap failed after resize size=%dx%d", width_px, height_px);
        device_lost_ = true;
    }
}

ID2D1DeviceContext2* Renderer::BeginDraw() {
    if (!dc_ || !target_) return nullptr;
    if (!EnsureRetain()) return nullptr;
    dc_->SetTarget(retain_.get());
    dc_->BeginDraw();
    return dc_.get();
}

bool Renderer::BlitRetainToSwapchain(const RECT* dirty, UINT dirty_count) {
    if (!dc_ || !target_ || !retain_) return false;
    // CopyFromBitmap 在位图仍是 DC 目标时会空操作。
    dc_->SetTarget(nullptr);
    auto copy_rect = [this](const RECT* r) -> HRESULT {
        D2D1_POINT_2U dest{0, 0};
        const D2D1_RECT_U* src = nullptr;
        D2D1_RECT_U src_box{};
        if (r) {
            dest.x = static_cast<UINT32>(r->left);
            dest.y = static_cast<UINT32>(r->top);
            src_box = {static_cast<UINT32>(r->left), static_cast<UINT32>(r->top),
                       static_cast<UINT32>(r->right), static_cast<UINT32>(r->bottom)};
            src = &src_box;
        }
        return target_->CopyFromBitmap(&dest, retain_.get(), src);
    };
    HRESULT hr = S_OK;
    UINT copied = 0;
    if (dirty && dirty_count > 0) {
        for (UINT i = 0; i < dirty_count; ++i) {
            const RECT& r = dirty[i];
            if (r.right <= r.left || r.bottom <= r.top) continue;
            hr = copy_rect(&r);
            if (FAILED(hr)) break;
            ++copied;
        }
    }
    if (copied == 0) hr = copy_rect(nullptr);
    if (FAILED(hr)) {
        if (IsDeviceLost(hr)) {
            device_lost_ = true;
            return false;
        }
        dc_->SetTarget(target_.get());
        dc_->BeginDraw();
        dc_->SetTransform(D2D1::Matrix3x2F::Identity());
        const D2D1_SIZE_F px = retain_->GetSize();
        const D2D1_RECT_F dest{0.0f, 0.0f, px.width, px.height};
        dc_->DrawBitmap(retain_.get(), dest, 1.0f, D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, &dest);
        hr = dc_->EndDraw();
        if (FAILED(hr) && IsDeviceLost(hr)) {
            device_lost_ = true;
            return false;
        }
    }
    return true;
}

bool Renderer::EndDraw(bool wait_vsync) { return EndDraw(wait_vsync, nullptr, 0); }

bool Renderer::EndDraw(bool wait_vsync, const RECT* dirty, UINT dirty_count) {
    if (!dc_) return false;
    HRESULT hr = dc_->EndDraw();
    if (FAILED(hr)) {
        if (IsDeviceLost(hr)) {
            device_lost_ = true;
            return false;
        }
    }
    // A deferred frame lives in retain_. Include all earlier dirty pixels in the retry.
    if (present_pending_) { dirty = nullptr; dirty_count = 0; }
    RECT used[kMaxDirtyRects]{};
    UINT used_n = 0;
    if (dirty && dirty_count > 0) {
        const UINT cap = dirty_count < static_cast<UINT>(kMaxDirtyRects)
                             ? dirty_count
                             : static_cast<UINT>(kMaxDirtyRects);
        for (UINT i = 0; i < cap; ++i) {
            const RECT& r = dirty[i];
            if (r.right <= r.left || r.bottom <= r.top) continue;
            used[used_n++] = r;
        }
    }
    const RECT* blit_dirty = used_n > 0 ? used : nullptr;
    if (!BlitRetainToSwapchain(blit_dirty, used_n)) return false;
    if (swapchain_) {
        const bool host = App::HostMode();
        const UINT sync = host ? 0u : (wait_vsync ? 1u : 0u);
        const UINT flags = host ? DXGI_PRESENT_DO_NOT_WAIT : 0u;
        HRESULT presented = E_FAIL;
        if (used_n > 0) {
            DXGI_PRESENT_PARAMETERS params{};
            params.DirtyRectsCount = used_n;
            params.pDirtyRects = used;
            presented = swapchain_->Present1(sync, flags, &params);
            if (FAILED(presented) && presented != DXGI_ERROR_WAS_STILL_DRAWING && !IsDeviceLost(presented)) {
                if (!BlitRetainToSwapchain(nullptr, 0)) return false;
                presented = swapchain_->Present(sync, flags);
            }
        } else {
            presented = swapchain_->Present(sync, flags);
        }
        if (FAILED(presented)) {
            present_pending_ = true;
            if (IsDeviceLost(presented)) device_lost_ = true;
            return false;
        }
        present_pending_ = false;
    }
    if (comp_) comp_->Commit();
    if (wait_vsync && !App::HostMode()) DwmFlush();
    return true;
}

void Renderer::SetVisualTransform(const D2D1_MATRIX_3X2_F& matrix) {
    if (comp_visual_) comp_visual_->SetTransform(matrix);
}

bool Renderer::Recover() {
    return Init(hwnd_, width_, height_, composition_hwnd_);
}

} // namespace lumen
