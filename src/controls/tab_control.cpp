#include "lumen/TabControl.h"
#include "lumen/Icons.h"
#include "lumen/Menu.h"
#include "lumen/Panel.h"
#include "lumen/Painter.h"
#include "lumen/Window.h"
#include "../core/text_service.h"
#include <algorithm>
#include <cmath>
#include <windows.h>

namespace lumen {
namespace {
constexpr float kStripHeight = 40.0f;
constexpr float kTabPadX = 14.0f;
constexpr float kGlyphSlot = 24.0f;
constexpr float kCloseSlot = 28.0f;
constexpr float kOverflowSlot = 36.0f;
constexpr float kIndicatorHeight = 2.5f;
constexpr float kSlideSeconds = 0.32f;
constexpr float kDragSlop = 4.0f;
constexpr float kWheelStep = 48.0f;
constexpr uint32_t kLeftButton = 0x0001;
constexpr uint32_t kMiddleButton = 0x0010;

float BadgeSlot(const TabItem& item) {
    if (item.badge.Empty()) return 0.0f;
    return MeasureInfoBadge(item.badge).w + 6.0f;
}

int MapMovedIndex(int index, size_t from, size_t to) noexcept {
    if (index < 0) return index;
    const int i = index;
    const int f = static_cast<int>(from);
    const int t = static_cast<int>(to);
    if (i == f) return t;
    if (f < t) {
        if (i > f && i <= t) return i - 1;
    } else if (t < f) {
        if (i >= t && i < f) return i + 1;
    }
    return i;
}
} // namespace

void TabControl::RelayoutParent() { Control::RelayoutParent(); }

StackPanel& TabControl::AddTab(std::wstring_view title) {
    return AddTab(TabItem{L"", std::wstring(title), L"", false});
}

const TabItem& TabControl::Tab(size_t index) const {
    static const TabItem empty;
    return index < items_.size() ? items_[index] : empty;
}

std::wstring TabControl::MakeId(std::wstring_view preferred) const {
    auto exists = [&](std::wstring_view value) {
        return std::any_of(items_.begin(), items_.end(),
                           [&](const TabItem& item) { return item.id == value; });
    };
    if (!preferred.empty() && !exists(preferred)) return std::wstring(preferred);
    size_t suffix = items_.size() + 1;
    std::wstring id;
    do {
        id = L"tab_" + std::to_wstring(suffix++);
    } while (exists(id));
    return id;
}

StackPanel& TabControl::AddTab(TabItem item) {
    item.id = MakeId(item.id);
    items_.push_back(std::move(item));
    StackPanel& page = this->Add<StackPanel>();
    page.Visible(items_.size() == 1);
    indicator_ready_ = false;
    RelayoutParent();
    return page;
}

float TabControl::StripContent() const {
    static const Theme kGeometry{};
    float total = 0.0f;
    for (size_t i = 0; i < items_.size(); ++i) total += TabWidth(i, kGeometry);
    return total;
}

bool TabControl::StripOverflows() const {
    return StripContent() > absolute_.w + 0.5f && absolute_.w > 0.5f;
}

float TabControl::StripViewport() const {
    const float extra = StripOverflows() ? kOverflowSlot : 0.0f;
    return std::max(0.0f, absolute_.w - extra);
}

float TabControl::MaxScroll() const {
    return std::max(0.0f, StripContent() - StripViewport());
}

void TabControl::ClampScroll() {
    scroll_x_ = Clamp(scroll_x_, 0.0f, MaxScroll());
}

void TabControl::EnsureSelectedVisible() {
    if (selected_ < 0 || static_cast<size_t>(selected_) >= items_.size()) return;
    static const Theme kGeometry{};
    float x = 0.0f;
    for (int i = 0; i < selected_; ++i) x += TabWidth(static_cast<size_t>(i), kGeometry);
    const float w = TabWidth(static_cast<size_t>(selected_), kGeometry);
    const float view = StripViewport();
    if (!(view > 0.5f)) return;
    if (x < scroll_x_) scroll_x_ = x;
    else if (x + w > scroll_x_ + view) scroll_x_ = x + w - view;
    ClampScroll();
}

void TabControl::IndicatorSlot(size_t index, float& x, float& w) {
    float cursor = 0.0f;
    static const Theme kGeometry{};
    for (size_t i = 0; i < items_.size(); ++i) {
        const float tw = TabWidth(i, kGeometry);
        if (i == index) {
            const float text_w = MeasureText(items_[i].title, TextRole::BodyStrong).w;
            const float leading = items_[i].glyph.empty() ? 0.0f : kGlyphSlot;
            const float trailing = (items_[i].closable ? kCloseSlot : 0.0f) + BadgeSlot(items_[i]);
            w = std::min(text_w, std::max(tw - kTabPadX * 2.0f - leading - trailing, 0.0f));
            x = cursor + (tw - w) * 0.5f;
            return;
        }
        cursor += tw;
    }
    x = 0.0f;
    w = 0.0f;
}

void TabControl::ApplyIndicator(float t) {
    indicator_x_ = Lerp(indicator_from_x_, indicator_to_x_, t);
    indicator_w_ = Lerp(indicator_from_w_, indicator_to_w_, t);
}

void TabControl::SnapIndicator() {
    if (items_.empty()) return;
    IndicatorSlot(static_cast<size_t>(selected_), indicator_to_x_, indicator_to_w_);
    indicator_from_x_ = indicator_to_x_;
    indicator_from_w_ = indicator_to_w_;
    slide_.Snap(1.0f);
    ApplyIndicator(1.0f);
    indicator_ready_ = true;
}

TabControl& TabControl::SelectedIndex(int index) {
    if (index < 0 || index >= static_cast<int>(items_.size()) || index == selected_) return *this;
    selected_ = index;
    ShowOnlySelected();
    EnsureSelectedVisible();
    IndicatorSlot(static_cast<size_t>(selected_), indicator_to_x_, indicator_to_w_);
    const float dur = kSlideSeconds * MotionScale();
    if (window_ && indicator_ready_ && dur > 0.001f) {
        indicator_from_x_ = indicator_x_;
        indicator_from_w_ = indicator_w_;
        slide_.Play(0.0f, 1.0f, dur, Ease::Material);
        Animate();
    } else {
        SnapIndicator();
    }
    Invalidate();
    changed_.Emit(static_cast<ptrdiff_t>(selected_), static_cast<ptrdiff_t>(selected_));
    return *this;
}

const std::wstring& TabControl::SelectedId() const noexcept {
    static const std::wstring empty;
    return selected_ >= 0 && selected_ < static_cast<int>(items_.size())
               ? items_[static_cast<size_t>(selected_)].id
               : empty;
}

TabControl& TabControl::SelectedId(std::wstring_view id) {
    for (size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].id == id) return SelectedIndex(static_cast<int>(i));
    }
    return *this;
}

