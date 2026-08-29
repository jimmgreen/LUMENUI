// menu_window.h — 弹出菜单窗口：Menu 与 ComboBox 下拉共用的模态弹层。
#pragma once
#include "fluentui/Core.h"
#include "fluentui/Theme.h"
#include <windows.h>
#include <cstdint>
#include <memory>
#include <vector>

namespace fui {

struct MenuItem;
class Renderer;
class Painter;

class MenuWindow {
public:
    // screen_px：屏幕物理像素坐标（期望的弹层左上角，自动收进工作区）。
    // 返回被点击项索引（跳过分隔符），未选择返回 -1。
    static int Show(HWND owner, const std::vector<MenuItem>& items, POINT screen_px,
                    const Theme& theme, float scale);

private:
    MenuWindow(const std::vector<MenuItem>& items, const Theme& theme, float scale);
    int Run(HWND owner, POINT screen_px);
    void ComputeLayout();
    void Paint();
    void SetHover(int row);
    void CommitRow(int row);
    void Dismiss(int result);

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT Handle(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

    const std::vector<MenuItem>& items_;
    const Theme& theme_;
    float scale_;
    HWND hwnd_ = nullptr;
    std::unique_ptr<Renderer> renderer_;
    int width_px_ = 0, height_px_ = 0;
    float width_dip_ = 0.0f, height_dip_ = 0.0f;
    std::vector<int> row_item_;   // 视觉行 → items_ 下标（-1 为分隔符）
    int hover_row_ = -1;
    int press_row_ = -1;
    int result_ = -1;
    bool dismissed_ = false;
};

} // namespace fui
