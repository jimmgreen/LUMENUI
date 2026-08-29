// fluentui/Button.h — 按钮。
#pragma once
#include "Control.h"
#include <functional>
#include <string>

namespace fui {

enum class ButtonKind { Standard, Primary, Transparent, Danger };

class Button : public Control {
public:
    Button() = default;
    explicit Button(std::wstring_view text, ButtonKind kind = ButtonKind::Standard)
        : text_(text), kind_(kind) {}

    const std::wstring& Text() const noexcept { return text_; }
    Button& Text(std::wstring_view value) { text_ = value; RelayoutParent(); return *this; }
    // Segoe Fluent Icons 字形（见 fui::icon）。
    const std::wstring& Glyph() const noexcept { return glyph_; }
    Button& Glyph(std::wstring_view value) { glyph_ = value; RelayoutParent(); return *this; }
    ButtonKind Kind() const noexcept { return kind_; }
    Button& Kind(ButtonKind value) { kind_ = value; Invalidate(); return *this; }

    void OnClick(std::function<void()> handler) { click_ = std::move(handler); }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool Focusable() const noexcept override { return true; }
    bool OnKey(uint32_t vk) override;
    void OnMouseUp(Point local, uint32_t buttons) override;
    void OnFocusChanged(bool focused) override;

    void RelayoutParent();

    std::wstring text_;
    std::wstring glyph_;
    ButtonKind kind_ = ButtonKind::Standard;
    std::function<void()> click_;
};

} // namespace fui
