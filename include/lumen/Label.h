// lumen/Label.h — 静态文本。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include "Signal.h"
#include <functional>
#include <string>

namespace lumen {

class Label : public ControlOf<Label> {
public:
    Label() = default;
    explicit Label(std::wstring_view text, TextRole role = TextRole::Body)
        : text_(text), role_(role) {}

    Label& Text(std::wstring_view value) {
        AssertUiThread();
        text_ = std::wstring(value);
        RelayoutParent();
        return *this;
    }
    Label& Text(std::string_view utf8) { return Text(U8(utf8)); }
    const std::wstring& Text() const {
        AssertUiThread();
        return text_;
    }
    Label& Role(TextRole role) { role_ = role; RelayoutParent(); return *this; }
    Label& Secondary(bool value) { secondary_ = value; Invalidate(); return *this; }
    Label& Foreground(Color value) { foreground_ = value; Invalidate(); return *this; }
    Label& Alignment(Align value) { align_ = value; Invalidate(); return *this; }
    // 自动换行（按容器宽度）；关闭时单行省略号。
    Label& Wrap(bool value) { wrap_ = value; RelayoutParent(); return *this; }
    // 文字辉光（text-glow）：8 向低透明晕染，仅建议用于标题类大字。
    Label& TextGlow(bool value) { glow_ = value; Invalidate(); return *this; }

    template <class T, class Fmt>
    Label& BindText(Property<T>& p, Fmt fmt) {
        auto apply = [this, fmt](const T& v) { Text(fmt(v)); };
        apply(p.Get());
        text_bind_ = ScopedConnection(p.OnChanged([this, fmt](const T& v) { Text(fmt(v)); }));
        return *this;
    }
    Label& BindText(Property<std::wstring>& p) {
        auto apply = [this, &p] { Text(p.Get()); };
        apply();
        text_bind_ = ScopedConnection(p.OnChanged([apply](const std::wstring&) { apply(); }));
        return *this;
    }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    AutomationControlType AutomationType() const noexcept override {
        return AutomationControlType::Text;
    }
    std::wstring AutomationName() const override {
        return accessible_name_.empty() ? text_ : accessible_name_;
    }
    std::wstring AutomationValue() const override { return text_; }
    void Draw(Painter& painter, const Theme& theme) override;
    bool HitTransparent() const noexcept override { return true; }

    std::wstring text_;
    TextRole role_ = TextRole::Body;
    Align align_ = Align::Leading;
    Color foreground_{0.0f, 0.0f, 0.0f, 0.0f};
    bool secondary_ = false;
    bool wrap_ = false;
    bool glow_ = false;
    ScopedConnection text_bind_;
};

} // namespace lumen
