#include "offscreen.h"
#include <cstring>
#include <objbase.h>
#include <vector>

namespace lumen {

bool OffscreenRenderer::Init(int width_px, int height_px) {
    width_ = width_px;
    height_ = height_px;
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
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, IID_PPV_ARGS(&d2d_factory_))))
        return false;
    if (FAILED(d2d_factory_->CreateDevice(dxgi_.get(), &d2d_device_))) return false;
    ComPtr<ID2D1DeviceContext> context;
    if (FAILED(d2d_device_->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &context)))
        return false;
    if (FAILED(context->QueryInterface(IID_PPV_ARGS(&dc_)))) return false;
    if (!CreateTarget()) return false;
    return true;
}

bool OffscreenRenderer::CreateTarget() {
    // 自建 D3D 纹理作为 D2D 目标，读回时直接 CopyResource
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(width_);
    desc.Height = static_cast<UINT>(height_);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET;
    if (FAILED(d3d_->CreateTexture2D(&desc, nullptr, &texture_))) return false;
    ComPtr<IDXGISurface> surface;
    if (FAILED(texture_->QueryInterface(IID_PPV_ARGS(&surface)))) return false;
    D2D1_BITMAP_PROPERTIES1 props = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    if (FAILED(dc_->CreateBitmapFromDxgiSurface(surface.get(), &props, &target_))) return false;
    dc_->SetTarget(target_.get());

    D3D11_TEXTURE2D_DESC staging_desc{};
    staging_desc.Width = static_cast<UINT>(width_);
    staging_desc.Height = static_cast<UINT>(height_);
    staging_desc.MipLevels = 1;
    staging_desc.ArraySize = 1;
    staging_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    staging_desc.SampleDesc.Count = 1;
    staging_desc.Usage = D3D11_USAGE_STAGING;
    staging_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(d3d_->CreateTexture2D(&staging_desc, nullptr, &staging_))) return false;
    return true;
}

void OffscreenRenderer::Shutdown() {
    target_.reset();
    texture_.reset();
    staging_.reset();
    dc_.reset();
    d2d_device_.reset();
    d2d_factory_.reset();
    dxgi_.reset();
    d3d_.reset();
    wic_.reset();
}

ID2D1DeviceContext2* OffscreenRenderer::BeginDraw() {
    if (!dc_) return nullptr;
    dc_->BeginDraw();
    return dc_.get();
}

bool OffscreenRenderer::EndDraw() {
    if (!dc_) return false;
    return SUCCEEDED(dc_->EndDraw());
}

bool OffscreenRenderer::ReadBack(std::vector<uint8_t>& bgra) {
    if (!staging_ || !texture_) return false;
    ComPtr<ID3D11DeviceContext> context;
    d3d_->GetImmediateContext(&context);
    if (!context) return false;
    context->CopyResource(staging_.get(), texture_.get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context->Map(staging_.get(), 0, D3D11_MAP_READ, 0, &mapped))) return false;
    bgra.resize(static_cast<size_t>(width_) * height_ * 4);
    const uint8_t* src = static_cast<const uint8_t*>(mapped.pData);
    for (int y = 0; y < height_; ++y) {
        memcpy(bgra.data() + static_cast<size_t>(y) * width_ * 4,
               src + static_cast<size_t>(y) * mapped.RowPitch,
               static_cast<size_t>(width_) * 4);
    }
    context->Unmap(staging_.get(), 0);
    return true;
}

bool OffscreenRenderer::ReadPixel(int x, int y, Color& out) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return false;
    std::vector<uint8_t> bgra;
    if (!ReadBack(bgra)) return false;
    const uint8_t* px = bgra.data() + (static_cast<size_t>(y) * width_ + x) * 4;
    out = {px[2] / 255.0f, px[1] / 255.0f, px[0] / 255.0f, px[3] / 255.0f};
    return true;
}

bool OffscreenRenderer::SavePNG(const wchar_t* path) {
    if (!wic_) {
        if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&wic_))))
            return false;
    }
    std::vector<uint8_t> bgra;
    if (!ReadBack(bgra)) return false;
    ComPtr<IWICBitmap> bitmap;
    if (FAILED(wic_->CreateBitmapFromMemory(static_cast<UINT>(width_),
                                            static_cast<UINT>(height_),
                                            GUID_WICPixelFormat32bppBGRA,
                                            static_cast<UINT>(width_ * 4),
                                            static_cast<UINT>(bgra.size()), bgra.data(),
                                            &bitmap)))
        return false;
    ComPtr<IWICStream> stream;
    if (FAILED(wic_->CreateStream(&stream))) return false;
    if (FAILED(stream->InitializeFromFilename(path, GENERIC_WRITE))) return false;
    ComPtr<IWICBitmapEncoder> encoder;
    if (FAILED(wic_->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder))) return false;
    if (FAILED(encoder->Initialize(stream.get(), WICBitmapEncoderNoCache))) return false;
    ComPtr<IWICBitmapFrameEncode> frame;
    ComPtr<IPropertyBag2> props;
    if (FAILED(encoder->CreateNewFrame(&frame, &props))) return false;
    if (FAILED(frame->Initialize(props.get()))) return false;
    if (FAILED(frame->SetSize(static_cast<UINT>(width_), static_cast<UINT>(height_)))) return false;
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    if (FAILED(frame->SetPixelFormat(&format))) return false;
    if (FAILED(frame->WritePixels(height_, static_cast<UINT>(width_ * 4),
                                  static_cast<UINT>(bgra.size()), bgra.data()))) return false;
    if (FAILED(frame->Commit())) return false;
    return SUCCEEDED(encoder->Commit());
}

} // namespace lumen
