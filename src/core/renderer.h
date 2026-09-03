// renderer.h — 每窗口渲染设备：D3D11 → D2D 目标位图，DirectComposition 呈现通道。
#pragma once
#include "com_ptr.h"
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <dcomp.h>
#include <d2d1_3.h>
#include <memory>

namespace lumen {

class LumaTextBridge;

class Renderer {
public:
    Renderer();
    ~Renderer();   // unique_ptr<LumaTextBridge> 需要完整类型，析构在 cpp 中定义
    bool Init(HWND hwnd, int width_px, int height_px);
    void Shutdown();
    void Resize(int width_px, int height_px);

    ID2D1DeviceContext2* BeginDraw();
    bool EndDraw(bool wait_vsync = true);  // 提交 DComp；设备丢失时返回 false 并置 NeedsRecovery
    bool EndDraw(bool wait_vsync, const RECT* dirty, UINT dirty_count);
    bool NeedsRecovery() const noexcept { return device_lost_; }
    bool Ready() const noexcept { return ready_; }
    bool Recover();   // 重建全部设备资源
    LumaTextBridge* Luma() noexcept { return luma_.get(); }
    // 合成器侧 2D 变换（物理像素）。弹层出现动画用：内容仍按恒等绘制，避免 LumaText 切路径。
    void SetVisualTransform(const D2D1_MATRIX_3X2_F& matrix);

    // 弹层打开期间宿主不要 DwmFlush，避免 DComp 把菜单压回下面。
    static void FlyoutEnter();
    static void FlyoutLeave();
    static bool FlyoutOpen() noexcept;

    int Width() const noexcept { return width_; }
    int Height() const noexcept { return height_; }

private:
    bool CreateDeviceResources();
    void ReleaseDeviceResources();
    bool CreateTargetBitmap();
    bool EnsureRetain();
    bool BlitRetainToSwapchain(const RECT* dirty, UINT dirty_count);
    static bool IsDeviceLost(HRESULT hr) noexcept;
    static LONG flyout_depth_;

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
    ComPtr<ID2D1Bitmap1> retain_;
    std::unique_ptr<LumaTextBridge> luma_;
};

} // namespace lumen

#include "lumen/win_undef.h"
