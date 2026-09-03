// lumen/Swatch.h — 圆形色块选择按钮（强调色切换等）。
// Events: OnPicked / BindPicked
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include "Signal.h"
#include <functional>

namespace lumen {

class ColorSwatch : public ControlOf<ColorSwatch> {
public:
    explicit ColorSwatch(lumen::Color color) : color_(color) {}

    ColorSwatch& Color(lumen::Color value) { color_ = value; Invalidate(); return *this; }
    lumen::Color Color() const noexcept { return color_; }
    ColorSwatch& Selected(bool value) { selected_ = value; Invalidate(); return *this; }
    template <class T, class Pred>
    ColorSwatch& BindSelected(Property<T>& p, Pred pred) {
        auto apply = [this, pred](const T& v) { Selected(static_cast<bool>(pred(v))); };
        apply(p.Get());
        selected_bind_ = ScopedConnection(p.OnChanged([this, pred](const T& v) {
            Selected(static_cast<bool>(pred(v)));
        }));
        return *this;
    }
    ColorSwatch& OnPicked(std::function<void()> handler) { picked_.Subscribe(std::move(handler)); return *this; }
    Connection BindPicked(std::function<void()> handler) {
        return picked_.Connect(std::move(handler));
    }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool Focusable() const noexcept override { return true; }
    void OnMouseUp(Point local, uint32_t buttons) override;
    bool OnKey(uint32_t vk) override;

    void RelayoutParent();

    lumen::Color color_;
    bool selected_ = false;
    Signal<> picked_;
    ScopedConnection selected_bind_;
};

} // namespace lumen
