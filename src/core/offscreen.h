// offscreen.h — 离屏渲染器：无窗口绘制到 D2D 位图，用于视觉回归与性能基准。
#pragma once
#include "com_ptr.h"
#include "fluentui/Core.h"
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <d2d1_3.h>
#include <wincodec.h>
#include <vector>

namespace fui {

class OffscreenRenderer {
public:
    bool Init(int width_px, int height_px);
    void Shutdown();

    ID2D1DeviceContext2* BeginDraw();
    bool EndDraw();
    bool SavePNG(const wchar_t* path);   // 需先 CoInitializeEx
    bool ReadPixel(int x, int y, Color& out);

    int Width() const noexcept { return width_; }
    int Height() const noexcept { return height_; }

private:
    bool CreateTarget();
    bool ReadBack(std::vector<uint8_t>& bgra);

    int width_ = 0, height_ = 0;
    ComPtr<ID3D11Device> d3d_;
    ComPtr<IDXGIDevice1> dxgi_;
    ComPtr<ID2D1Factory2> d2d_factory_;
    ComPtr<ID2D1Device1> d2d_device_;
    ComPtr<ID2D1DeviceContext2> dc_;
    ComPtr<ID2D1Bitmap1> target_;
    ComPtr<ID3D11Texture2D> texture_;
    ComPtr<ID3D11Texture2D> staging_;
    ComPtr<IWICImagingFactory> wic_;
};

} // namespace fui
