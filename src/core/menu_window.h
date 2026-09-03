// menu_window.h — 弹出菜单窗口：Menu 与 ComboBox 下拉共用的模态弹层。
#pragma once
#include "lumen/Core.h"
#include "lumen/Theme.h"
#include <windows.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace lumen {

struct MenuItem;
class Renderer;
class Painter;

class MenuWindow {
public:
    // screen_px：屏幕物理像素坐标（期望的弹层左上角，自动收进工作区）。
    // min_width_dip：下拉与触发器等宽（0 表示按内容）。
    // 返回被点击项索引（跳过分隔符），未选择返回 -1。
    static int Show(HWND owner, std::vector<MenuItem>& items, POINT screen_px,
                    const Theme& theme, float scale, float min_width_dip = 0.0f,
                    std::function<bool(wchar_t)> on_char = nullptr);
    ~MenuWindow();

private:
    MenuWindow(std::vector<MenuItem>& items, const Theme& theme, float scale,
               float min_width_dip);
    int Run(HWND owner, POINT screen_px);
    bool CreatePopup(HWND owner, POINT screen_px);
    void DestroyPopup();
    void ComputeLayout();
    void Reload();   // 过滤后按新 items_ 重排并改窗口尺寸
    void Paint();
    int RowAt(float x_dip, float y_dip) const;
    bool ContainsScreenPx(POINT screen_px) const;
    bool ContainsCascadePx(POINT screen_px) const;
    MenuWindow* Root() noexcept;
    MenuWindow* HitDeepest(POINT screen_px);
    void ToClientDip(POINT screen_px, float& x, float& y) const;
    void RoutePointer(POINT screen_px, bool move, bool down, bool up);
    void HandleKey(WPARAM vk);
    void SetHover(int row, bool immediate = false);
    void SyncSubmenu(bool immediate);
    void CommitRow(int row);
    void Dismiss(int result, const wchar_t* reason);
    void TickAppear();
    void ClampScroll();
    void ScrollBy(float delta);
    bool MatchShortcut(uint32_t vk);
    bool MatchMnemonic(wchar_t ch);
    void OpenSubmenu(int row);
    void CloseSubmenu();
    void ArmOpenTimer(int row);
    void ArmCloseTimer();
    void KillSubmenuTimers();
    void OnSubmenuTimer(UINT_PTR id);
    bool InSafeTriangle(POINT screen_px) const;
    float RowHeightAt(size_t row) const noexcept;
    bool RowInteractive(size_t row) const noexcept;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT Handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    std::vector<MenuItem>& items_;
    const Theme& theme_;
    std::function<bool(wchar_t)> on_char_;
    float scale_;
    float min_width_dip_ = 0.0f;
    HWND hwnd_ = nullptr;
    std::unique_ptr<Renderer> renderer_;
    MenuWindow* parent_ = nullptr;
    std::unique_ptr<MenuWindow> child_;
    int child_row_ = -1;
    int width_px_ = 0, height_px_ = 0;
    float width_dip_ = 0.0f, height_dip_ = 0.0f;
    float content_height_dip_ = 0.0f;
    float scroll_y_ = 0.0f;
    float max_scroll_y_ = 0.0f;
    std::vector<int> row_item_;   // 视觉行 → items_ 下标（-1 为分隔符）
    int hover_row_ = -1;
    int press_row_ = -1;
    int result_ = -1;
    int pending_open_row_ = -1;
    bool pending_close_ = false;
    bool dismissed_ = false;
    bool armed_ = false;          // 开层时残留鼠标已排空后再响应；点外面立即关
    bool first_paint_ = true;
    bool any_gutter_ = false;
    POINT last_cursor_{};
    float appear_t_ = 0.0f;       // 展开 0..1；缩放走 DComp 视觉树，绘制保持恒等
    LONGLONG appear_qpc_ = 0;
    LONGLONG run_qpc_ = 0;
};

} // namespace lumen
