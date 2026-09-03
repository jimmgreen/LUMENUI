// lumen/ToggleButton.h -- standalone sticky button (checked stays pressed).
// Events: OnToggled / BindToggled
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "Button.h"
#include "ControlOf.h"
#include <functional>
#include <string>

namespace lumen {

class ToggleButton : public ControlOf<ToggleButton> {
public:
    ToggleButton() = default;
    explicit ToggleButton(std::wstring_view text) : text_(text) {}

    const std::wstring& Text() const noexcept { return text_; }
    ToggleButton& Text(std::wstring_view value) { text_ = value; RelayoutParent(); return *this; }
    const std::wstring& Glyph() const noexcept { return glyph_; }
    ToggleButton& Glyph(std::wstring_view value) { glyph_ = value; RelayoutParent(); return *this; }
    ButtonSize SizeClass() const noexcept { return size_; }
    ToggleButton& SizeClass(ButtonSize value) { size_ = value; RelayoutParent(); return *this; }
    ToggleButton& Pill(bool value) { pill_ = value; Invalidate(); return *this; }
    bool Pill() const noexcept { return pill_; }

    bool Checked() const noexcept { return checked_; }
    ToggleButton& Checked(bool value);  // programmatic, no OnToggled

    ToggleButton& OnToggled(std::function<void(bool)> handler) {
        toggled_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindToggled(std::function<void(bool)> handler) { return toggled_.Connect(std::move(handler)); }
    ToggleButton& BindChecked(Property<bool>& p);

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    float ChromeRadius(const Theme& theme) const noexcept override {
        return pill_ ? absolute_.h * 0.5f : theme.radius_control;
    }
    bool Focusable() const noexcept override { return true; }
    AutomationControlType AutomationType() const noexcept override {
        return AutomationControlType::Button;
    }
    uint32_t AutomationPatterns() const noexcept override { return kPatternToggle; }
    std::wstring AutomationName() const override {
        return accessible_name_.empty() ? text_ : accessible_name_;
    }
    int AutomationToggleState() const noexcept override { return checked_ ? 1 : 0; }
    bool AutomationToggle() override {
        if (!enabled_) return false;
        Toggle();
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
    void Toggle();

    std::wstring text_;
    std::wstring glyph_;
    ButtonSize size_ = ButtonSize::Medium;
    bool pill_ = false;
    bool checked_ = false;
    float glow_t_ = 0.0f;
    float scale_t_ = 0.0f;
    Signal<bool> toggled_;
    ScopedConnection checked_bind_;
    ScopedConnection checked_ctrl_;
    bool bind_loop_ = false;
};

} // namespace lumen
