// lumen/MenuBar.h — 窗口菜单栏：File/Edit… 标题行，点击弹出对应菜单（复用 Menu 弹层）。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include "Menu.h"
#include <string>
#include <vector>

namespace lumen {

class MenuBar : public ControlOf<MenuBar> {
public:
    MenuBar() = default;

    // 追加一个顶级菜单（拷贝持有；item 的 action 在选中时回调）。
    MenuBar& AddMenu(std::wstring_view title, Menu menu);
    size_t Count() const noexcept { return menus_.size(); }
    // Alt+助记键：匹配标题里的 &X。成功则弹出对应菜单。
    bool ActivateMnemonic(uint32_t vk);

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    AutomationControlType AutomationType() const noexcept override {
        return AutomationControlType::MenuBar;
    }
    void Draw(Painter& painter, const Theme& theme) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseLeave() override;
    CursorShape CursorAt(Point local) const override;

private:
    int TitleAt(float x) const;   // -1 无
    void OpenMenu(int index);

    std::vector<std::wstring> titles_;
    std::vector<Menu> menus_;
    std::vector<float> title_x_;      // Measure 时重建的布局缓存（绘制只读）
    std::vector<float> title_w_;
    float text_h_ = 16.0f;            // 标题行盒高（Measure 缓存，绘制期不做文本测量）
    int hover_ = -1;
};

} // namespace lumen
