// lumen/RepeatButton.h — 按住连发按钮（外观同 Button）。
// Events: OnClick
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: 顶层窗口，客户区由 Root() 布局
#pragma once
#include "Animate.h"
#include "Button.h"

namespace lumen {

class RepeatButton : public Button {
public:
    RepeatButton() = default;
    explicit RepeatButton(std::wstring_view text, ButtonKind kind = ButtonKind::Standard)
        : Button(text, kind) {}

    // 按下后到第一次连发的等待（秒）。默认 0.40。
    RepeatButton& Delay(float seconds) {
        hold_.delay = seconds > 0.0f ? seconds : 0.0f;
        return *this;
    }
    float Delay() const noexcept { return hold_.delay; }
    // 连发间隔（秒）。默认 0.05。
    RepeatButton& Interval(float seconds) {
        hold_.interval = seconds > 0.0f ? seconds : 1.0e-4f;
        return *this;
    }
    float Interval() const noexcept { return hold_.interval; }

    RepeatButton& Text(std::wstring_view value) { Button::Text(value); return *this; }
    RepeatButton& Glyph(std::wstring_view value) { Button::Glyph(value); return *this; }
    RepeatButton& Kind(ButtonKind value) { Button::Kind(value); return *this; }
    RepeatButton& SizeClass(ButtonSize value) { Button::SizeClass(value); return *this; }
    RepeatButton& Height(float value) { Button::Height(value); return *this; }
    RepeatButton& Pill(bool value) { Button::Pill(value); return *this; }
    RepeatButton& Shimmer(bool value) { Button::Shimmer(value); return *this; }
    RepeatButton& OnClick(std::function<void()> handler) { Button::OnClick(std::move(handler)); return *this; }
    bool Enabled() const noexcept { return Control::Enabled(); }
    RepeatButton& Enabled(bool value) {
        if (!value) StopHold();
        Button::Enabled(value);
        return *this;
    }

protected:
    friend class WindowImpl;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseUp(Point local, uint32_t buttons) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseLeave() override;
    void OnFocusChanged(bool focused) override;
    bool OnKey(uint32_t vk) override;
    bool OnAnimate(float dt_seconds) override;

private:
    bool Inside(Point local) const noexcept;
    void Fire();
    void BeginHold(bool from_key, Point local);
    void StopHold();

    RepeatHold hold_{};
    bool pointer_inside_ = false;
    bool key_held_ = false;
};

} // namespace lumen