TabControl& TabControl::TabBadge(std::wstring_view id, InfoBadgeData badge) {
    for (TabItem& item : items_) {
        if (item.id == id) {
            item.badge = std::move(badge);
            RelayoutParent();
            return *this;
        }
    }
    return *this;
}

InfoBadgeData TabControl::TabBadge(std::wstring_view id) const {
    for (const TabItem& item : items_) {
        if (item.id == id) return item.badge;
    }
    return {};
}

TabControl& TabControl::MoveTab(size_t from, size_t to) {
    if (from >= items_.size() || to >= items_.size() || from == to) return *this;
    if (from >= children_.size() || to >= children_.size()) return *this;
    TabItem item = std::move(items_[from]);
    items_.erase(items_.begin() + static_cast<ptrdiff_t>(from));
    items_.insert(items_.begin() + static_cast<ptrdiff_t>(to), std::move(item));
    std::unique_ptr<Control> page = std::move(children_[from]);
    children_.erase(children_.begin() + static_cast<ptrdiff_t>(from));
    children_.insert(children_.begin() + static_cast<ptrdiff_t>(to), std::move(page));
    selected_ = MapMovedIndex(selected_, from, to);
    hover_tab_ = MapMovedIndex(hover_tab_, from, to);
    indicator_ready_ = false;
    ShowOnlySelected();
    EnsureSelectedVisible();
    RelayoutParent();
    Invalidate();
    reordered_.Emit(from, to);
    return *this;
}

bool TabControl::CloseTab(std::wstring_view id) {
    size_t index = items_.size();
    for (size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].id == id) { index = i; break; }
    }
    if (index >= items_.size() || !items_[index].closable) return false;
    const std::wstring closing_id = items_[index].id;
    if (closing_ && !closing_(closing_id)) return false;
    const std::wstring previous_selected = SelectedId();
    Control* page = &Child(index);
    Remove(*page);
    items_.erase(items_.begin() + static_cast<ptrdiff_t>(index));
    if (items_.empty()) {
        selected_ = -1;
    } else if (static_cast<int>(index) < selected_) {
        --selected_;
    } else if (static_cast<int>(index) == selected_) {
        selected_ = std::min(static_cast<int>(index), static_cast<int>(items_.size()) - 1);
    }
    hover_tab_ = -1;
    press_tab_ = -1;
    drag_tab_ = -1;
    drop_slot_ = -1;
    indicator_ready_ = false;
    ShowOnlySelected();
    ClampScroll();
    EnsureSelectedVisible();
    RelayoutParent();
    Invalidate();
    closed_.Emit(closing_id);
    if (SelectedId() != previous_selected) changed_.Emit(static_cast<ptrdiff_t>(selected_), static_cast<ptrdiff_t>(selected_));
    return true;
}

