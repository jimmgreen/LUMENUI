// lumen/HotkeyBox.h — 捕获并显示 Ctrl+K 这类快捷键（与 Menu shortcut 同格式）。
// Events: OnChanged / BindChanged
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include "Signal.h"
#include <functional>
#include <string>

namespace lumen {

class HotkeyBox : public ControlOf<HotkeyBox> {
public:
    HotkeyBox() = default;
    explicit HotkeyBox(std::wstring_view chord) { Chord(chord); }

    HotkeyBox& Chord(std::wstring_view value);
    const std::wstring& Chord() const noexcept { return chord_; }
    uint32_t Vk() const noexcept { return vk_; }
    bool HasCtrl() const noexcept { return ctrl_; }
    bool HasShift() const noexcept { return shift_; }
    bool HasAlt() const noexcept { return alt_; }
    bool Empty() const noexcept { return vk_ == 0; }
    HotkeyBox& Clear();

    HotkeyBox& Placeholder(std::wstring_view value) {
        placeholder_ = value;
        Invalidate();
        return *this;
    }
    const std::wstring& Placeholder() const noexcept { return placeholder_; }

    HotkeyBox& OnChanged(std::function<void()> handler) {
        changed_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindChanged(std::function<void()> handler) {
        return changed_.Connect(std::move(handler));
    }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool Focusable() const noexcept override { return true; }
    void OnFocusChanged(bool focused) override;
    bool OnKey(uint32_t vk) override;
    bool OnChar(wchar_t ch) override;
    void OnMouseEnter() override;
    void OnMouseLeave() override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    CursorShape CursorAt(Point local) const override;

    void RelayoutParent();

private:
    bool Assign(uint32_t vk, bool ctrl, bool shift, bool alt, bool notify);
    Rect ClearRect() const noexcept;
    bool InClear(Point local) const noexcept;

    std::wstring chord_;
    std::wstring placeholder_{L"Press shortcut"};
    Signal<> changed_;
    uint32_t vk_ = 0;
    bool ctrl_ = false;
    bool shift_ = false;
    bool alt_ = false;
};

} // namespace lumen
