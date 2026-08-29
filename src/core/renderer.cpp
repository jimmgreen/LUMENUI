#include "renderer.h"
#include <wrl/client.h>  // 仅用 IID_PPV_ARGS 辅助

namespace fui {

bool Renderer::IsDeviceLost(HRESULT hr) noexcept {
    return hr == D2DERR_RECREATE_TARGET || hr == DXGI_ERROR_DEVICE_REMOVED ||
           hr == DXGI_ERROR_DEVICE_RESET || hr == DXGI_ERROR_DEVICE_HUNG;
}

bool Renderer::Init(HWND hwnd, int width_px, int height_px) {
    hwnd_ = hwnd;
    width_ = width_px;
    height_ = height_px;
    device_lost_ = false;
    ready_ = false;
    ready_ = CreateDeviceResources();
    return ready_;
}

bool Renderer::CreateDeviceResources() {
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    D3D_FEATURE_LEVEL feature_level{};
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, nullptr, 0,
                                   D3D11_SDK_VERSION, &d3d_, &feature_level, nullptr);
    if (FAILED(hr)) {
        hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags, nullptr, 0,
                               D3D11_SDK_VERSION, &d3d_, &feature_level, nullptr);
    }
    if (FAILED(hr)) return false;

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
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Scaling = DXGI_SCALING_STRETCH;
    // 组合交换链要求 PREMULTIPLIED（内容不透明时呈现效果与 IGNORE 相同）
    desc.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
    hr = factory->CreateSwapChainForComposition(d3d_.get(), &desc, nullptr, &swapchain_);
    if (FAILED(hr)) return false;
    factory->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_ALT_ENTER);
    dxgi_->SetMaximumFrameLatency(1);

    hr = DCompositionCreateDevice(dxgi_.get(), IID_PPV_ARGS(&comp_));
    if (FAILED(hr)) return false;
    hr = comp_->CreateTargetForHwnd(hwnd_, TRUE, &comp_target_);
    if (FAILED(hr)) return false;
    if (FAILED(comp_->CreateVisual(&comp_visual_))) return false;
    comp_visual_->SetContent(swapchain_.get());
    comp_target_->SetRoot(comp_visual_.get());
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
    dc_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    return true;
}

bool Renderer::CreateTargetBitmap() {
    ComPtr<IDXGISurface> surface;
    if (FAILED(swapchain_->GetBuffer(0, IID_PPV_ARGS(&surface)))) return false;
    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED), 96.0f,
        96.0f);
    if (FAILED(dc_->CreateBitmapFromDxgiSurface(surface.get(), &props, &target_))) return false;
    dc_->SetTarget(target_.get());
    return true;
}

void Renderer::ReleaseDeviceResources() {
    ready_ = false;
    target_.reset();
    dc_.reset();
    d2d_device_.reset();
    d2d_factory_.reset();
    comp_visual_.reset();
    comp_target_.reset();
    comp_.reset();
    swapchain_.reset();
    dxgi_.reset();
    d3d_.reset();
}

void Renderer::Shutdown() {
    ReleaseDeviceResources();
    hwnd_ = nullptr;
}

void Renderer::Resize(int width_px, int height_px) {
    if (!swapchain_ || width_px <= 0 || height_px <= 0) return;
    if (width_px == width_ && height_px == height_ && target_) return;   // 同尺寸：无需重建
    width_ = width_px;
    height_ = height_px;
    target_.reset();
    const HRESULT hr = swapchain_->ResizeBuffers(0, static_cast<UINT>(width_px),
                                                 static_cast<UINT>(height_px),
                                                 DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
        device_lost_ = true;
        return;
    }
    if (!CreateTargetBitmap()) device_lost_ = true;
}

ID2D1DeviceContext2* Renderer::BeginDraw() {
    if (!dc_) return nullptr;
    dc_->BeginDraw();
    return dc_.get();
}

bool Renderer::EndDraw() {
    if (!dc_) return false;
    HRESULT hr = dc_->EndDraw();
    if (FAILED(hr)) {
        if (IsDeviceLost(hr)) {
            device_lost_ = true;
            return false;
        }
        // 非设备错误：仍尝试呈现
    }
    // 组合交换链同样需要 Present 把后备缓冲交给合成器，Commit 只提交视觉树。
    if (swapchain_) swapchain_->Present(1, 0);
    comp_->Commit();
    return true;
}

bool Renderer::Recover() {
    ReleaseDeviceResources();
    return Init(hwnd_, width_, height_);
}

} // namespace fui
