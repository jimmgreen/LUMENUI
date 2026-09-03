// lumen/DropDownButton.h — 整钮弹出菜单（无主操作 / 箭头分隔）。
// Events: OnDropdown / BindDropdown
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "Button.h"
#include "Menu.h"
#include "ControlOf.h"
#include "Signal.h"
#include <functional>
#include <optional>
#include <string>

namespace lumen {

class DropDownButton : public ControlOf<DropDownButton> {
public:
    DropDownButton() = default;
    explicit DropDownButton(std::wstring_view text, ButtonKind kind = ButtonKind::Standard)
        : text_(text), kind_(kind) {}

    const std::wstring& Text() const noexcept { return text_; }
    DropDownButton& Text(std::wstring_view value) {
        text_ = value;
        RelayoutParent();
        return *this;
    }
    const std::wstring& Glyph() const noexcept { return glyph_; }
    DropDownButton& Glyph(std::wstring_view value) {
        glyph_ = value;
        RelayoutParent();
        return *this;
    }
    ButtonKind Kind() const noexcept { return kind_; }
    DropDownButton& Kind(ButtonKind value) {
        kind_ = value;
        Invalidate();
        return *this;
    }
    ButtonSize SizeClass() const noexcept { return size_; }
    DropDownButton& SizeClass(ButtonSize value) {
        size_ = value;
        RelayoutParent();
        return *this;
    }

    // 整钮点击 / Space / Enter / Down 时回调。
    DropDownButton& OnDropdown(std::function<void()> handler) {
        dropdown_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindDropdown(std::function<void()> handler) {
        return dropdown_.Connect(std::move(handler));
    }
    // 一行式：弹出该菜单（锚定底边）。可与 OnDropdown 并存。
    DropDownButton& DropdownMenu(Menu menu) {
        dropdown_menu_ = std::move(menu);
        dropdown_menu_bind_ = dropdown_.Connect([this] {
            if (!dropdown_menu_) return;
            Menu popup = *dropdown_menu_;
            popup.PopupTo(*this);
        });
        return *this;
    }
    // 弹出；未入窗口树或无下拉时静默。
    void Open();

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool Focusable() const noexcept override { return true; }
    AutomationControlType AutomationType() const noexcept override {
        return AutomationControlType::Button;
    }
    uint32_t AutomationPatterns() const noexcept override { return kPatternExpand; }
    std::wstring AutomationName() const override {
        return accessible_name_.empty() ? text_ : accessible_name_;
    }
    int AutomationExpandState() const noexcept override { return 0; }
    bool AutomationExpand() override {
        if (!enabled_) return false;
        Open();
        return true;
    }
    bool AutomationCollapse() override { return true; }
    bool OnKey(uint32_t vk) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseUp(Point local, uint32_t buttons) override;
    void OnMouseEnter() override;
    void OnMouseLeave() override;
    void OnFocusChanged(bool focused) override;
    bool OnAnimate(float dt_seconds) override;

    void RelayoutParent();

    std::wstring text_;
    std::wstring glyph_;
    ButtonKind kind_ = ButtonKind::Standard;
    ButtonSize size_ = ButtonSize::Medium;
    float glow_t_ = 0.0f;
    float scale_t_ = 0.0f;
    Signal<> dropdown_;
    std::optional<Menu> dropdown_menu_;
    Connection dropdown_menu_bind_;
};

} // namespace lumen
