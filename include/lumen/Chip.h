// lumen/Chip.h — 胶囊标签：默认为装饰码签；Selectable / Closable 后可点选、关闭。
// Events: OnToggled / BindToggled / OnClosed / BindClosed
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include "Signal.h"
#include <functional>
#include <string>

namespace lumen {

class Chip : public ControlOf<Chip> {
public:
    Chip() = default;
    explicit Chip(std::wstring_view text) : text_(text) {}

    Chip& Text(std::wstring_view value) { text_ = value; RelayoutParent(); return *this; }
    const std::wstring& Text() const noexcept { return text_; }
    Chip& Glyph(std::wstring_view value) { glyph_ = value; RelayoutParent(); return *this; }
    Chip& Foreground(Color value) { foreground_ = value; Invalidate(); return *this; }
    Chip& Background(Color value) {
        background_ = value;
        custom_background_ = true;
        Invalidate();
        return *this;
    }

    Chip& Selectable(bool value) {
        if (selectable_ == value) return *this;
        selectable_ = value;
        RelayoutParent();
        return *this;
    }
    bool Selectable() const noexcept { return selectable_; }
    bool Selected() const noexcept { return selected_; }
    Chip& Selected(bool value) {
        if (selected_ == value) return *this;
        selected_ = value;
        Invalidate();
        return *this;
    }
    Chip& OnToggled(std::function<void(bool)> handler) {
        toggled_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindToggled(std::function<void(bool)> handler) {
        return toggled_.Connect(std::move(handler));
    }

    Chip& Closable(bool value) {
        if (closable_ == value) return *this;
        closable_ = value;
        RelayoutParent();
        return *this;
    }
    bool Closable() const noexcept { return closable_; }
    Chip& OnClosed(std::function<void()> handler) {
        closed_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindClosed(std::function<void()> handler) {
        return closed_.Connect(std::move(handler));
    }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool HitTransparent() const noexcept override { return !selectable_ && !closable_; }
    bool Focusable() const noexcept override { return selectable_ || closable_; }
    float ChromeRadius(const Theme&) const noexcept override { return absolute_.h * 0.5f; }
    bool OnKey(uint32_t vk) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseUp(Point local, uint32_t buttons) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseEnter() override;
    void OnMouseLeave() override;
    void OnFocusChanged(bool focused) override;
    bool OnAnimate(float dt_seconds) override;
    CursorShape CursorAt(Point local) const override;

    void RelayoutParent();
    void Toggle();
    void Dismiss();
    bool CloseHit(Point local) const noexcept;

    std::wstring text_;
    std::wstring glyph_;
    Color foreground_{0.0f, 0.0f, 0.0f, 0.0f};   // 默认次要字色
    Color background_{0.0f, 0.0f, 0.0f, 0.0f};   // 默认 fill_hover
    bool custom_background_ = false;
    bool selectable_ = false;
    bool selected_ = false;
    bool closable_ = false;
    bool close_hover_ = false;
    float glow_t_ = 0.0f;
    Signal<bool> toggled_;
    Signal<> closed_;
};

} // namespace lumen
