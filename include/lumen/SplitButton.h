// lumen/SplitButton.h — 分割按钮：主操作区 + 下拉箭头区。
// Events: OnClick / BindClick / OnDropdown / BindDropdown / OnToggled / BindToggled
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include "Menu.h"
#include "Signal.h"
#include <functional>
#include <optional>
#include <string>

namespace lumen {

class SplitButton : public ControlOf<SplitButton> {
public:
    SplitButton() = default;
    explicit SplitButton(std::wstring_view text, bool primary = false)
        : text_(text), primary_(primary) {}

    SplitButton& Text(std::wstring_view value) { text_ = value; RelayoutParent(); return *this; }
    const std::wstring& Text() const noexcept { return text_; }
    SplitButton& Primary(bool value) { primary_ = value; Invalidate(); return *this; }
    // 主区左侧小圆点（状态指示，如"发布就绪"）。
    SplitButton& StatusDot(Color color) {
        dot_color_ = color;
        has_dot_ = true;
        RelayoutParent();
        return *this;
    }

    SplitButton& OnClick(std::function<void()> handler) {
        click_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindClick(std::function<void()> handler) { return click_.Connect(std::move(handler)); }
    // 点击箭头区时回调（用 AbsoluteBounds 计算弹出位置）。
    SplitButton& OnDropdown(std::function<void()> handler) {
        dropdown_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindDropdown(std::function<void()> handler) {
        return dropdown_.Connect(std::move(handler));
    }
    // 一行式：箭头区弹出该菜单（自动锚定到按钮底边）。可与 OnDropdown 并存。
    SplitButton& DropdownMenu(Menu menu) {
        dropdown_menu_ = std::move(menu);
        dropdown_menu_bind_ = dropdown_.Connect([this] {
            if (!dropdown_menu_) return;
            Menu popup = *dropdown_menu_;   // 拷贝弹出：叶子 action 里改菜单不影响原配置
            popup.PopupTo(*this);
        });
        return *this;
    }
    // 开关模式：主区点击切换选中态并触发 OnToggled（click_ 不再触发）。
    SplitButton& Toggle(bool value) {
        toggle_ = value;
        if (!toggle_) checked_ = false;
        RelayoutParent();
        return *this;
    }
    bool Toggle() const noexcept { return toggle_; }
    SplitButton& Checked(bool value) {
        if (!toggle_ || checked_ == value) return *this;
        checked_ = value;
        RelayoutParent();
        return *this;
    }
    bool Checked() const noexcept { return checked_; }
    SplitButton& OnToggled(std::function<void(bool)> handler) {
        toggled_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindToggled(std::function<void(bool)> handler) {
        return toggled_.Connect(std::move(handler));
    }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool Focusable() const noexcept override { return true; }
    AutomationControlType AutomationType() const noexcept override {
        return AutomationControlType::SplitButton;
    }
    uint32_t AutomationPatterns() const noexcept override {
        return kPatternInvoke | (toggle_ ? kPatternToggle : 0u);
    }
    std::wstring AutomationName() const override {
        return accessible_name_.empty() ? text_ : accessible_name_;
    }
    bool AutomationInvoke() override {
        if (!enabled_) return false;
        if (toggle_) ToggleFromUser();
        else click_.Emit();
        return true;
    }
    int AutomationToggleState() const noexcept override {
        if (!toggle_) return -1;
        return checked_ ? 1 : 0;
    }
    bool AutomationToggle() override {
        if (!enabled_ || !toggle_) return false;
        ToggleFromUser();
        return true;
    }
    bool OnKey(uint32_t vk) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseUp(Point local, uint32_t buttons) override;
    void OnMouseEnter() override;
    void OnMouseLeave() override;
    void OnFocusChanged(bool focused) override;
    bool OnAnimate(float dt_seconds) override;

    void RelayoutParent();
    void ToggleFromUser();

    std::wstring text_;
    bool primary_ = false;
    bool toggle_ = false;
    bool checked_ = false;
    bool has_dot_ = false;
    Color dot_color_{0.0f, 0.0f, 0.0f, 0.0f};
    bool arrow_pressed_ = false;
    float glow_t_ = 0.0f;        // 悬停辉光 0..1
    Signal<> click_;
    Signal<> dropdown_;
    Signal<bool> toggled_;
    std::optional<Menu> dropdown_menu_;
    Connection dropdown_menu_bind_;
};

} // namespace lumen
