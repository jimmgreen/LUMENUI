// fluentui/Switch.h — 开关。
#pragma once
#include "Control.h"
#include <functional>
#include <string>

namespace fui {

class Switch : public Control {
public:
    Switch() = default;
    explicit Switch(std::wstring_view text) : text_(text) {}

    const std::wstring& Text() const noexcept { return text_; }
    Switch& Text(std::wstring_view value) { text_ = value; RelayoutParent(); return *this; }
    bool Checked() const noexcept { return checked_; }
    void SetChecked(bool value);   // 编程赋值，不触发 OnToggled

    void OnToggled(std::function<void()> handler) { toggled_ = std::move(handler); }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool Focusable() const noexcept override { return true; }
    bool OnKey(uint32_t vk) override;
    void OnMouseUp(Point local, uint32_t buttons) override;
    bool OnAnimate(float dt_seconds) override;

    void RelayoutParent();

    std::wstring text_;
    bool checked_ = false;
    float knob_t_ = 0.0f;   // 0 关 → 1 开
    std::function<void()> toggled_;
};

} // namespace fui
