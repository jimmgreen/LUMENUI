#include "fluentui/ComboBox.h"
#include "fluentui/Icons.h"
#include "fluentui/Menu.h"
#include "fluentui/Panel.h"
#include "fluentui/Painter.h"
#include "../core/menu_window.h"
#include "../core/text_service.h"
#include "../core/window_impl.h"
#include <windows.h>
#include <algorithm>

namespace fui {
namespace {
constexpr float kPadX = 10.0f;
constexpr float kChevronArea = 28.0f;

Color Mix(Color a, Color b, float t) {
    return {Lerp(a.r, b.r, t), Lerp(a.g, b.g, t), Lerp(a.b, b.b, t), Lerp(a.a, b.a, t)};
}
} // namespace

void ComboBox::RelayoutParent() { Control::RelayoutParent(); }

void ComboBox::AddItem(std::wstring_view text) {
    items_.emplace_back(text);
    RelayoutParent();
}

void ComboBox::ClearItems() {
    items_.clear();
    selected_ = -1;
    RelayoutParent();
}

void ComboBox::SetSelectedIndex(ptrdiff_t index) {
    if (index < -1 || index >= static_cast<ptrdiff_t>(items_.size())) return;
    if (selected_ == index) return;
    selected_ = index;
    Invalidate();
    if (changed_) changed_();
}

std::wstring ComboBox::SelectedText() const {
    return selected_ >= 0 ? items_[static_cast<size_t>(selected_)] : std::wstring();
}

Size ComboBox::Measure(Size, const Theme& theme) {
    float max_w = 60.0f;
    for (const std::wstring& item : items_) {
        max_w = std::max(max_w, UiText().MeasureText(item, TextRole::Body).w);
    }
    return {std::min(max_w + kPadX * 2.0f + kChevronArea, 320.0f), theme.input_height};
}

void ComboBox::OnFocusChanged(bool focused) {
    Control::OnFocusChanged(focused);
    Invalidate();
}

void ComboBox::OnMouseDown(Point local, uint32_t buttons) {
    (void)local;
    (void)buttons;
    if (enabled_) OpenPopup();
}

void ComboBox::OpenPopup() {
    if (items_.empty() || !window_) return;
    dropdown_open_ = true;
    Invalidate();

    std::vector<MenuItem> menu_items;
    menu_items.reserve(items_.size());
    for (size_t i = 0; i < items_.size(); ++i) {
        MenuItem item;
        item.text = items_[i];
        item.checked = static_cast<ptrdiff_t>(i) == selected_;
        menu_items.push_back(std::move(item));
    }
    POINT px{static_cast<LONG>(absolute_.x * WindowImpl::ScaleOf(window_)),
             static_cast<LONG>((absolute_.Bottom() + 4.0f) * WindowImpl::ScaleOf(window_))};
    ClientToScreen(WindowImpl::HwndOf(window_), &px);
    const int result = MenuWindow::Show(WindowImpl::HwndOf(window_), menu_items, px,
                                        WindowImpl::ThemeOf(window_),
                                        WindowImpl::ScaleOf(window_));
    dropdown_open_ = false;
    if (result >= 0 && result != selected_) {
        selected_ = result;
        if (changed_) changed_();
    }
    Invalidate();
}

bool ComboBox::OnKey(uint32_t vk) {
    if (!enabled_ || items_.empty()) return false;
    switch (vk) {
    case VK_DOWN:
    case VK_UP: {
        const ptrdiff_t direction = vk == VK_DOWN ? 1 : -1;
        const ptrdiff_t next = Clamp(selected_ + direction, ptrdiff_t{0},
                                     static_cast<ptrdiff_t>(items_.size()) - 1);
        if (next != selected_) {
            selected_ = next;
            Invalidate();
            if (changed_) changed_();
        }
        return true;
    }
    case VK_SPACE:
    case VK_RETURN:
        OpenPopup();
        return true;
    default:
        return false;
    }
}

void ComboBox::Draw(Painter& painter, const Theme& theme) {
    Color fill = Mix(theme.control_fill, theme.control_fill_hover, hover_t_);
    Color border = theme.control_stroke;
    if (dropdown_open_ || pressed_) {
        fill = theme.control_fill_pressed;
        border = theme.control_stroke_strong;
    }
    if (!enabled_) {
        fill.a *= 0.55f;
        border.a *= 0.5f;
    }
    painter.FillRoundedRect(absolute_, theme.radius_control, fill);
    painter.StrokeRoundedRect(absolute_, theme.radius_control, border);
    if (focused_ && enabled_) {
        painter.FillRoundedRect({absolute_.x + 2.0f, absolute_.Bottom() - 2.0f,
                                 absolute_.w - 4.0f, 2.0f},
                                1.0f, theme.accent);
    }

    painter.DrawIcon(icon::kChevronDown,
                     {absolute_.Right() - kChevronArea, absolute_.y, kChevronArea, absolute_.h},
                     10.0f, enabled_ ? theme.text_secondary : theme.text_disabled);
    const std::wstring text = SelectedText();
    if (!text.empty()) {
        painter.DrawText(text,
                         {absolute_.x + kPadX, absolute_.y,
                          absolute_.w - kPadX - kChevronArea, absolute_.h},
                         TextRole::Body, enabled_ ? theme.text : theme.text_disabled);
    }
}

} // namespace fui
