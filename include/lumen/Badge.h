// lumen/Badge.h — 状态徽章（实心/浅底胶囊）。
// Events: 无（本头无订阅事件）
// Keys: 无独立快捷键（命中穿透或非焦点）
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include <string>

namespace lumen {

class Badge : public ControlOf<Badge> {
public:
    enum class BadgeTone { Accent, Success, Warning, Neutral };

    Badge() = default;
    explicit Badge(std::wstring_view text, BadgeTone tone = BadgeTone::Accent)
        : text_(text), tone_(tone) {}

    Badge& Text(std::wstring_view value) { text_ = value; RelayoutParent(); return *this; }
    const std::wstring& Text() const noexcept { return text_; }
    Badge& Tone(BadgeTone value) { tone_ = value; Invalidate(); return *this; }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;

    void RelayoutParent();

    std::wstring text_;
    BadgeTone tone_ = BadgeTone::Accent;
};

} // namespace lumen
