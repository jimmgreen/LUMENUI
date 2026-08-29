// fluentui/RadioButton.h — 单选按钮。同一父容器内 Group 值相同的按钮互斥。
#pragma once
#include "Control.h"
#include <functional>
#include <string>

namespace fui {

class RadioButton : public Control {
public:
    RadioButton() = default;
    explicit RadioButton(std::wstring_view text) : text_(text) {}

    const std::wstring& Text() const noexcept { return text_; }
    RadioButton& Text(std::wstring_view value) { text_ = value; RelayoutParent(); return *this; }
    RadioButton& Group(int value) { group_ = value; return *this; }
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

    void RelayoutParent();
    void SelectExclusive();   // 选中自己并取消同组其他选中

    std::wstring text_;
    int group_ = 0;
    bool checked_ = false;
    std::function<void()> toggled_;
};

} // namespace fui
