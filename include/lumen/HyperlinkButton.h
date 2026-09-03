// lumen/HyperlinkButton.h — 内联文字链：下划线，悬停提亮。
// Events: OnClick / BindClick
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include "Signal.h"
#include <functional>
#include <string>

namespace lumen {

class HyperlinkButton : public ControlOf<HyperlinkButton> {
public:
    HyperlinkButton() = default;
    explicit HyperlinkButton(std::wstring_view text) : text_(text) {}

    HyperlinkButton& Text(std::wstring_view value) {
        text_ = value;
        RelayoutParent();
        return *this;
    }
    const std::wstring& Text() const noexcept { return text_; }
    HyperlinkButton& OnClick(std::function<void()> handler) {
        click_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindClick(std::function<void()> handler) { return click_.Connect(std::move(handler)); }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool Focusable() const noexcept override { return true; }
    AutomationControlType AutomationType() const noexcept override {
        return AutomationControlType::Hyperlink;
    }
    uint32_t AutomationPatterns() const noexcept override { return kPatternInvoke; }
    std::wstring AutomationName() const override {
        return accessible_name_.empty() ? text_ : accessible_name_;
    }
    bool AutomationInvoke() override {
        if (!enabled_) return false;
        click_.Emit();
        return true;
    }
    bool OnKey(uint32_t vk) override;
    void OnMouseUp(Point local, uint32_t buttons) override;
    void OnFocusChanged(bool focused) override;
    CursorShape CursorAt(Point local) const override;

    void RelayoutParent();

    std::wstring text_;
    Signal<> click_;
};

} // namespace lumen
