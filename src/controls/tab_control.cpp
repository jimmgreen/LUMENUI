#include "fluentui/TabControl.h"
#include "fluentui/Panel.h"
#include "fluentui/Painter.h"
#include "../core/text_service.h"
#include <algorithm>

namespace fui {
namespace {
constexpr float kStripHeight = 40.0f;
constexpr float kTabPadX = 14.0f;
constexpr float kIndicatorHeight = 2.5f;

Color Mix(Color a, Color b, float t) {
    return {Lerp(a.r, b.r, t), Lerp(a.g, b.g, t), Lerp(a.b, b.b, t), Lerp(a.a, b.a, t)};
}
} // namespace

void TabControl::RelayoutParent() { Control::RelayoutParent(); }

StackPanel& TabControl::AddTab(std::wstring_view title) {
    titles_.emplace_back(title);
    StackPanel& page = this->Add<StackPanel>();
    page.SetVisible(titles_.size() == 1);
    RelayoutParent();
    return page;
}

void TabControl::SetSelectedIndex(int index) {
    if (index < 0 || index >= static_cast<int>(titles_.size()) || index == selected_) return;
    selected_ = index;
    ShowOnlySelected();
    Invalidate();
    if (changed_) changed_();
}

void TabControl::ShowOnlySelected() {
    for (size_t i = 0; i < children_.size(); ++i) {
        SetChildVisibility(i, static_cast<int>(i) == selected_);
    }
}

float TabControl::TabWidth(size_t index, const Theme& theme) {
    (void)theme;
    return UiText().MeasureText(titles_[index], TextRole::Body).w + kTabPadX * 2.0f + 8.0f;
}

int TabControl::TabAt(Point local, const Theme& theme) {
    if (local.y < 0.0f || local.y >= kStripHeight) return -1;
    float x = 0.0f;
    for (size_t i = 0; i < titles_.size(); ++i) {
        x += TabWidth(i, theme);
        if (local.x < x) return static_cast<int>(i);
    }
    return -1;
}

Size TabControl::Measure(Size, const Theme&) {
    static const Theme kGeometryDefault;
    float page_w = 0.0f, page_h = 0.0f;
    for (size_t i = 0; i < children_.size(); ++i) {
        const Size desired = MeasureChildAt(i, {1.0e5f, 1.0e5f}, kGeometryDefault);
        page_w = std::max(page_w, desired.w);
        page_h = std::max(page_h, desired.h);
    }
    return {std::max(page_w, 200.0f), kStripHeight + std::max(page_h, 100.0f)};
}

void TabControl::Arrange(const Rect& absolute) {
    absolute_ = absolute;
    static const Theme kGeometryDefault;
    const Rect content{absolute.x, absolute.y + kStripHeight, absolute.w,
                       std::max(absolute.h - kStripHeight, 0.0f)};
    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        SetChildBounds(Child(i), {0.0f, kStripHeight, absolute.w, content.h});
        MeasureChildAt(i, {content.w, content.h}, kGeometryDefault);
        ArrangeChildAt(i);
    }
}

void TabControl::Draw(Painter& painter, const Theme& theme) {
    float x = absolute_.x;
    for (size_t i = 0; i < titles_.size(); ++i) {
        const float w = TabWidth(i, theme);
        const Rect tab{x, absolute_.y, w, kStripHeight};
        const bool selected = static_cast<int>(i) == selected_;
        const bool hovered = static_cast<int>(i) == hover_tab_;
        painter.DrawText(titles_[i], tab, selected ? TextRole::BodyStrong : TextRole::Body,
                         selected ? theme.text
                                  : (hovered ? theme.text : theme.text_secondary));
        if (selected) {
            painter.FillRoundedRect({tab.x + kTabPadX, tab.Bottom() - kIndicatorHeight,
                                     w - kTabPadX * 2.0f, kIndicatorHeight},
                                    kIndicatorHeight * 0.5f, theme.accent);
        }
        x += w;
    }
    painter.FillRect({absolute_.x, absolute_.y + kStripHeight - 1.0f, absolute_.w, 1.0f},
                     theme.divider);
}

void TabControl::OnMouseMove(Point local, uint32_t buttons) {
    (void)buttons;
    static const Theme kGeometryDefault;
    const int tab = TabAt(local, kGeometryDefault);
    if (tab != hover_tab_) {
        hover_tab_ = tab;
        Invalidate();
    }
}

void TabControl::OnMouseDown(Point local, uint32_t buttons) {
    (void)buttons;
    static const Theme kGeometryDefault;
    const int tab = TabAt(local, kGeometryDefault);
    if (tab >= 0) SetSelectedIndex(tab);
}

void TabControl::OnMouseLeave() {
    Panel::OnMouseLeave();
    if (hover_tab_ != -1) {
        hover_tab_ = -1;
        Invalidate();
    }
}

} // namespace fui
