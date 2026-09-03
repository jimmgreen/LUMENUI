#include "lumen/Avatar.h"
#include "lumen/Icons.h"
#include "lumen/Painter.h"
#include "../core/text_service.h"
#include <cwctype>

namespace lumen {

Avatar& Avatar::Name(std::wstring_view value) {
    name_ = std::wstring(value);
    initials_.clear();
    // CJK 取首字；拉丁取首个字母并大写。无字母则退回首字。
    for (wchar_t ch : name_) {
        if (ch >= 0x2E80 && ch <= 0x9FFF) {
            initials_.assign(1, ch);
            break;
        }
        if (std::iswalpha(static_cast<wint_t>(ch))) {
            initials_.assign(1, static_cast<wchar_t>(towupper(static_cast<wint_t>(ch))));
            break;
        }
    }
    if (initials_.empty() && !name_.empty()) initials_.assign(1, name_[0]);
    Invalidate();
    return *this;
}

Size Avatar::Measure(Size, const Theme&) {
    const float side = diameter_ + (presence_ == Presence::None ? 0.0f : 4.0f);
    return {side, side};
}

void Avatar::Draw(Painter& painter, const Theme& theme) {
    if (absolute_.IsEmpty()) return;
    const Rect circle{absolute_.x, absolute_.y, diameter_, diameter_};
    painter.FillRoundedRect(circle, diameter_ * 0.5f, theme.fill_input_hover);
    painter.StrokeRoundedRect(circle, diameter_ * 0.5f, theme.stroke_card);

    if (!glyph_.empty()) {
        painter.DrawIcon(glyph_, circle, diameter_ * 0.5f, theme.text_secondary);
    } else if (!initials_.empty()) {
        painter.DrawText(initials_, circle, TextRole::Caption, theme.text, Align::Center);
    }

    if (presence_ != Presence::None) {
        const float dot = std::max(8.0f, diameter_ * 0.28f);
        // 碳底描边圈：状态点压在圆缘右下，先画背景色圈再画状态点。
        const float ring = dot + 4.0f;
        const Rect ring_rect{circle.Right() - ring * 0.75f, circle.Bottom() - ring * 0.75f, ring,
                             ring};
        painter.FillRoundedRect(ring_rect, ring * 0.5f, theme.bg);
        Color state = theme.text_disabled;   // Away
        if (presence_ == Presence::Online) state = theme.accent;
        if (presence_ == Presence::Busy) state = theme.text;
        painter.FillRoundedRect({ring_rect.x + 2.0f, ring_rect.y + 2.0f, dot, dot}, dot * 0.5f,
                                state);
    }
}

} // namespace lumen
