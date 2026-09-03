// lumen/InfoBar.h — 页内提示条：字形 + 标题/正文 + 可选操作钮 / 关闭。语义靠亮度与图标。
// Events: OnClosed / BindClosed
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "Panel.h"
#include "Signal.h"
#include <functional>
#include <string>

namespace lumen {

class Button;

class InfoBar : public PanelOf<InfoBar> {
public:
    enum class InfoTone { Informational, Success, Warning, Critical };

    InfoBar() = default;
    explicit InfoBar(std::wstring_view title) : title_(title) {}

    InfoBar& Title(std::wstring_view value) { title_ = value; RelayoutParent(); return *this; }
    const std::wstring& Title() const noexcept { return title_; }
    InfoBar& Message(std::wstring_view value) { message_ = value; RelayoutParent(); return *this; }
    const std::wstring& Message() const noexcept { return message_; }
    InfoBar& Glyph(std::wstring_view value) { glyph_ = value; Invalidate(); return *this; }
    const std::wstring& Glyph() const noexcept { return glyph_; }
    InfoBar& Tone(InfoTone value) { tone_ = value; Invalidate(); return *this; }
    InfoTone Tone() const noexcept { return tone_; }
    InfoBar& Closable(bool value) { closable_ = value; RelayoutParent(); return *this; }
    InfoBar& OnClosed(std::function<void()> handler) { closed_.Subscribe(std::move(handler)); return *this; }
    Connection BindClosed(std::function<void()> handler) {
        return closed_.Connect(std::move(handler));
    }
    // 右侧操作钮（不自动关闭）。空 label 隐藏。也可 Add<Button> 挂更多操作。
    InfoBar& Action(std::wstring_view label, std::function<void()> on_click);

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    AutomationControlType AutomationType() const noexcept override {
        return AutomationControlType::StatusBar;
    }
    std::wstring AutomationName() const override {
        if (!accessible_name_.empty()) return accessible_name_;
        if (title_.empty()) return message_;
        if (message_.empty()) return title_;
        return title_ + L" " + message_;
    }
    int AutomationLiveSetting() const noexcept override { return 2; }
    void Arrange(const Rect& absolute) override;
    void Draw(Painter& painter, const Theme& theme) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseLeave() override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseUp(Point local, uint32_t buttons) override;

    void RelayoutParent();
    Rect CloseRect() const noexcept;
    const wchar_t* GlyphToDraw() const noexcept;

    std::wstring title_;
    std::wstring message_;
    std::wstring glyph_;
    InfoTone tone_ = InfoTone::Informational;
    bool closable_ = true;
    bool close_hot_ = false;
    bool close_press_ = false;
    Button* action_ = nullptr;
    std::function<void()> action_cb_;
    Signal<> closed_;
};

} // namespace lumen