bool TabControl::OnAnimate(float dt) {
    if (!slide_.running) return Control::OnAnimate(dt);
    const bool more = slide_.Tick(dt);
    ApplyIndicator(slide_.Value());
    return more || Control::OnAnimate(dt);
}

void TabControl::ShowOnlySelected() {
    for (size_t i = 0; i < children_.size(); ++i) {
        SetChildVisibility(i, static_cast<int>(i) == selected_);
    }
}

float TabControl::TabWidth(size_t index, const Theme& theme) const {
    (void)theme;
    const TabItem& item = items_[index];
    return MeasureText(item.title, TextRole::BodyStrong).w + kTabPadX * 2.0f +
           (item.glyph.empty() ? 0.0f : kGlyphSlot) + (item.closable ? kCloseSlot : 0.0f) +
           BadgeSlot(item);
}

bool TabControl::OverflowAt(Point local) const {
    if (!StripOverflows() || local.y < 0.0f || local.y >= kStripHeight) return false;
    return local.x >= StripViewport() && local.x < absolute_.w;
}

int TabControl::TabAt(Point local, const Theme& theme) {
    if (local.y < 0.0f || local.y >= kStripHeight) return -1;
    if (OverflowAt(local)) return -1;
    float x = -scroll_x_;
    for (size_t i = 0; i < items_.size(); ++i) {
        const float w = TabWidth(i, theme);
        if (local.x >= x && local.x < x + w) return static_cast<int>(i);
        x += w;
    }
    return -1;
}

bool TabControl::CloseAt(Point local, size_t index, const Theme& theme) {
    if (index >= items_.size() || !items_[index].closable || local.y < 0.0f ||
        local.y >= kStripHeight) return false;
    float x = -scroll_x_;
    for (size_t i = 0; i < index; ++i) x += TabWidth(i, theme);
    const float w = TabWidth(index, theme);
    return local.x >= x + w - kCloseSlot && local.x < x + w;
}

int TabControl::DropSlotAt(Point local, const Theme& theme) {
    if (items_.empty()) return 0;
    float x = -scroll_x_;
    for (size_t i = 0; i < items_.size(); ++i) {
        const float w = TabWidth(i, theme);
        if (local.x < x + w * 0.5f) return static_cast<int>(i);
        x += w;
    }
    return static_cast<int>(items_.size());
}

