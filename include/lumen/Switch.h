// lumen/Switch.h — 开关。
// Events: OnToggled / BindToggled
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include "Signal.h"
#include <functional>
#include <string>

namespace lumen {

class Switch : public ControlOf<Switch> {
public:
    Switch() = default;
    explicit Switch(std::wstring_view text) : text_(text) {}

    const std::wstring& Text() const noexcept { return text_; }
    Switch& Text(std::wstring_view value) { text_ = value; RelayoutParent(); return *this; }
    bool Checked() const noexcept { return checked_; }
    Switch& Checked(bool value);   // 编程赋值，不触发 OnToggled

    Switch& OnToggled(std::function<void(bool)> handler) {
        toggled_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindToggled(std::function<void(bool)> handler) { return toggled_.Connect(std::move(handler)); }
    Switch& BindChecked(Property<bool>& p);
    template <class T, class Pred>
    Switch& BindChecked(Property<T>& p, Pred pred) {
        auto apply = [this, pred](const T& v) { Checked(static_cast<bool>(pred(v))); };
        apply(p.Get());
        checked_bind_ = ScopedConnection(p.OnChanged([this, pred](const T& v) {
            Checked(static_cast<bool>(pred(v)));
        }));
        return *this;
    }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
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
        checked_ = !checked_;
        if (window_) Animate();
        else knob_t_ = checked_ ? 1.0f : 0.0f;
        toggled_.Emit(checked_);
        Invalidate();
        return true;
    }
    bool BlocksCardSpotlight() const noexcept override { return false; }
    bool OnKey(uint32_t vk) override;
    void OnMouseUp(Point local, uint32_t buttons) override;
    bool OnAnimate(float dt_seconds) override;

    void RelayoutParent();

    std::wstring text_;
    bool checked_ = false;
    float knob_t_ = 0.0f;   // 0 关 → 1 开
    Signal<bool> toggled_;
    ScopedConnection checked_bind_;
    ScopedConnection checked_ctrl_;
    bool bind_loop_ = false;
};

} // namespace lumen
