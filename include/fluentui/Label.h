// fluentui/Label.h — 静态文本。
#pragma once
#include "Control.h"
#include <string>

namespace fui {

class Label : public Control {
public:
    Label() = default;
    explicit Label(std::wstring_view text, TextRole role = TextRole::Body)
        : text_(text), role_(role) {}

    Label& Text(std::wstring_view value) { text_ = std::wstring(value); RelayoutParent(); return *this; }
    const std::wstring& Text() const noexcept { return text_; }
    Label& Role(TextRole role) { role_ = role; RelayoutParent(); return *this; }
    Label& Secondary(bool value) { secondary_ = value; Invalidate(); return *this; }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool HitTransparent() const noexcept override { return true; }

    std::wstring text_;
    TextRole role_ = TextRole::Body;
    bool secondary_ = false;
};

} // namespace fui