Size TabControl::Measure(Size, const Theme&) {
    static const Theme kGeometryDefault{};
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
    ClampScroll();
    static const Theme kGeometryDefault{};
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
    if (!indicator_ready_) SnapIndicator();

    const float view = StripViewport();
    const Rect strip_clip{absolute_.x, absolute_.y, view, kStripHeight};
    painter.PushClip(strip_clip);
    float x = absolute_.x - scroll_x_;
    for (size_t i = 0; i < items_.size(); ++i) {
        const float w = TabWidth(i, theme);
        const Rect tab{x, absolute_.y, w, kStripHeight};
        const bool selected = static_cast<int>(i) == selected_;
        const bool hovered = static_cast<int>(i) == hover_tab_ && drag_tab_ < 0;
        const bool dragging = static_cast<int>(i) == drag_tab_;
        if (dragging || (hovered && !selected)) {
            painter.FillRoundedRect(tab.Inset(2.0f, 4.0f), 8.0f,
                                    dragging ? theme.fill_selected : theme.fill_hover);
        }
        const TextRole role = selected ? TextRole::BodyStrong : TextRole::Body;
        float content_x = tab.x + kTabPadX;
        if (!items_[i].glyph.empty()) {
            painter.DrawIcon(items_[i].glyph, {content_x, tab.y, 16.0f, tab.h}, 15.0f,
                             selected || hovered || dragging ? theme.text : theme.text_secondary);
            content_x += kGlyphSlot;
        }
        const float close_w = items_[i].closable ? kCloseSlot : 0.0f;
        const float badge_w = BadgeSlot(items_[i]);
        painter.DrawText(items_[i].title,
                         {content_x, tab.y, std::max(0.0f, tab.Right() - close_w - badge_w -
                                                              kTabPadX - content_x), tab.h},
                         role,
                         selected || hovered || dragging ? theme.text : theme.text_secondary,
                         Align::Center);
        if (!items_[i].badge.Empty()) {
            PaintInfoBadge(painter, theme,
                           {tab.Right() - close_w - badge_w * 0.5f, tab.y + tab.h * 0.5f},
                           items_[i].badge);
        }
        if (items_[i].closable) {
            const Rect close{tab.Right() - kCloseSlot, tab.y, kCloseSlot, tab.h};
            if (hovered) painter.FillRoundedRect(close.Inset(4.0f, 6.0f), 6.0f, theme.fill_hover);
            painter.DrawIcon(icon::kClose, close, 12.0f,
                             hovered ? theme.text : theme.text_secondary);
        }
        if (selected && FocusVisible() && drag_tab_ < 0) {
            PaintFocusRing(painter, theme, tab.Inset(2.0f, 4.0f), 8.0f);
        }
        x += w;
    }
    if (indicator_w_ > 0.5f && drag_tab_ < 0) {
        const Rect indicator{absolute_.x + indicator_x_ - scroll_x_,
                             absolute_.y + kStripHeight - kIndicatorHeight, indicator_w_,
                             kIndicatorHeight};
        painter.DrawGlow(indicator, kIndicatorHeight * 0.5f, theme.glow_sm);
        painter.FillRoundedRect(indicator, kIndicatorHeight * 0.5f, theme.accent);
    }
    if (drag_tab_ >= 0 && drop_slot_ >= 0) {
        float mark = absolute_.x - scroll_x_;
        static const Theme kGeometry{};
        const int slot = std::min(drop_slot_, static_cast<int>(items_.size()));
        for (int i = 0; i < slot; ++i) mark += TabWidth(static_cast<size_t>(i), kGeometry);
        painter.FillRect({mark - 1.0f, absolute_.y + 6.0f, 2.0f, kStripHeight - 12.0f},
                         theme.accent);
    }
    painter.PopClip();

    painter.FillRect({absolute_.x, absolute_.y + kStripHeight - 1.0f, absolute_.w, 1.0f},
                     theme.stroke_divider);
    if (StripOverflows()) {
        const Rect more{absolute_.x + view, absolute_.y, kOverflowSlot, kStripHeight};
        if (hover_overflow_) painter.FillRoundedRect(more.Inset(2.0f, 4.0f), 8.0f, theme.fill_hover);
        painter.DrawText(L"⋯", more, TextRole::BodyStrong, theme.text, Align::Center);
    }
}

bool TabControl::OnKey(uint32_t vk) {
    if (items_.empty()) return false;
    if (vk == VK_LEFT || vk == VK_RIGHT) {
        const int direction = vk == VK_RIGHT ? 1 : -1;
        const int next = Clamp(selected_ + direction, 0, static_cast<int>(items_.size()) - 1);
        SelectedIndex(next);
        return true;
    }
    if (vk == VK_HOME) {
        SelectedIndex(0);
        return true;
    }
    if (vk == VK_END) {
        SelectedIndex(static_cast<int>(items_.size()) - 1);
        return true;
    }
    if (vk == VK_DOWN && StripOverflows()) {
        OpenOverflow();
        return true;
    }
    if ((vk == VK_DELETE || vk == 'W') && selected_ >= 0 &&
        (vk == VK_DELETE || (GetKeyState(VK_CONTROL) & 0x8000) != 0)) {
        return CloseTab(SelectedId());
    }
    return false;
}

void TabControl::OpenOverflow() {
    Window* window = WindowOf();
    if (!window || items_.empty()) return;
    Menu menu;
    for (size_t i = 0; i < items_.size(); ++i) {
        MenuItem row;
        row.text = items_[i].title;
        row.glyph = items_[i].glyph;
        row.checked = static_cast<int>(i) == selected_;
        const std::wstring id = items_[i].id;
        row.action = [this, id] { SelectedId(id); };
        menu.AddItem(std::move(row));
    }
    const float view = StripViewport();
    menu.Popup(*window, {absolute_.x + view, absolute_.y + kStripHeight});
}

