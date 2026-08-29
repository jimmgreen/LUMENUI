// renderer.h — 每窗口渲染设备：D3D11 → D2D 目标位图，DirectComposition 呈现通道。
#pragma once
#include "com_ptr.h"
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <dcomp.h>
#include <d2d1_3.h>

namespace fui {

class Renderer {
public:
    bool Init(HWND hwnd, int width_px, int height_px);
    void Shutdown();
    void Resize(int width_px, int height_px);

    ID2D1DeviceContext2* BeginDraw();
    bool EndDraw();   // 提交 DComp；设备丢失时返回 false 并置 NeedsRecovery
    bool NeedsRecovery() const noexcept { return device_lost_; }
    bool Ready() const noexcept { return ready_; }
    bool Recover();   // 重建全部设备资源

    int Width() const noexcept { return width_; }
    int Height() const noexcept { return height_; }

private:
    bool CreateDeviceResources();
    void ReleaseDeviceResources();
    bool CreateTargetBitmap();
    static bool IsDeviceLost(HRESULT hr) noexcept;

    HWND hwnd_ = nullptr;
    int width_ = 0, height_ = 0;
    bool device_lost_ = false;
    bool ready_ = false;

    ComPtr<ID3D11Device> d3d_;
    ComPtr<IDXGIDevice1> dxgi_;
    ComPtr<IDXGISwapChain1> swapchain_;
    ComPtr<IDCompositionDevice> comp_;
    ComPtr<IDCompositionTarget> comp_target_;
    ComPtr<IDCompositionVisual> comp_visual_;
    ComPtr<ID2D1Factory2> d2d_factory_;
    ComPtr<ID2D1Device1> d2d_device_;
    ComPtr<ID2D1DeviceContext2> dc_;
    ComPtr<ID2D1Bitmap1> target_;
};

} // namespace fui
