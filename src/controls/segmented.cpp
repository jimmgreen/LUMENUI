#include "lumen/Segmented.h"
#include "lumen/Panel.h"
#include "lumen/Painter.h"
#include "../core/text_service.h"

namespace lumen {
namespace {
constexpr float kPad = 4.0f;
constexpr float kItemPadX = 16.0f;
constexpr float kItemRadius = 8.0f;
constexpr float kSlideSeconds = 0.6f;
} // namespace

void Segmented::RelayoutParent() { Control::RelayoutParent(); }

int Segmented::AddItem(std::wstring_view text) {
    items_.emplace_back(text);
    thumb_ready_ = false;
    RelayoutParent();
    return static_cast<int>(items_.size()) - 1;
}

void Segmented::ItemSlot(size_t index, float& x, float& w, const Theme& theme) {
    x = kPad;
    w = 0.0f;
    for (size_t i = 0; i < items_.size(); ++i) {
        const float iw = ItemWidth(i, theme);
        if (i == index) {
            w = iw;
            return;
        }
        x += iw;
    }
}

void Segmented::ApplyThumb(float t) {
    thumb_x_ = Lerp(thumb_from_x_, thumb_to_x_, t);
    thumb_w_ = Lerp(thumb_from_w_, thumb_to_w_, t);
}

void Segmented::SnapThumb() {
    if (items_.empty() || selected_ < 0) return;
    static const Theme kGeometry{};
    ItemSlot(static_cast<size_t>(selected_), thumb_to_x_, thumb_to_w_, kGeometry);
    thumb_from_x_ = thumb_to_x_;
    thumb_from_w_ = thumb_to_w_;
    slide_.Snap(1.0f);
    ApplyThumb(1.0f);
    thumb_ready_ = true;
}

Segmented& Segmented::SelectedIndex(ptrdiff_t index) {
    if (index < 0 || index >= static_cast<ptrdiff_t>(items_.size()) || index == selected_) return *this;
    selected_ = index;
    static const Theme kGeometry{};
    ItemSlot(static_cast<size_t>(selected_), thumb_to_x_, thumb_to_w_, kGeometry);
    if (window_ && thumb_ready_) {
        thumb_from_x_ = thumb_x_;
        thumb_from_w_ = thumb_w_;
        slide_.Play(0.0f, 1.0f, kSlideSeconds, Ease::Linear);
        Animate();
    } else {
        SnapThumb();
    }
    Invalidate();
    changed_.Emit(selected_, selected_);
    return *this;
}

bool Segmented::OnAnimate(float dt) {
    if (!slide_.running) return false;
    const bool more = slide_.Tick(dt);
    ApplyThumb(slide_.Value());
    return more || Control::OnAnimate(dt);
}

float Segmented::ItemWidth(size_t index, const Theme&) {
    return MeasureText(items_[index], TextRole::Caption).w + kItemPadX * 2.0f;
}

int Segmented::ItemAt(Point local, const Theme& theme) {
    float x = kPad;
    for (size_t i = 0; i < items_.size(); ++i) {
        const float w = ItemWidth(i, theme);
        if (local.x >= x && local.x < x + w) return static_cast<int>(i);
        x += w;
    }
    return -1;
}

Size Segmented::Measure(Size, const Theme& theme) {
    float width = kPad * 2.0f;
    for (size_t i = 0; i < items_.size(); ++i) width += ItemWidth(i, theme);
    return {width, 36.0f};
}

void Segmented::Draw(Painter& painter, const Theme& theme) {
    painter.FillRoundedRect(absolute_, 12.0f, theme.fill_hover);
    painter.StrokeRoundedRect(absolute_, 12.0f, theme.stroke_card);

    if (!thumb_ready_) SnapThumb();

    const float item_h = absolute_.h - kPad * 2.0f;
    const float item_y = absolute_.y + kPad;

    if (hover_item_ >= 0 && hover_item_ != static_cast<int>(selected_)) {
        float hx = 0.0f, hw = 0.0f;
        ItemSlot(static_cast<size_t>(hover_item_), hx, hw, theme);
        painter.FillRoundedRect({absolute_.x + hx, item_y, hw, item_h}, kItemRadius,
                                theme.fill_pressed);
    }

    if (thumb_w_ > 0.5f) {
        const Rect thumb{absolute_.x + thumb_x_, item_y, thumb_w_, item_h};
        painter.DrawGlow(thumb, kItemRadius, theme.glow_sm);
        painter.FillRoundedRect(thumb, kItemRadius, theme.accent);
    }

    float x = absolute_.x + kPad;
    for (size_t i = 0; i < items_.size(); ++i) {
        const float w = ItemWidth(i, theme);
        const Rect slot{x, item_y, w, item_h};
        const bool selected = static_cast<ptrdiff_t>(i) == selected_;
        const Color color = selected ? theme.accent_text
                                     : ((hovered_ && hover_item_ == static_cast<int>(i))
                                            ? theme.text
                                            : theme.text_secondary);
        painter.DrawText(items_[i], slot, TextRole::Caption, color, Align::Center);
        x += w;
    }
}

bool Segmented::OnKey(uint32_t vk) {
    if (items_.empty()) return false;
    if (vk == VK_LEFT || vk == VK_RIGHT) {
        const ptrdiff_t direction = vk == VK_RIGHT ? 1 : -1;
        const ptrdiff_t next =
            Clamp(selected_ + direction, ptrdiff_t{0}, static_cast<ptrdiff_t>(items_.size()) - 1);
        SelectedIndex(next);
        return true;
    }
    return false;
}

void Segmented::OnMouseMove(Point local, uint32_t buttons) {
    (void)buttons;
    static const Theme kGeometry{};
    const int index = ItemAt(local, kGeometry);
    if (index != hover_item_) {
        hover_item_ = index;
        Invalidate();
    }
}

void Segmented::OnMouseLeave() {
    Control::OnMouseLeave();
    if (hover_item_ != -1) {
        hover_item_ = -1;
        Invalidate();
    }
}

void Segmented::OnMouseDown(Point local, uint32_t buttons) {
    (void)buttons;
    static const Theme kGeometry{};
    const int index = ItemAt(local, kGeometry);
    if (index >= 0) SelectedIndex(index);
}

Segmented& Segmented::BindSelectedIndex(Property<int>& p) {
    auto apply = [this, &p] {
        if (bind_loop_) return;
        bind_loop_ = true;
        SelectedIndex(static_cast<ptrdiff_t>(p.Get()));
        bind_loop_ = false;
    };
    apply();
    index_prop_ = ScopedConnection(p.OnChanged([apply](const int&) { apply(); }));
    index_ctrl_ = ScopedConnection(changed_.Connect([this, &p](ptrdiff_t, ptrdiff_t) {
        if (bind_loop_) return;
        bind_loop_ = true;
        p = static_cast<int>(selected_);
        bind_loop_ = false;
    }));
    return *this;
}

} // namespace lumen