void TabControl::EndDrag() {
    const int from = drag_tab_;
    int slot = drop_slot_;
    drag_tab_ = -1;
    drop_slot_ = -1;
    press_tab_ = -1;
    if (from < 0 || slot < 0) {
        Invalidate();
        return;
    }
    int to = slot;
    if (from < slot) --to;
    if (to < 0) to = 0;
    if (to >= static_cast<int>(items_.size())) to = static_cast<int>(items_.size()) - 1;
    if (to != from) MoveTab(static_cast<size_t>(from), static_cast<size_t>(to));
    else Invalidate();
}

void TabControl::OnMouseMove(Point local, uint32_t buttons) {
    last_pointer_y_ = local.y;
    static const Theme kGeometryDefault{};
    if (press_tab_ >= 0 && (buttons & kLeftButton) && can_reorder_ && drag_tab_ < 0) {
        if (std::fabs(local.x - press_local_.x) >= kDragSlop ||
            std::fabs(local.y - press_local_.y) >= kDragSlop) {
            drag_tab_ = press_tab_;
            drop_slot_ = drag_tab_;
        }
    }
    if (drag_tab_ >= 0) {
        const float view = StripViewport();
        if (StripOverflows() && view > 8.0f) {
            if (local.x < 16.0f) scroll_x_ -= 12.0f;
            else if (local.x > view - 16.0f) scroll_x_ += 12.0f;
            ClampScroll();
        }
        drop_slot_ = DropSlotAt(local, kGeometryDefault);
        Invalidate();
        return;
    }
    const bool over = OverflowAt(local);
    const int tab = TabAt(local, kGeometryDefault);
    if (tab != hover_tab_ || over != hover_overflow_) {
        hover_tab_ = tab;
        hover_overflow_ = over;
        Invalidate();
    }
}

void TabControl::OnMouseDown(Point local, uint32_t buttons) {
    last_pointer_y_ = local.y;
    Focus();
    static const Theme kGeometryDefault{};
    if (buttons & kMiddleButton) {
        const int tab = TabAt(local, kGeometryDefault);
        if (tab >= 0) CloseTab(items_[static_cast<size_t>(tab)].id);
        return;
    }
    if (!(buttons & kLeftButton)) return;
    if (OverflowAt(local)) {
        OpenOverflow();
        return;
    }
    const int tab = TabAt(local, kGeometryDefault);
    if (tab < 0) return;
    if (CloseAt(local, static_cast<size_t>(tab), kGeometryDefault)) {
        CloseTab(items_[static_cast<size_t>(tab)].id);
        return;
    }
    press_tab_ = tab;
    press_local_ = local;
    SelectedIndex(tab);
}

void TabControl::OnMouseUp(Point local, uint32_t buttons) {
    (void)local;
    (void)buttons;
    if (drag_tab_ >= 0) EndDrag();
    else press_tab_ = -1;
}

void TabControl::OnMouseLeave() {
    Panel::OnMouseLeave();
    last_pointer_y_ = -1.0f;
    if (drag_tab_ >= 0) return;
    press_tab_ = -1;
    if (hover_tab_ != -1 || hover_overflow_) {
        hover_tab_ = -1;
        hover_overflow_ = false;
        Invalidate();
    }
}

bool TabControl::OnWheel(float delta) {
    if (!StripOverflows()) return false;
    if (last_pointer_y_ < 0.0f || last_pointer_y_ >= kStripHeight) return false;
    scroll_x_ = Clamp(scroll_x_ - delta * kWheelStep, 0.0f, MaxScroll());
    Invalidate();
    return true;
}

bool TabControl::OnHWheel(float delta) {
    if (!StripOverflows()) return false;
    scroll_x_ = Clamp(scroll_x_ + delta * kWheelStep, 0.0f, MaxScroll());
    Invalidate();
    return true;
}

bool TabControl::CapturesOverlay(Point p) const {
    return p.y >= 0.0f && p.y < kStripHeight && p.x >= 0.0f && p.x < absolute_.w;
}

bool TabControl::PrefersDragOverPan() const noexcept {
    return drag_tab_ >= 0 || press_tab_ >= 0;
}

TabControl& TabControl::BindSelectedIndex(Property<int>& p) {
    auto apply = [this, &p] {
        if (bind_loop_) return;
        bind_loop_ = true;
        SelectedIndex(p.Get());
        bind_loop_ = false;
    };
    apply();
    index_prop_ = ScopedConnection(p.OnChanged([apply](const int&) { apply(); }));
    index_ctrl_ = ScopedConnection(changed_.Connect([this, &p](ptrdiff_t, ptrdiff_t) {
        if (bind_loop_) return;
        bind_loop_ = true;
        p = selected_;
        bind_loop_ = false;
    }));
    return *this;
}

} // namespace lumen
