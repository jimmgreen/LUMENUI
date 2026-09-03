#include "lumen/ListView.h"
#include "lumen/EmptyState.h"
#include "lumen/ItemsModel.h"
#include "lumen/EmptyState.h"
#include "lumen/Panel.h"
#include "lumen/Painter.h"
#include "lumen/Animate.h"
#include "../core/com_ptr.h"
#include "../core/text_service.h"
#include <windows.h>
#include <d2d1_3.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>

namespace lumen {
namespace {
constexpr float kBarHit = 10.0f;
constexpr float kGroupHeight = 32.0f;
constexpr uint32_t kShiftFlag = 0x0004;   // MK_SHIFT
constexpr uint32_t kCtrlFlag = 0x0008;    // MK_CONTROL
constexpr float kEnterDur = 0.20f;
constexpr float kEnterStagger = 0.02f;
constexpr float kEnterLift = 8.0f;
constexpr float kEnterCap = 12.0f;
constexpr float kDragSlop = 8.0f;
constexpr float kSwipePad = 12.0f;
constexpr float kSwipeCommit = 36.0f;
constexpr uint32_t kLeftButton = 0x0001;

Color Fade(Color c, float t) noexcept {
    c.a *= t;
    return c;
}

bool KeyHeld(int vk) noexcept {
    return (GetKeyState(vk) & 0x8000) != 0;
}

bool ShiftHeld(uint32_t buttons) noexcept {
    return (buttons & kShiftFlag) != 0 || KeyHeld(VK_SHIFT);
}

bool CtrlHeld(uint32_t buttons) noexcept {
    return (buttons & kCtrlFlag) != 0 || KeyHeld(VK_CONTROL);
}

ptrdiff_t MapMovedIndex(ptrdiff_t i, size_t from, size_t to) noexcept {
    if (i < 0) return i;
    const size_t u = static_cast<size_t>(i);
    if (u == from) return static_cast<ptrdiff_t>(to);
    if (from < to) {
        if (u > from && u <= to) return i - 1;
    } else if (u >= to && u < from) {
        return i + 1;
    }
    return i;
}
} // namespace

struct ListView::DrawCache {
    ComPtr<ID2D1CommandList> list;
    void* device = nullptr;
    float scroll = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    ptrdiff_t first = -1;
    ptrdiff_t last = -1;
    ptrdiff_t selected = -1;
    ptrdiff_t hover = -1;
    size_t count = 0;
    uint64_t sel = 0;
    bool enabled = true;
};

ListView::ListView() = default;
ListView::~ListView() = default;

void ListView::RelayoutParent() { Control::RelayoutParent(); }

void ListView::EnsureOrder() {
    if (order_.size() == item_count_) return;
    order_.resize(item_count_);
    for (size_t i = 0; i < item_count_; ++i) order_[i] = i;
}

void ListView::RemapSelection(size_t from, size_t to) {
    selected_ = MapMovedIndex(selected_, from, to);
    keyboard_anchor_ = MapMovedIndex(keyboard_anchor_, from, to);
    for (ptrdiff_t& row : selected_set_) row = MapMovedIndex(row, from, to);
    std::sort(selected_set_.begin(), selected_set_.end());
    selected_set_.erase(std::unique(selected_set_.begin(), selected_set_.end()),
                        selected_set_.end());
}

ListView& ListView::MoveItem(size_t from, size_t to) {
    if (from >= item_count_ || to >= item_count_ || from == to) return *this;
    if (!groups_.empty()) {
        if (GroupForItem(static_cast<ptrdiff_t>(from)) !=
            GroupForItem(static_cast<ptrdiff_t>(to))) {
            return *this;
        }
    }
    EnsureOrder();
    const size_t value = order_[from];
    order_.erase(order_.begin() + static_cast<ptrdiff_t>(from));
    order_.insert(order_.begin() + static_cast<ptrdiff_t>(to), value);
    RemapSelection(from, to);
    Invalidate();
    reordered_.Emit(from, to);
    return *this;
}

EmptyState& ListView::EnsureEmpty() {
    if (!empty_) {
        empty_ = &Add<EmptyState>();
        empty_->Visible(false);
    }
    return *empty_;
}

void ListView::SyncEmpty() {
    if (!empty_) return;
    empty_->Visible(item_count_ == 0);
}

ListView& ListView::EmptyTitle(std::wstring_view value) {
    EnsureEmpty().Title(value);
    SyncEmpty();
    RelayoutParent();
    return *this;
}

ListView& ListView::EmptyHint(std::wstring_view value) {
    EnsureEmpty().Hint(value);
    SyncEmpty();
    RelayoutParent();
    return *this;
}

ListView& ListView::EmptyGlyph(std::wstring_view value) {
    EnsureEmpty().Glyph(value);
    SyncEmpty();
    RelayoutParent();
    return *this;
}

ListView& ListView::EmptyAction(std::wstring_view label, std::function<void()> on_click) {
    EnsureEmpty().Action(label, std::move(on_click));
    SyncEmpty();
    RelayoutParent();
    return *this;
}

ListView& ListView::AnimateInserted(size_t index) {
    enter_playing_ = false;
    if (index >= item_count_) return *this;
    mut_done_ = {};
    mut_kind_ = MutKind::Insert;
    mut_index_ = static_cast<ptrdiff_t>(index);
    if (MotionScale() <= 0.001f) {
        FinishMut();
        return *this;
    }
    mut_tween_.Play(0.0f, 1.0f, 0.24f, Ease::CssEaseOut);
    Animate();
    Invalidate();
    return *this;
}

ListView& ListView::AnimateRemoved(size_t index, std::function<void()> done) {
    enter_playing_ = false;
    mut_done_ = std::move(done);
    if (index >= item_count_) {
        FinishMut();
        return *this;
    }
    mut_kind_ = MutKind::Remove;
    mut_index_ = static_cast<ptrdiff_t>(index);
    if (MotionScale() <= 0.001f) {
        FinishMut();
        return *this;
    }
    mut_tween_.Play(1.0f, 0.0f, 0.20f, Ease::CssEaseIn);
    Animate();
    Invalidate();
    return *this;
}

void ListView::FinishMut() {
    mut_kind_ = MutKind::None;
    mut_index_ = -1;
    mut_tween_.Snap(1.0f);
    auto done = std::move(mut_done_);
    if (done) done();
    Invalidate();
}

bool ListView::HasSwipe() const noexcept {
    return static_cast<bool>(swipe_leading_.invoke) ||
           static_cast<bool>(swipe_trailing_.invoke) ||
           !swipe_leading_.label.empty() || !swipe_trailing_.label.empty();
}

float ListView::SwipeActionWidth(const ListSwipeAction& action) const {
    float w = kSwipePad * 2.0f;
    if (!action.glyph.empty()) w += 22.0f;
    if (!action.label.empty()) w += MeasureText(action.label, TextRole::CaptionStrong).w;
    return std::max(w, 56.0f);
}

bool ListView::LivePaint() const noexcept {
    return enter_playing_ || !groups_.empty() || !order_.empty() || item_count_ == 0 ||
           swipe_row_ >= 0 || reorder_dragging_ || mut_kind_ != MutKind::None ||
           std::fabs(swipe_x_) > 0.5f;
}

ListView& ListView::ItemCount(size_t count, bool play_enter) {
    groups_.clear();
    group_offsets_.clear();
    order_.clear();
    item_count_ = count;
    swipe_row_ = -1;
    swipe_x_ = 0.0f;
    mut_kind_ = MutKind::None;
    mut_index_ = -1;
    mut_done_ = {};
    model_cache_ = -1;
    FilterSelection();
    SyncEmpty();
    ClampScroll();
    if (play_enter) BeginEnter();
    RelayoutParent();
    return *this;
}

ListView& ListView::Bind(ItemsModel& model) {
    owned_model_.reset();
    model_ = &model;
    model_cache_ = -1;
    item_text_ = [this](size_t i, std::wstring& s) {
        if (!model_) {
            s.clear();
            return;
        }
        if (model_cache_ != static_cast<ptrdiff_t>(i)) {
            model_->Get(i, model_row_);
            model_cache_ = static_cast<ptrdiff_t>(i);
        }
        s = model_row_.text;
    };
    item_glyph_ = [this](size_t i, std::wstring& s) {
        if (!model_) {
            s.clear();
            return;
        }
        if (model_cache_ != static_cast<ptrdiff_t>(i)) {
            model_->Get(i, model_row_);
            model_cache_ = static_cast<ptrdiff_t>(i);
        }
        s = model_row_.glyph;
    };
    model_inserted_ = ScopedConnection(model.OnInserted([this](size_t index, size_t n) {
        model_cache_ = -1;
        if (!model_) return;
        ItemCount(model_->Count(), false);
        if (n == 1) AnimateInserted(index);
    }));
    model_removed_ = ScopedConnection(model.OnRemoved([this](size_t, size_t) {
        model_cache_ = -1;
        if (model_) ItemCount(model_->Count(), false);
    }));
    model_changed_ = ScopedConnection(model.OnChanged([this](size_t, size_t) {
        model_cache_ = -1;
        Invalidate();
    }));
    model_reset_ = ScopedConnection(model.OnReset([this] {
        model_cache_ = -1;
        if (model_) ItemCount(model_->Count(), false);
    }));
    ItemCount(model.Count(), false);
    model_detached_ = ScopedConnection(model.OnDetached([this] {
        model_ = nullptr;
        owned_model_.reset();
        ItemCount(0, false);
    }));
    return *this;
}

ListView& ListView::Bind(std::shared_ptr<ItemsModel> model) {
    if (!model) return *this;
    ItemsModel& ref = *model;
    Bind(ref);
    owned_model_ = std::move(model);
    return *this;
}

ListView& ListView::Groups(std::vector<ListGroup> groups) {
    order_.clear();
    groups_ = std::move(groups);
    RebuildGroups();
    FilterSelection();
    SyncEmpty();
    ClampScroll();
    BeginEnter();
    RelayoutParent();
    return *this;
}

void ListView::RebuildGroups() {
    group_offsets_.clear();
    group_offsets_.reserve(groups_.size());
    size_t offset = 0;
    for (const ListGroup& group : groups_) {
        group_offsets_.push_back(offset);
        offset += group.item_count;
    }
    item_count_ = offset;
    if (focus_group_ >= static_cast<ptrdiff_t>(groups_.size())) focus_group_ = -1;
}

ListView& ListView::GroupExpanded(std::wstring_view id, bool expanded) {
    for (size_t i = 0; i < groups_.size(); ++i) {
        if (groups_[i].id != id || groups_[i].expanded == expanded) continue;
        groups_[i].expanded = expanded;
        ClampScroll();
        Invalidate();
        group_expanded_changed_.Emit(groups_[i].id, expanded);
        break;
    }
    return *this;
}

bool ListView::GroupExpanded(std::wstring_view id) const {
    for (const ListGroup& group : groups_) if (group.id == id) return group.expanded;
    return false;
}

float ListView::RowExtent(size_t index) const {
    const float row_h = std::max(theme_row_height_, 1.0f);
    if (groups_.empty() && mut_kind_ != MutKind::None &&
        mut_index_ == static_cast<ptrdiff_t>(index)) {
        return row_h * mut_tween_.Value();
    }
    return row_h;
}

float ListView::ContentHeight() const {
    const float row_h = std::max(theme_row_height_, 1.0f);
    if (groups_.empty()) {
        float content = static_cast<float>(item_count_) * row_h;
        if (mut_kind_ != MutKind::None && mut_index_ >= 0) {
            content -= row_h * (1.0f - mut_tween_.Value());
        }
        return std::max(0.0f, content);
    }
    float content = 0.0f;
    for (const ListGroup& group : groups_) {
        content += kGroupHeight;
        if (group.expanded) content += static_cast<float>(group.item_count) * row_h;
    }
    return content;
}

float ListView::MaxScroll() const {
    return std::max(0.0f, ContentHeight() - absolute_.h);
}

bool ListView::IsSelected(size_t index) const {
    const ptrdiff_t row = static_cast<ptrdiff_t>(index);
    if (row < 0 || row >= static_cast<ptrdiff_t>(item_count_)) return false;
    if (!multi_select_) return row == selected_;
    return std::binary_search(selected_set_.begin(), selected_set_.end(), row);
}

std::vector<size_t> ListView::SelectedIndices() const {
    std::vector<size_t> out;
    if (multi_select_) {
        out.reserve(selected_set_.size());
        for (ptrdiff_t row : selected_set_) {
            if (row >= 0) out.push_back(static_cast<size_t>(row));
        }
    } else if (selected_ >= 0) {
        out.push_back(static_cast<size_t>(selected_));
    }
    return out;
}

ListView& ListView::SelectedIndex(ptrdiff_t index) {
    if (index < -1 || index >= static_cast<ptrdiff_t>(item_count_)) return *this;
    const bool set_differs =
        multi_select_ &&
        (selected_set_.size() != 1 ||
         (index >= 0 && selected_set_.front() != index));
    if (selected_ == index && !set_differs) return *this;
    selected_ = index;
    focus_group_ = -1;
    keyboard_anchor_ = index;
    if (multi_select_) {
        selected_set_.clear();
        if (index >= 0) selected_set_.push_back(index);
    }
    if (index >= 0) ScrollTo(static_cast<size_t>(index));
    Invalidate();
    selection_changed_.Emit(selected_, SelectedDataIndex());
    return *this;
}

void ListView::SelectedIndices(std::vector<ptrdiff_t> indices) {
    indices.erase(std::remove_if(indices.begin(), indices.end(),
                                 [this](ptrdiff_t i) {
                                     return i < 0 || i >= static_cast<ptrdiff_t>(item_count_);
                                 }),
                  indices.end());
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    if (!multi_select_) {
        SelectedIndex(indices.empty() ? ptrdiff_t{-1} : indices.back());
        return;
    }
    selected_set_ = std::move(indices);
    selected_ = selected_set_.empty() ? ptrdiff_t{-1}
                                      : static_cast<ptrdiff_t>(selected_set_.back());
    keyboard_anchor_ = selected_;
    if (selected_ >= 0) ScrollTo(static_cast<size_t>(selected_));
    Invalidate();
    selection_changed_.Emit(selected_, SelectedDataIndex());
}

void ListView::ClearSelection() {
    if (selected_ < 0 && selected_set_.empty()) return;
    selected_ = -1;
    keyboard_anchor_ = -1;
    selected_set_.clear();
    Invalidate();
    selection_changed_.Emit(selected_, SelectedDataIndex());
}

void ListView::FilterSelection() {
    selected_set_.erase(std::remove_if(selected_set_.begin(), selected_set_.end(),
                                       [this](ptrdiff_t i) {
                                           return i < 0 ||
                                                  i >= static_cast<ptrdiff_t>(item_count_);
                                       }),
                        selected_set_.end());
    if (item_count_ == 0) {
        selected_ = -1;
        keyboard_anchor_ = -1;
        return;
    }
    const ptrdiff_t max_i = static_cast<ptrdiff_t>(item_count_) - 1;
    if (selected_ > max_i) {
        selected_ = multi_select_ && !selected_set_.empty() ? selected_set_.back() : max_i;
    }
    if (keyboard_anchor_ > max_i) keyboard_anchor_ = selected_;
}

void ListView::SelectRangeTo(ptrdiff_t row) {
    const ptrdiff_t anchor = keyboard_anchor_ < 0 ? row : keyboard_anchor_;
    selected_set_.clear();
    for (ptrdiff_t i = std::min(anchor, row); i <= std::max(anchor, row); ++i) {
        selected_set_.push_back(i);
    }
    selected_ = row;
    ScrollTo(static_cast<size_t>(row));
    Invalidate();
    selection_changed_.Emit(selected_, SelectedDataIndex());
}

void ListView::ToggleSelected(ptrdiff_t row) {
    const auto it = std::lower_bound(selected_set_.begin(), selected_set_.end(), row);
    if (it != selected_set_.end() && *it == row) {
        selected_set_.erase(it);
    } else {
        selected_set_.insert(it, row);
    }
    selected_ = row;   // 焦点跟随点击，范围锚不动
    Invalidate();
    selection_changed_.Emit(selected_, SelectedDataIndex());
}

void ListView::ScrollTo(size_t index) {
    if (absolute_.IsEmpty()) return;   // 首次布局前没有视口尺寸
    const float top = ItemTop(index);
    if (top < 0.0f) return;
    const float bottom = top + RowExtent(index);
    if (top < scroll_offset_) {
        target_offset_ = top;
    } else if (bottom > scroll_offset_ + absolute_.h) {
        target_offset_ = bottom - absolute_.h;
    }
    target_offset_ = Clamp(target_offset_, 0.0f, MaxScroll());
    Animate();
}

void ListView::ClampScroll() {
    target_offset_ = Clamp(target_offset_, 0.0f, MaxScroll());
    scroll_offset_ = Clamp(scroll_offset_, 0.0f, MaxScroll());
}

Size ListView::Measure(Size available, const Theme& theme) {
    theme_row_height_ = theme.list_row_height;
    SyncEmpty();
    if (empty_ && empty_->Visible()) {
        for (size_t i = 0; i < ChildCount(); ++i) {
            if (&Child(i) == empty_) {
                MeasureChildAt(i, available, theme);
                break;
            }
        }
    }
    const float w = (available.w > 0.0f && available.w < 1.0e4f) ? available.w : 200.0f;
    const float h = (available.h > 0.0f && available.h < 1.0e4f) ? available.h : 200.0f;
    return {w, h};
}

void ListView::Arrange(const Rect& absolute) {
    absolute_ = absolute;
    SyncEmpty();
    if (empty_ && empty_->Visible()) {
        SetChildBounds(*empty_, {0.0f, 0.0f, absolute.w, absolute.h});
        for (size_t i = 0; i < ChildCount(); ++i) {
            if (&Child(i) == empty_) {
                ArrangeChildAt(i);
                break;
            }
        }
    }
}

void ListView::OnFocusChanged(bool focused) {
    Control::OnFocusChanged(focused);
    Invalidate();
}

bool ListView::OnAnimate(float dt_seconds) {
    bool moving = EaseTo(scroll_offset_, target_offset_, dt_seconds, 20.0f, 0.1f);
    moving |= EaseTo(expand_progress_, (hovered_ || dragging_) ? 1.0f : 0.0f, dt_seconds, 18.0f);
    if (!swipe_dragging_ && std::fabs(swipe_x_) > 0.01f) {
        moving |= EaseTo(swipe_x_, 0.0f, dt_seconds, 22.0f, 0.4f);
        if (std::fabs(swipe_x_) <= 0.4f) {
            swipe_x_ = 0.0f;
            swipe_row_ = -1;
        }
        Invalidate();
    }
    if (enter_playing_) {
        enter_elapsed_ += dt_seconds;
        if (enter_elapsed_ >= kEnterCap * kEnterStagger + kEnterDur) {
            enter_playing_ = false;
        } else {
            moving = true;
            Invalidate();
        }
    }
    if (mut_kind_ != MutKind::None) {
        if (mut_tween_.Tick(dt_seconds)) {
            moving = true;
            Invalidate();
        } else {
            FinishMut();
            moving = true;
        }
    }
    return moving || Control::OnAnimate(dt_seconds);
}

void ListView::BeginEnter() {
    if (!window_ || MotionScale() <= 0.001f) {
        enter_playing_ = false;
        enter_elapsed_ = 10.0f;
        return;
    }
    enter_playing_ = true;
    enter_elapsed_ = 0.0f;
    Animate();
}

float ListView::ItemEnter(size_t visible_index) const noexcept {
    if (!enter_playing_) return 1.0f;
    const float delay = std::min(static_cast<float>(visible_index), kEnterCap) * kEnterStagger;
    return EaseAt(Clamp((enter_elapsed_ - delay) / kEnterDur, 0.0f, 1.0f), Ease::CssEaseOut);
}

Rect ListView::VerticalTrack() const noexcept {
    return {absolute_.Right() - kBarHit, absolute_.y, kBarHit, absolute_.h};
}

ScrollThumb ListView::Thumb(float expand) const noexcept {
    return MakeScrollThumb(absolute_, ContentHeight(), scroll_offset_, expand, true);
}

bool ListView::CapturesOverlay(Point p) const {
    if (dragging_ || swipe_dragging_ || reorder_dragging_) return true;
    if (MaxScroll() <= 0.5f) return false;
    return VerticalTrack().Contains(p);
}

bool ListView::BeginScrollDrag(Point local) {
    if (MaxScroll() <= 0.5f) return false;
    const Point world{absolute_.x + local.x, absolute_.y + local.y};
    if (!VerticalTrack().Contains(world)) return false;
    const ScrollThumb thumb = Thumb(1.0f);
    dragging_ = true;
    if (thumb.visible && thumb.rect.Contains(world)) {
        drag_grab_ = world.y - thumb.rect.y;
    } else {
        const float thumb_h = thumb.visible ? thumb.rect.h : 20.0f;
        const float track = std::max(1.0f, absolute_.h - thumb_h);
        const float t = Clamp((local.y - thumb_h * 0.5f) / track, 0.0f, 1.0f);
        scroll_offset_ = target_offset_ = t * MaxScroll();
        drag_grab_ = thumb_h * 0.5f;
        Invalidate();
    }
    Animate();
    return true;
}

void ListView::MoveSelection(ptrdiff_t delta) {
    if (item_count_ == 0) return;
    if (!groups_.empty()) {
        MoveGroupedFocus(delta < 0 ? -1 : 1);
        return;
    }
    const ptrdiff_t next =
        selected_ < 0 ? (delta > 0 ? 0 : static_cast<ptrdiff_t>(item_count_) - 1)
                      : Clamp(selected_ + delta, ptrdiff_t{0},
                              static_cast<ptrdiff_t>(item_count_) - 1);
    if (next == selected_) return;
    if (multi_select_ && (GetKeyState(VK_SHIFT) & 0x8000) != 0) {
        SelectRangeTo(next);
    } else {
        SelectedIndex(next);
    }
}

void ListView::SelectJump(ptrdiff_t index, bool to_end) {
    if (item_count_ == 0) return;
    if (multi_select_ && (GetKeyState(VK_SHIFT) & 0x8000) != 0) {
        SelectRangeTo(index);
    } else {
        SelectedIndex(index);
    }
    // Home/End 直接贴到内容端点
    target_offset_ = to_end ? MaxScroll() : 0.0f;
    Animate();
}

bool ListView::OnKey(uint32_t vk) {
    const float row_h = std::max(theme_row_height_, 1.0f);
    const ptrdiff_t page =
        std::max(ptrdiff_t{1}, static_cast<ptrdiff_t>(absolute_.h / row_h));
    switch (vk) {
    case VK_DOWN:
        MoveSelection(1);
        return true;
    case VK_UP:
        MoveSelection(-1);
        return true;
    case VK_PRIOR:
        MoveSelection(-page);
        return true;
    case VK_NEXT:
        MoveSelection(page);
        return true;
    case VK_HOME:
        if (!groups_.empty()) {
            focus_group_ = 0;
            target_offset_ = 0.0f;
            Animate();
            Invalidate();
            return true;
        }
        SelectJump(0, false);
        return true;
    case VK_END:
        if (!groups_.empty()) {
            for (ptrdiff_t g = static_cast<ptrdiff_t>(groups_.size()) - 1; g >= 0; --g) {
                if (groups_[static_cast<size_t>(g)].expanded &&
                    groups_[static_cast<size_t>(g)].item_count > 0) {
                    focus_group_ = -1;
                    SelectedIndex(static_cast<ptrdiff_t>(group_offsets_[static_cast<size_t>(g)] +
                                                             groups_[static_cast<size_t>(g)].item_count - 1));
                    return true;
                }
            }
            focus_group_ = static_cast<ptrdiff_t>(groups_.size()) - 1;
            Invalidate();
            return true;
        }
        SelectJump(static_cast<ptrdiff_t>(item_count_) - 1, true);
        return true;
    case 'A':
        if (multi_select_ && (GetKeyState(VK_CONTROL) & 0x8000) != 0 && item_count_) {
            std::vector<ptrdiff_t> all(item_count_);
            for (size_t i = 0; i < item_count_; ++i) all[i] = static_cast<ptrdiff_t>(i);
            SelectedIndices(std::move(all));
            return true;
        }
        return false;
    case VK_ESCAPE:
        if (multi_select_ && !selected_set_.empty()) {
            ClearSelection();
            return true;
        }
        return false;
    case VK_RETURN:
        if (focus_group_ >= 0) {
            ToggleGroup(static_cast<size_t>(focus_group_));
            return true;
        }
        if (selected_ >= 0 && selected_ < static_cast<ptrdiff_t>(item_count_)) {
            activate_.Emit(static_cast<size_t>(selected_));
        }
        return true;
    default:
        if (!groups_.empty() && focus_group_ >= 0 && (vk == VK_LEFT || vk == VK_RIGHT)) {
            const bool expand = vk == VK_RIGHT;
            GroupExpanded(groups_[static_cast<size_t>(focus_group_)].id, expand);
            return true;
        }
        return false;
    }
}

ptrdiff_t ListView::RowAt(Point local) const {
    if (!groups_.empty()) {
        const float row_h = std::max(theme_row_height_, 1.0f);
        const float y = local.y + scroll_offset_;
        if (local.x < 0.0f || local.x > absolute_.w || y < 0.0f) return -1;
        float cursor = 0.0f;
        for (size_t g = 0; g < groups_.size(); ++g) {
            cursor += kGroupHeight;
            if (!groups_[g].expanded) continue;
            const float extent = static_cast<float>(groups_[g].item_count) * row_h;
            if (y >= cursor && y < cursor + extent) {
                return static_cast<ptrdiff_t>(group_offsets_[g] +
                                              static_cast<size_t>((y - cursor) / row_h));
            }
            cursor += extent;
        }
        return -1;
    }
    const float row_h = std::max(theme_row_height_, 1.0f);
    const float y = local.y + scroll_offset_;
    if (local.x < 0.0f || local.x > absolute_.w || y < 0.0f) return -1;
    if (mut_kind_ != MutKind::None && mut_index_ >= 0) {
        const float before = static_cast<float>(mut_index_) * row_h;
        const float mut_h = row_h * mut_tween_.Value();
        if (y < before) {
            const ptrdiff_t row = static_cast<ptrdiff_t>(y / row_h);
            return row >= static_cast<ptrdiff_t>(item_count_) ? -1 : row;
        }
        if (y < before + mut_h) return mut_index_;
        const ptrdiff_t row =
            mut_index_ + 1 + static_cast<ptrdiff_t>((y - before - mut_h) / row_h);
        return row >= static_cast<ptrdiff_t>(item_count_) ? -1 : row;
    }
    const ptrdiff_t row = static_cast<ptrdiff_t>(y / row_h);
    if (row >= static_cast<ptrdiff_t>(item_count_)) return -1;
    return row;
}

ptrdiff_t ListView::GroupAt(Point local) const {
    if (groups_.empty() || local.x < 0.0f || local.x > absolute_.w) return -1;
    const float row_h = std::max(theme_row_height_, 1.0f);
    if (local.y >= 0.0f && local.y < kGroupHeight) {
        float sticky_cursor = 0.0f;
        ptrdiff_t sticky = -1;
        float next_header = ContentHeight();
        for (size_t g = 0; g < groups_.size(); ++g) {
            if (sticky_cursor <= scroll_offset_) sticky = static_cast<ptrdiff_t>(g);
            else { next_header = sticky_cursor; break; }
            sticky_cursor += kGroupHeight;
            if (groups_[g].expanded) {
                sticky_cursor += static_cast<float>(groups_[g].item_count) * row_h;
            }
        }
        if (sticky >= 0) {
            const float sticky_y = std::min(0.0f, next_header - scroll_offset_ - kGroupHeight);
            if (local.y >= sticky_y && local.y < sticky_y + kGroupHeight) return sticky;
        }
    }
    const float y = local.y + scroll_offset_;
    float cursor = 0.0f;
    for (size_t g = 0; g < groups_.size(); ++g) {
        if (y >= cursor && y < cursor + kGroupHeight) return static_cast<ptrdiff_t>(g);
        cursor += kGroupHeight;
        if (groups_[g].expanded) cursor += static_cast<float>(groups_[g].item_count) * row_h;
    }
    return -1;
}

ptrdiff_t ListView::GroupForItem(ptrdiff_t item) const {
    if (item < 0) return -1;
    for (size_t g = 0; g < groups_.size(); ++g) {
        const size_t first = group_offsets_[g];
        if (static_cast<size_t>(item) >= first &&
            static_cast<size_t>(item) < first + groups_[g].item_count) return static_cast<ptrdiff_t>(g);
    }
    return -1;
}

float ListView::ItemTop(size_t index) const {
    const float row_h = std::max(theme_row_height_, 1.0f);
    if (groups_.empty()) {
        float top = static_cast<float>(index) * row_h;
        if (mut_kind_ != MutKind::None && mut_index_ >= 0 &&
            static_cast<ptrdiff_t>(index) > mut_index_) {
            top -= row_h * (1.0f - mut_tween_.Value());
        }
        return top;
    }
    float cursor = 0.0f;
    for (size_t g = 0; g < groups_.size(); ++g) {
        cursor += kGroupHeight;
        const size_t first = group_offsets_[g];
        if (index >= first && index < first + groups_[g].item_count) {
            return groups_[g].expanded ? cursor + static_cast<float>(index - first) * row_h : -1.0f;
        }
        if (groups_[g].expanded) cursor += static_cast<float>(groups_[g].item_count) * row_h;
    }
    return -1.0f;
}

void ListView::ToggleGroup(size_t group) {
    if (group >= groups_.size()) return;
    GroupExpanded(groups_[group].id, !groups_[group].expanded);
}

void ListView::MoveGroupedFocus(int direction) {
    if (groups_.empty()) return;
    if (focus_group_ >= 0) {
        const ptrdiff_t g = focus_group_;
        if (direction > 0 && groups_[static_cast<size_t>(g)].expanded &&
            groups_[static_cast<size_t>(g)].item_count > 0) {
            focus_group_ = -1;
            SelectedIndex(static_cast<ptrdiff_t>(group_offsets_[static_cast<size_t>(g)]));
            return;
        }
        const ptrdiff_t next = g + direction;
        if (next >= 0 && next < static_cast<ptrdiff_t>(groups_.size())) {
            focus_group_ = next;
            Invalidate();
        }
        return;
    }
    const ptrdiff_t group = GroupForItem(selected_);
    if (group < 0) { focus_group_ = direction > 0 ? 0 : static_cast<ptrdiff_t>(groups_.size()) - 1; Invalidate(); return; }
    const size_t first = group_offsets_[static_cast<size_t>(group)];
    const size_t last = first + groups_[static_cast<size_t>(group)].item_count - 1;
    if ((direction < 0 && selected_ == static_cast<ptrdiff_t>(first)) ||
        (direction > 0 && selected_ == static_cast<ptrdiff_t>(last))) {
        const ptrdiff_t next_group = direction < 0 ? group : group + 1;
        if (next_group >= 0 && next_group < static_cast<ptrdiff_t>(groups_.size())) {
            focus_group_ = next_group;
            Invalidate();
        }
        return;
    }
    SelectedIndex(selected_ + direction);
}

void ListView::BeginPress(Point local, ptrdiff_t row) {
    press_local_ = local;
    press_row_ = row;
    press_armed_ = true;
    swipe_dragging_ = false;
    reorder_dragging_ = false;
    pan_vertical_ = false;
    drop_row_ = row;
}

void ListView::ResetPress() {
    press_armed_ = false;
    press_row_ = -1;
    swipe_dragging_ = false;
    reorder_dragging_ = false;
    pan_vertical_ = false;
    drop_row_ = -1;
}

void ListView::ApplySwipeX(float x) {
    const float trail =
        (swipe_trailing_.invoke || !swipe_trailing_.label.empty())
            ? SwipeActionWidth(swipe_trailing_)
            : 0.0f;
    const float lead = (swipe_leading_.invoke || !swipe_leading_.label.empty())
                           ? SwipeActionWidth(swipe_leading_)
                           : 0.0f;
    swipe_x_ = Clamp(x, -trail, lead);
    Invalidate();
}

void ListView::EndSwipe() {
    const ptrdiff_t row = swipe_row_;
    const float x = swipe_x_;
    swipe_dragging_ = false;
    press_armed_ = false;
    if (row >= 0 && std::fabs(x) >= kSwipeCommit) {
        const ListSwipeAction& action = x > 0.0f ? swipe_leading_ : swipe_trailing_;
        if (action.invoke) action.invoke(static_cast<size_t>(row));
        swipe_x_ = 0.0f;
        swipe_row_ = -1;
        Invalidate();
        return;
    }
    Animate();
}

void ListView::EndReorder() {
    const ptrdiff_t from = press_row_;
    ptrdiff_t to = drop_row_;
    reorder_dragging_ = false;
    press_armed_ = false;
    if (from >= 0 && to >= 0 && from != to) {
        if (!groups_.empty() && GroupForItem(from) != GroupForItem(to)) to = from;
        if (from != to) MoveItem(static_cast<size_t>(from), static_cast<size_t>(to));
    }
    drop_row_ = -1;
    Invalidate();
}

void ListView::OnMouseDown(Point local, uint32_t buttons) {
    if (!(buttons & kLeftButton)) return;
    Focus();
    if (BeginScrollDrag(local)) return;
    const ptrdiff_t group = GroupAt(local);
    if (group >= 0) {
        focus_group_ = group;
        ToggleGroup(static_cast<size_t>(group));
        Invalidate();
        return;
    }
    const ptrdiff_t row = RowAt(local);
    if (row < 0) return;
    const bool shift = ShiftHeld(buttons);
    const bool ctrl = CtrlHeld(buttons);
    // 修饰键点击只改选区，不武装拖排/滑动，避免手微动把多选抢走。
    if (!shift && !ctrl) BeginPress(local, row);
    if (!multi_select_) {
        focus_group_ = -1;
        SelectedIndex(row);
        return;
    }
    if (shift) {
        SelectRangeTo(row);       // Shift：从锚点重画范围
    } else if (ctrl) {
        ToggleSelected(row);      // Ctrl：增删单行
    } else {
        SelectedIndex(row);    // 普通点击：替换整个选区
    }
}

void ListView::OnMouseDoubleClick(Point local) {
    if (RowAt(local) >= 0) activate_.Emit(selected_ >= 0 ? static_cast<size_t>(selected_) : 0);
}

void ListView::OnMouseMove(Point local, uint32_t buttons) {
    if (dragging_) {
        const ScrollThumb thumb = Thumb(1.0f);
        const float thumb_h = thumb.visible ? thumb.rect.h : 20.0f;
        const float track = std::max(1.0f, absolute_.h - thumb_h);
        const float t = Clamp((local.y - drag_grab_) / track, 0.0f, 1.0f);
        scroll_offset_ = target_offset_ = t * MaxScroll();
        Invalidate();
        return;
    }
    if (press_armed_ && (buttons & kLeftButton)) {
        const float dx = local.x - press_local_.x;
        const float dy = local.y - press_local_.y;
        const bool gesture_ok = !ShiftHeld(buttons) && !CtrlHeld(buttons);
        if (!swipe_dragging_ && !reorder_dragging_ && gesture_ok) {
            if (std::max(std::fabs(dx), std::fabs(dy)) >= kDragSlop) {
                if (HasSwipe() && std::fabs(dx) > std::fabs(dy)) {
                    swipe_dragging_ = true;
                    swipe_row_ = press_row_;
                } else if (can_reorder_ && press_row_ >= 0) {
                    reorder_dragging_ = true;
                    drop_row_ = press_row_;
                }
            }
        }
        if (swipe_dragging_) {
            ApplySwipeX(dx);
            return;
        }
        if (reorder_dragging_) {
            ptrdiff_t row = RowAt(local);
            if (row < 0 && item_count_ > 0 && local.y + scroll_offset_ >= ContentHeight()) {
                row = static_cast<ptrdiff_t>(item_count_) - 1;
            }
            if (row >= 0 && !groups_.empty() &&
                GroupForItem(press_row_) != GroupForItem(row)) {
                row = press_row_;
            }
            if (row != drop_row_) {
                drop_row_ = row;
                Invalidate();
            }
            return;
        }
    }
    const ptrdiff_t row = RowAt(local);
    const ptrdiff_t group = GroupAt(local);
    if (row != hover_row_ || group != hover_group_) {
        hover_row_ = row;
        hover_group_ = group;
        Animate();
        Invalidate();
    }
}

void ListView::OnMouseUp(Point, uint32_t) {
    dragging_ = false;
    if (swipe_dragging_) EndSwipe();
    else if (reorder_dragging_) EndReorder();
    else ResetPress();
    Animate();
}

void ListView::OnMouseLeave() {
    Control::OnMouseLeave();
    hover_row_ = -1;
    hover_group_ = -1;
    Animate();
    Invalidate();
}

bool ListView::OnWheel(float delta) {
    if (MaxScroll() <= 0.0f) return false;
    const float row_h = std::max(theme_row_height_, 1.0f);
    target_offset_ = Clamp(target_offset_ - delta * row_h * 2.5f, 0.0f, MaxScroll());
    Animate();
    return true;
}

bool ListView::CanPan() const noexcept {
    return MaxScroll() > 0.5f || HasSwipe();
}

void ListView::PanBy(float dx, float dy) {
    if (!pan_vertical_ && !swipe_dragging_ && HasSwipe() && press_row_ >= 0 &&
        std::fabs(dx) > std::fabs(dy) && std::fabs(dx) > 1.0f) {
        swipe_dragging_ = true;
        swipe_row_ = press_row_;
    }
    if (swipe_dragging_) {
        ApplySwipeX(swipe_x_ + dx);
        return;
    }
    pan_vertical_ = true;
    scroll_offset_ = Clamp(scroll_offset_ - dy, 0.0f, MaxScroll());
    target_offset_ = scroll_offset_;
    Invalidate();
}

void ListView::PanFling(float, float vy) {
    if (swipe_dragging_) {
        EndSwipe();
        return;
    }
    target_offset_ = Clamp(scroll_offset_ - vy * 0.22f, 0.0f, MaxScroll());
    Animate();
}

bool ListView::PrefersDragOverPan() const noexcept {
    return dragging_ || swipe_dragging_ || reorder_dragging_;
}

CursorShape ListView::CursorAt(Point) const {
    return reorder_dragging_ ? CursorShape::SizeNS : CursorShape::Arrow;
}

void ListView::Draw(Painter& painter, const Theme& theme) {
    theme_row_height_ = theme.list_row_height;
    const float row_h = std::max(theme_row_height_, 1.0f);
    ClampScroll();

    if (item_count_ == 0) {
        painter.FillRoundedRect(absolute_, theme.radius_control, theme.fill_input);
        if (focused_ && enabled_) PaintFocusRing(painter, theme, absolute_, theme.radius_control);
        return;
    }

    const auto paint_action = [&](const Rect& slot, const ListSwipeAction& action, Color fill,
                                  bool trailing) {
        if (slot.w < 4.0f) return;
        painter.FillRect(slot, fill);
        if (trailing) {
            float cursor = slot.Right() - kSwipePad;
            if (!action.label.empty()) {
                const float tw = MeasureText(action.label, TextRole::CaptionStrong).w;
                cursor -= tw;
                painter.DrawText(action.label, {cursor, slot.y, tw, slot.h},
                                 TextRole::CaptionStrong, theme.text);
                if (!action.glyph.empty()) cursor -= 6.0f;
            }
            if (!action.glyph.empty()) {
                cursor -= 16.0f;
                painter.DrawIcon(action.glyph, {cursor, slot.y, 16.0f, slot.h}, 16.0f, theme.text);
            }
        } else {
            float x = slot.x + kSwipePad;
            if (!action.glyph.empty()) {
                painter.DrawIcon(action.glyph, {x, slot.y, 16.0f, slot.h}, 16.0f, theme.text);
                x += 22.0f;
            }
            if (!action.label.empty()) {
                const float tw = std::max(0.0f, slot.Right() - kSwipePad - x);
                painter.DrawText(action.label, {x, slot.y, tw, slot.h}, TextRole::CaptionStrong,
                                 theme.text);
            }
        }
    };

    const auto draw_item = [&](ptrdiff_t row, float y, size_t vis, float extent) {
        const float enter = ItemEnter(vis);
        if (enter <= 0.004f) return;
        y += kEnterLift * (1.0f - enter);
        float alpha = enter;
        if (mut_kind_ != MutKind::None && mut_index_ == row) alpha *= mut_tween_.Value();
        if (alpha <= 0.004f) return;
        float x_off = 0.0f;
        if (swipe_row_ == row) x_off = swipe_x_;
        const Rect row_rect{absolute_.x, y, absolute_.w, std::max(extent, 0.0f)};
        if (x_off < -0.5f && (swipe_trailing_.invoke || !swipe_trailing_.label.empty())) {
            paint_action({row_rect.Right() + x_off, y, -x_off, row_rect.h}, swipe_trailing_,
                         Fade(theme.danger, 0.28f * alpha), true);
        } else if (x_off > 0.5f && (swipe_leading_.invoke || !swipe_leading_.label.empty())) {
            paint_action({row_rect.x, y, x_off, row_rect.h}, swipe_leading_,
                         Fade(theme.fill_hover, alpha), false);
        }
        const Rect shifted{row_rect.x + x_off, y, row_rect.w, row_rect.h};
        const Rect row_slot = shifted.Inset(4.0f, 0.0f);
        if (IsSelected(static_cast<size_t>(row))) {
            painter.FillRoundedRect(row_slot, theme.radius_control, Fade(theme.fill_selected, alpha));
            painter.FillRoundedRect({row_slot.x, row_slot.y, 3.0f, row_slot.h}, 1.5f,
                                    Fade(theme.accent, alpha));
        } else if (row == hover_row_ && enabled_ && !reorder_dragging_) {
            painter.FillRoundedRect(row_slot, theme.radius_control, Fade(theme.fill_hover, alpha));
        }
        if (reorder_dragging_ && row == press_row_) {
            painter.StrokeRoundedRect(row_slot, theme.radius_control, Fade(theme.accent, 0.55f), 1.0f);
        }
        draw_text_.clear();
        draw_glyph_.clear();
        const size_t data = DataIndex(static_cast<size_t>(row));
        if (item_text_) item_text_(data, draw_text_);
        if (item_glyph_) item_glyph_(data, draw_glyph_);
        const std::wstring& glyph = draw_glyph_;
        const std::wstring& text = draw_text_;
        float text_x = shifted.x + 12.0f;
        if (!glyph.empty()) {
            painter.DrawIcon(glyph, {text_x, shifted.y, 16.0f, shifted.h}, 16.0f,
                             Fade(theme.text_secondary, alpha));
            text_x += 26.0f;
        }
        if (!text.empty()) {
            painter.DrawText(text, {text_x, shifted.y, shifted.Right() - 12.0f - text_x, shifted.h},
                             TextRole::Body, Fade(theme.text, alpha));
        }
    };

    const auto paint_drop = [&] {
        if (!reorder_dragging_ || drop_row_ < 0) return;
        const float y = absolute_.y + ItemTop(static_cast<size_t>(drop_row_)) - scroll_offset_;
        painter.FillRect({absolute_.x + 8.0f, y - 1.0f, absolute_.w - 16.0f, 2.0f}, theme.accent);
    };

    const auto paint_ungrouped = [&] {
        painter.FillRoundedRect(absolute_, theme.radius_control, theme.fill_input);
        if (mut_kind_ != MutKind::None && mut_index_ >= 0) {
            const float view0 = scroll_offset_;
            const float before = static_cast<float>(mut_index_) * row_h;
            const float mut_h = row_h * mut_tween_.Value();
            ptrdiff_t first = 0;
            if (view0 < before) first = static_cast<ptrdiff_t>(view0 / row_h);
            else if (view0 < before + mut_h) first = mut_index_;
            else {
                first = mut_index_ + 1 +
                        static_cast<ptrdiff_t>((view0 - before - mut_h) / row_h);
            }
            const ptrdiff_t visible = static_cast<ptrdiff_t>(absolute_.h / row_h) + 3;
            for (ptrdiff_t row = std::max(ptrdiff_t{0}, first);
                 row < first + visible && row < static_cast<ptrdiff_t>(item_count_); ++row) {
                const float y = absolute_.y + ItemTop(static_cast<size_t>(row)) - scroll_offset_;
                const size_t vis = static_cast<size_t>(std::max(ptrdiff_t{0}, row - first));
                draw_item(row, y, vis, RowExtent(static_cast<size_t>(row)));
            }
        } else {
            const ptrdiff_t first = static_cast<ptrdiff_t>(scroll_offset_ / row_h);
            const ptrdiff_t visible = static_cast<ptrdiff_t>(absolute_.h / row_h) + 2;
            for (ptrdiff_t row = first;
                 row < first + visible && row < static_cast<ptrdiff_t>(item_count_); ++row) {
                const size_t vis = static_cast<size_t>(std::max(ptrdiff_t{0}, row - first));
                draw_item(row, absolute_.y + static_cast<float>(row) * row_h - scroll_offset_, vis,
                          row_h);
            }
        }
        paint_drop();
    };

    const auto paint_grouped = [&] {
        painter.FillRoundedRect(absolute_, theme.radius_control, theme.fill_input);
        float cursor = absolute_.y - scroll_offset_;
        ptrdiff_t sticky = -1;
        float sticky_next = absolute_.Bottom();
        for (size_t g = 0; g < groups_.size(); ++g) {
            const float header_y = cursor;
            if (header_y <= absolute_.y) sticky = static_cast<ptrdiff_t>(g);
            else if (sticky >= 0 && sticky_next == absolute_.Bottom()) sticky_next = header_y;
            if (header_y + kGroupHeight >= absolute_.y && header_y <= absolute_.Bottom()) {
                const Rect header{absolute_.x, header_y, absolute_.w, kGroupHeight};
                if (hover_group_ == static_cast<ptrdiff_t>(g))
                    painter.FillRect(header, theme.fill_hover);
                painter.DrawChevron({header.x + 16.0f, header.y + header.h * 0.5f}, 10.0f,
                                    groups_[g].expanded ? 0.0f : -90.0f, theme.text_secondary, 1.4f);
                painter.DrawText(groups_[g].title,
                                 {header.x + 30.0f, header.y, header.w - 42.0f, header.h},
                                 TextRole::CaptionStrong, theme.text_secondary);
                if (focused_ && focus_group_ == static_cast<ptrdiff_t>(g))
                    PaintFocusRing(painter, theme, header.Inset(4.0f, 2.0f), 6.0f);
            }
            cursor += kGroupHeight;
            if (groups_[g].expanded) {
                const float group_start = cursor;
                const size_t count = groups_[g].item_count;
                const ptrdiff_t first = std::max(
                    ptrdiff_t{0}, static_cast<ptrdiff_t>((absolute_.y - group_start) / row_h));
                const ptrdiff_t visible = static_cast<ptrdiff_t>(absolute_.h / row_h) + 2;
                const ptrdiff_t end = std::min(static_cast<ptrdiff_t>(count), first + visible);
                for (ptrdiff_t i = first; i < end; ++i) {
                    const float item_y = group_start + static_cast<float>(i) * row_h;
                    if (item_y + row_h >= absolute_.y && item_y <= absolute_.Bottom()) {
                        const size_t vis =
                            static_cast<size_t>(std::max(0.0f, (item_y - absolute_.y) / row_h));
                        draw_item(static_cast<ptrdiff_t>(group_offsets_[g]) + i, item_y, vis, row_h);
                    }
                }
                cursor = group_start + static_cast<float>(count) * row_h;
            }
        }
        if (sticky >= 0) {
            const float y = std::min(absolute_.y, sticky_next - kGroupHeight);
            const ListGroup& group = groups_[static_cast<size_t>(sticky)];
            const Rect header{absolute_.x, y, absolute_.w, kGroupHeight};
            painter.FillRect(header, theme.fill_input_hover);
            painter.DrawChevron({header.x + 16.0f, header.y + header.h * 0.5f}, 10.0f,
                                group.expanded ? 0.0f : -90.0f, theme.text_secondary, 1.4f);
            painter.DrawText(group.title, {header.x + 30.0f, header.y, header.w - 42.0f, header.h},
                             TextRole::CaptionStrong, theme.text_secondary);
        }
        paint_drop();
    };

    const auto replay_list = [](ID2D1DeviceContext2* dc, ID2D1CommandList* list) {
        D2D1_MATRIX_3X2_F saved{};
        dc->GetTransform(&saved);
        dc->SetTransform(D2D1::Matrix3x2F::Identity());
        dc->DrawImage(list);
        dc->SetTransform(saved);
    };

    ID2D1DeviceContext2* dc = painter.DeviceContext();
    if (!LivePaint() && painter.CanRecordCommandList() && dc) {
        const ptrdiff_t first = static_cast<ptrdiff_t>(scroll_offset_ / row_h);
        const ptrdiff_t visible = static_cast<ptrdiff_t>(absolute_.h / row_h) + 2;
        ptrdiff_t last = first + visible;
        if (last > static_cast<ptrdiff_t>(item_count_)) last = static_cast<ptrdiff_t>(item_count_);
        uint64_t sel = static_cast<uint64_t>(selected_ + 1);
        if (multi_select_) {
            sel ^= selected_set_.size() * 0x9e3779b97f4a7c15ULL;
            for (ptrdiff_t row : selected_set_) {
                sel ^= static_cast<uint64_t>(row + 1) * 0x100000001b3ULL;
                sel = (sel << 7) | (sel >> 57);
            }
        }
        if (!draw_cache_) draw_cache_ = std::make_unique<DrawCache>();
        DrawCache& cache = *draw_cache_;
        const bool hit = cache.list && cache.device == dc && cache.scroll == scroll_offset_ &&
                         cache.w == absolute_.w && cache.h == absolute_.h && cache.first == first &&
                         cache.last == last && cache.selected == selected_ &&
                         cache.hover == hover_row_ && cache.count == item_count_ &&
                         cache.sel == sel && cache.enabled == enabled_;
        if (!hit) {
            cache.list.reset();
            ComPtr<ID2D1CommandList> cmd;
            if (SUCCEEDED(dc->CreateCommandList(&cmd)) && cmd) {
                ComPtr<ID2D1Image> previous;
                dc->GetTarget(&previous);
                D2D1_MATRIX_3X2_F saved{};
                dc->GetTransform(&saved);
                dc->SetTarget(cmd.get());
                dc->SetTransform(saved);
                painter.PushClip(absolute_);
                paint_ungrouped();
                painter.PopClip();
                cmd->Close();
                dc->SetTarget(previous.get());
                dc->SetTransform(saved);
                cache.list = std::move(cmd);
                cache.device = dc;
                cache.scroll = scroll_offset_;
                cache.w = absolute_.w;
                cache.h = absolute_.h;
                cache.first = first;
                cache.last = last;
                cache.selected = selected_;
                cache.hover = hover_row_;
                cache.count = item_count_;
                cache.sel = sel;
                cache.enabled = enabled_;
            }
        }
        painter.PushClip(absolute_);
        if (cache.list) replay_list(dc, cache.list.get());
        else paint_ungrouped();
        const Color thumb =
            (hovered_ || dragging_) ? theme.scrollbar_thumb_hover : theme.scrollbar_thumb;
        painter.DrawScrollThumb(Thumb(expand_progress_), thumb);
        painter.PopClip();
        if (focused_ && enabled_) PaintFocusRing(painter, theme, absolute_, theme.radius_control);
        return;
    }

    painter.PushClip(absolute_);
    if (groups_.empty()) paint_ungrouped();
    else paint_grouped();
    const Color thumb = (hovered_ || dragging_) ? theme.scrollbar_thumb_hover : theme.scrollbar_thumb;
    painter.DrawScrollThumb(Thumb(expand_progress_), thumb);
    painter.PopClip();
    if (focused_ && enabled_) {
        PaintFocusRing(painter, theme, absolute_, theme.radius_control);
    }
}

} // namespace lumen
