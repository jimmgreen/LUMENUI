// lumen/RadioButton.h — 单选按钮。同一父容器内 Group 值相同的按钮互斥。
// Events: OnToggled / BindToggled
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include "Signal.h"
#include <functional>
#include <string>

namespace lumen {

class RadioButton : public ControlOf<RadioButton> {
public:
    RadioButton() = default;
    explicit RadioButton(std::wstring_view text) : text_(text) {}

    const std::wstring& Text() const noexcept { return text_; }
    RadioButton& Text(std::wstring_view value) { text_ = value; RelayoutParent(); return *this; }
    RadioButton& Group(int value) { group_ = value; return *this; }
    bool Checked() const noexcept { return checked_; }
    RadioButton& Checked(bool value);   // 编程赋值，不触发 OnToggled

    RadioButton& OnToggled(std::function<void(bool)> handler) {
        toggled_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindToggled(std::function<void(bool)> handler) { return toggled_.Connect(std::move(handler)); }
    template <class T, class Pred>
    RadioButton& BindChecked(Property<T>& p, Pred pred) {
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
        return AutomationControlType::RadioButton;
    }
    uint32_t AutomationPatterns() const noexcept override {
        return kPatternToggle | kPatternSelectionItem;
    }
    std::wstring AutomationName() const override {
        return accessible_name_.empty() ? text_ : accessible_name_;
    }
    int AutomationToggleState() const noexcept override { return checked_ ? 1 : 0; }
    bool AutomationToggle() override {
        if (!enabled_) return false;
        if (!checked_) SelectExclusive();
        return true;
    }
    bool BlocksCardSpotlight() const noexcept override { return false; }
    bool OnKey(uint32_t vk) override;
    void OnMouseUp(Point local, uint32_t buttons) override;

    void RelayoutParent();
    void SelectExclusive();   // 选中自己并取消同组其他选中

    std::wstring text_;
    int group_ = 0;
    bool checked_ = false;
    Signal<bool> toggled_;
    ScopedConnection checked_bind_;
};

} // namespace lumen
