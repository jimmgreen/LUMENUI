#include "lumen/PasswordBox.h"
#include "lumen/Icons.h"
#include "lumen/Painter.h"

namespace lumen {
namespace {
constexpr float kRevealWidth = 24.0f;   // 点击热区与文字避让宽度
constexpr float kRevealMargin = 6.0f;   // 图标与控件右缘的距离
} // namespace

PasswordBox::PasswordBox() { Password(true); }

PasswordBox::PasswordBox(std::wstring_view text) : TextBox(text) { Password(true); }

PasswordBox& PasswordBox::Revealable(bool value) {
    if (revealable_ == value) return *this;
    revealable_ = value;
    if (!revealable_ && revealed_) {
        revealed_ = false;
        Password(true);
    }
    RelayoutParent();
    return *this;
}

bool PasswordBox::InRevealZone(Point local) const {
    return revealable_ && absolute_.w > kRevealWidth && local.x >= absolute_.w - kRevealWidth &&
           local.x <= absolute_.w && local.y >= 0.0f && local.y <= absolute_.h;
}

float PasswordBox::PadRight() const {
    return revealable_ ? TextBox::PadRight() + kRevealWidth : TextBox::PadRight();
}

CursorShape PasswordBox::CursorAt(Point local) const {
    return InRevealZone(local) ? CursorShape::Hand : TextBox::CursorAt(local);
}

void PasswordBox::OnMouseDown(Point local, uint32_t buttons) {
    if ((buttons & 0x0001) && InRevealZone(local)) {
        Focus();
        revealed_ = !revealed_;
        Password(!revealed_);   // 切掩码不改文本、不动光标
        Invalidate();
        return;
    }
    TextBox::OnMouseDown(local, buttons);
}

void PasswordBox::Draw(Painter& painter, const Theme& theme) {
    TextBox::Draw(painter, theme);
    if (!revealable_ || absolute_.IsEmpty()) return;
    // 两态共用 kView 一只眼，揭示态的斜杠自绘（线宽随图标）——不同字形拼一对必然不搭。
    const Color ink = enabled_ ? theme.text_secondary : theme.text_disabled;
    const Rect icon_box{absolute_.Right() - kRevealWidth - kRevealMargin, absolute_.y,
                        kRevealWidth, absolute_.h};
    painter.DrawIcon(icon::kView, icon_box, 16.0f, ink);
    if (revealed_) {
        const float cx = icon_box.x + icon_box.w * 0.5f;
        const float cy = icon_box.y + icon_box.h * 0.5f;
        const float half = 16.0f * 0.5f * 0.7071f;   // 斜杠端点落在图标外接方各角
        painter.DrawLine({cx - half, cy - half}, {cx + half, cy + half}, ink, 1.4f);   // 135°
    }
}

} // namespace lumen