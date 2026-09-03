// lumen/CheckBox.h -- checkbox, optional three-state.
// Events: OnToggled / BindToggled
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include "Signal.h"
#include <functional>
#include <string>

namespace lumen {

enum class CheckState { Unchecked, Checked, Indeterminate };

class CheckBox : public ControlOf<CheckBox> {
public:
    CheckBox() = default;
    explicit CheckBox(std::wstring_view text) : text_(text) {}

    const std::wstring& Text() const noexcept { return text_; }
    CheckBox& Text(std::wstring_view value) { text_ = value; RelayoutParent(); return *this; }

    bool Checked() const noexcept { return state_ == CheckState::Checked; }
    CheckBox& Checked(bool value);  // programmatic, no OnToggled

    CheckState State() const noexcept { return state_; }
    CheckBox& State(CheckState value);  // programmatic, no OnToggled

    // Unchecked -> Checked -> Indeterminate -> Unchecked. Off = binary toggle.
    CheckBox& ThreeState(bool value) { three_state_ = value; return *this; }
    bool ThreeState() const noexcept { return three_state_; }

    CheckBox& OnToggled(std::function<void(bool)> handler) {
        toggled_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindToggled(std::function<void(bool)> handler) { return toggled_.Connect(std::move(handler)); }
    CheckBox& BindChecked(Property<bool>& p);
    template <class T, class Pred>
    CheckBox& BindChecked(Property<T>& p, Pred pred) {
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
        return AutomationControlType::CheckBox;
    }
    uint32_t AutomationPatterns() const noexcept override { return kPatternToggle; }
    std::wstring AutomationName() const override {
        return accessible_name_.empty() ? text_ : accessible_name_;
    }
    int AutomationToggleState() const noexcept override {
        if (state_ == CheckState::Checked) return 1;
        if (state_ == CheckState::Indeterminate) return 2;
        return 0;
    }
    bool AutomationToggle() override {
        if (!enabled_) return false;
        Cycle();
        return true;
    }
    bool BlocksCardSpotlight() const noexcept override { return false; }
    bool OnKey(uint32_t vk) override;
    void OnMouseUp(Point local, uint32_t buttons) override;

    void RelayoutParent();
    void Cycle();

    std::wstring text_;
    CheckState state_ = CheckState::Unchecked;
    bool three_state_ = false;
    Signal<bool> toggled_;
    ScopedConnection checked_bind_;
    ScopedConnection checked_ctrl_;
    bool bind_loop_ = false;
};

} // namespace lumen
