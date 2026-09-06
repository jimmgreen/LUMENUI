// lumen/ControlOf.h — CRTP：基类 setter 返回 Derived&，链中任意位置不断。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: 非布局控件头，或见类声明
#pragma once
#include "Control.h"
#include "Core.h"
#include "Menu.h"
#include "Text.h"
#include <memory>
#include <utility>

namespace lumen {

class ToolTip;

template <class D>
class ControlOf : public Control {
public:
    ControlOf() = default;
    ControlOf(const ControlOf&) = delete;
    ControlOf& operator=(const ControlOf&) = delete;
    ControlOf(ControlOf&&) noexcept = default;
    ControlOf& operator=(ControlOf&&) noexcept = default;

    bool Visible() const noexcept { return Control::Visible(); }
    bool Enabled() const noexcept { return Control::Enabled(); }
    const std::wstring& ToolTip() const noexcept { return Control::ToolTip(); }
    const std::wstring& AccessibleName() const noexcept { return Control::AccessibleName(); }
    bool Spotlight() const noexcept { return Control::Spotlight(); }

    // 焦点变化（获得/失去都发，参数为当前状态）。观察用：宿主据此管理键盘归属，
    // 不再为收事件派生控件。鼠标、Tab、程序 Focus()/Blur() 都经此一点。
    D& OnFocused(std::function<void(bool focused)> fn) {
        Control::BindFocused(std::move(fn)).Release();
        return Self();
    }

    D& Visible(bool v) {
        Control::Visible(v);
        return Self();
    }
    D& Enabled(bool v) {
        Control::Enabled(v);
        return Self();
    }
    D& ToolTip(std::wstring_view text) {
        Control::ToolTip(text);
        return Self();
    }
    D& ToolTip(std::unique_ptr<class ToolTip> content) {
        Control::ToolTip(std::move(content));
        return Self();
    }
    D& ToolTip(std::string_view utf8) { return ToolTip(U8(utf8)); }
    float ToolTipDelay() const noexcept { return Control::ToolTipDelay(); }
    D& ToolTipDelay(float seconds) {
        Control::ToolTipDelay(seconds);
        return Self();
    }
    D& Grow(float weight = 1.0f) {
        Control::Grow(weight);
        return Self();
    }
    D& FillCross(bool value = true) {
        Control::FillCross(value);
        return Self();
    }
    D& Margin(float uniform) {
        Control::Margin(uniform);
        return Self();
    }
    D& Margin(float horizontal, float vertical) {
        Control::Margin(horizontal, vertical);
        return Self();
    }
    D& Margin(Thickness thickness) {
        Control::Margin(thickness);
        return Self();
    }
    D& MinSize(Size size) {
        Control::MinSize(size);
        return Self();
    }
    D& MaxSize(Size size) {
        Control::MaxSize(size);
        return Self();
    }
    D& Style(const ThemeOverride& o) {
        Control::Style(o);
        return Self();
    }
    D& Density(lumen::Density d) {
        Control::Density(d);
        return Self();
    }
    D& Spotlight(bool enabled) {
        Control::Spotlight(enabled);
        return Self();
    }
    D& BindVisible(Property<bool>& p) {
        Control::BindVisible(p);
        return Self();
    }
    D& BindEnabled(Property<bool>& p) {
        Control::BindEnabled(p);
        return Self();
    }
    D& ContextMenu(Menu menu) {
        Control::ContextMenu(std::move(menu));
        return Self();
    }
    D& AccessibleName(std::wstring_view name) {
        Control::AccessibleName(name);
        return Self();
    }
    D& AccessibleName(std::string_view utf8) { return AccessibleName(U8(utf8)); }
    D& SetBounds(const Rect& r) {
        Control::SetBounds(r);
        return Self();
    }
    D& Focus() {
        Control::Focus();
        return Self();
    }
    D& Blur() {
        Control::Blur();
        return Self();
    }

protected:
    D& Self() noexcept { return static_cast<D&>(*this); }
    const D& Self() const noexcept { return static_cast<const D&>(*this); }
};

} // namespace lumen
