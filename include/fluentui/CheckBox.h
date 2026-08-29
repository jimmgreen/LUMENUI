// fluentui/CheckBox.h — 复选框。
#pragma once
#include "Control.h"
#include <functional>
#include <string>

namespace fui {

class CheckBox : public Control {
public:
    CheckBox() = default;
    explicit CheckBox(std::wstring_view text) : text_(text) {}

    const std::wstring& Text() const noexcept { return text_; }
    CheckBox& Text(std::wstring_view value) { text_ = value; RelayoutParent(); return *this; }
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

    std::wstring text_;
    bool checked_ = false;
    std::function<void()> toggled_;
};

} // namespace fui
