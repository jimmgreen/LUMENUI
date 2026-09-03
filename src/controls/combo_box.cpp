#include "lumen/ComboBox.h"
#include "lumen/Chip.h"
#include "lumen/Icons.h"
#include "lumen/Painter.h"
#include "../core/window_impl.h"
#include <windows.h>
#include <algorithm>
#include <cwctype>

namespace lumen {
namespace {
constexpr float kPadX = 16.0f;
constexpr float kChevronArea = 36.0f;
constexpr float kFieldWidth = 240.0f;
constexpr float kPopupPad = 4.0f;
constexpr float kRowHeight = 34.0f;
constexpr float kHeaderHeight = 28.0f;
constexpr float kBarHit = 10.0f;
constexpr float kChipPadH = 8.0f;
constexpr float kChipPadV = 6.0f;
constexpr float kChipGap = 6.0f;
constexpr float kChipH = 28.0f;
constexpr float kInf = 1.0e5f;
constexpr float kJumpTimeout = 1.0f;

Color Mix(Color a, Color b, float t) {
    return {Lerp(a.r, b.r, t), Lerp(a.g, b.g, t), Lerp(a.b, b.b, t), Lerp(a.a, b.a, t)};
}

bool AxisFinite(float v) noexcept { return v >= 0.0f && v < 1.0e4f; }

bool ContainsFolded(std::wstring_view text, std::wstring_view query) {
    if (query.empty()) return true;
    if (query.size() > text.size()) return false;
    for (size_t start = 0; start + query.size() <= text.size(); ++start) {
        bool match = true;
        for (size_t i = 0; i < query.size(); ++i) {
            if (towlower(text[start + i]) != towlower(query[i])) { match = false; break; }
        }
        if (match) return true;
    }
    return false;
}

bool StartsFolded(std::wstring_view text, std::wstring_view query) {
    if (query.empty()) return true;
    if (query.size() > text.size()) return false;
    for (size_t i = 0; i < query.size(); ++i) {
        if (towlower(text[i]) != towlower(query[i])) return false;
    }
    return true;
}

bool JumpIsRepeat(const std::wstring& jump) noexcept {
    if (jump.size() < 2) return false;
    const wint_t first = towlower(jump[0]);
    for (size_t i = 1; i < jump.size(); ++i) {
        if (towlower(jump[i]) != first) return false;
    }
    return true;
}
} // namespace

class ComboBox::DropdownPopup : public Control {
public:
    explicit DropdownPopup(ComboBox* owner) : owner_(owner) {}

    void Rebuild(bool filter) {
        rows_.clear();
        matches_.clear();
        const std::wstring_view query = filter ? std::wstring_view(owner_->edit_text_)
                                               : std::wstring_view{};
        const auto& items = owner_->items_;
        const auto& groups = owner_->groups_;
        auto push_item = [&](size_t i) {
            if (!ContainsFolded(items[i], query)) return false;
            rows_.push_back({false, i});
            matches_.push_back(i);
            return true;
        };
        if (groups.empty()) {
            rows_.reserve(items.size());
            matches_.reserve(items.size());
            for (size_t i = 0; i < items.size(); ++i) push_item(i);
        } else {
            size_t offset = 0;
            for (size_t g = 0; g < groups.size(); ++g) {
                const size_t count = groups[g].item_count;
                rows_.push_back({true, g});
                bool any = false;
                for (size_t j = 0; j < count && offset + j < items.size(); ++j) {
                    if (push_item(offset + j)) any = true;
                }
                if (!any) rows_.pop_back();
                offset += count;
            }
            while (offset < items.size()) push_item(offset++);
        }
        uniform_ = true;
        content_h_ = 0.0f;
        for (const DropRow& row : rows_) {
            if (row.header) uniform_ = false;
            content_h_ += row.header ? kHeaderHeight : kRowHeight;
        }
        focus_ = -1;
        for (size_t i = 0; i < rows_.size(); ++i) {
            if (!rows_[i].header &&
                static_cast<ptrdiff_t>(rows_[i].index) == owner_->selected_) {
                focus_ = static_cast<ptrdiff_t>(i);
                break;
            }
        }
        if (focus_ < 0) focus_ = FirstItemRow();
        scroll_y_ = 0.0f;
        ScrollFocusIntoView();
        Invalidate();
    }

    size_t MatchCount() const noexcept { return matches_.size(); }

    void FocusDataIndex(size_t data) {
        for (size_t i = 0; i < rows_.size(); ++i) {
            if (!rows_[i].header && rows_[i].index == data) {
                focus_ = static_cast<ptrdiff_t>(i);
                ScrollFocusIntoView();
                Invalidate();
                return;
            }
        }
    }

protected:
    Size Measure(Size available, const Theme&) override {
        const float cap = static_cast<float>(owner_->max_dropdown_rows_) * kRowHeight;
        const float view = std::min(content_h_, cap);
        return {available.w, kPopupPad * 2.0f + std::max(kRowHeight, view)};
    }
    bool Focusable() const noexcept override { return true; }
    void Arrange(const Rect& absolute) override {
        absolute_ = absolute;
        ScrollFocusIntoView();
    }
    CursorShape CursorAt(Point local) const override {
        if (MaxScroll() > 0.5f && local.x >= absolute_.w - kBarHit) return CursorShape::Arrow;
        return CursorShape::Hand;
    }
    bool CapturesOverlay(Point p) const override {
        if (dragging_) return true;
        if (MaxScroll() <= 0.5f) return false;
        return p.x >= absolute_.Right() - kBarHit;
    }

    void Draw(Painter& painter, const Theme& theme) override {
        painter.FillRoundedRect(absolute_, theme.radius_flyout, theme.bg);
        painter.FillRoundedRect(absolute_, theme.radius_flyout, theme.fill_input);
        painter.StrokeRoundedRect(absolute_, theme.radius_flyout, theme.stroke_card);
        painter.PushClip(absolute_.Inset(kPopupPad, kPopupPad));
        const size_t first = FirstVisible();
        float y = kPopupPad + RowTop(first) - scroll_y_;
        const float bottom = absolute_.h - kPopupPad;
        for (size_t row = first; row < rows_.size(); ++row) {
            const float h = RowH(row);
            if (y >= bottom) break;
            const Rect slot{absolute_.x + kPopupPad, absolute_.y + y,
                            absolute_.w - kPopupPad * 2.0f, h};
            const DropRow& drop = rows_[row];
            if (drop.header) {
                painter.DrawText(owner_->groups_[drop.index].title,
                                 {slot.x + 10.0f, slot.y, slot.w - 20.0f, slot.h},
                                 TextRole::CaptionStrong, theme.text_secondary);
            } else {
                const bool picked = owner_->ItemPicked(drop.index);
                if (static_cast<ptrdiff_t>(row) == focus_) {
                    painter.FillRoundedRect(slot.Inset(2.0f, 1.0f), 7.0f, theme.fill_hover);
                }
                if (picked) {
                    painter.FillRoundedRect(slot.Inset(2.0f, 1.0f), 7.0f, theme.fill_selected);
                }
                painter.DrawText(owner_->items_[drop.index],
                                 {slot.x + 10.0f, slot.y, slot.w - 38.0f, slot.h}, TextRole::Body,
                                 theme.text);
                if (picked) {
                    painter.DrawIcon(icon::kCheckMark,
                                     {slot.Right() - 28.0f, slot.y, 20.0f, slot.h}, 13.0f,
                                     theme.text);
                }
                if (static_cast<ptrdiff_t>(row) == focus_ && HasFocus()) {
                    PaintFocusRing(painter, theme, slot.Inset(2.0f, 1.0f), 7.0f);
                }
            }
            y += h;
        }
        painter.PopClip();
        painter.DrawScrollThumb(Thumb(), theme.scrollbar_thumb_hover);
    }

    bool OnKey(uint32_t vk) override {
        if (vk == VK_ESCAPE) { WindowImpl::CloseTransient(window_); return true; }
        if (vk == VK_RETURN || vk == VK_SPACE) { Commit(); return true; }
        if (vk == VK_UP || vk == VK_DOWN || vk == VK_HOME || vk == VK_END ||
            vk == VK_PRIOR || vk == VK_NEXT) {
            if (matches_.empty()) return true;
            const ptrdiff_t page = std::max(ptrdiff_t{1},
                static_cast<ptrdiff_t>((absolute_.h - kPopupPad * 2.0f) / kRowHeight) - 1);
            if (vk == VK_HOME) focus_ = FirstItemRow();
            else if (vk == VK_END) focus_ = LastItemRow();
            else if (vk == VK_PRIOR) {
                MoveFocus(-page);
            } else if (vk == VK_NEXT) {
                MoveFocus(page);
            } else {
                MoveFocus(vk == VK_DOWN ? 1 : -1);
            }
            ScrollFocusIntoView();
            Invalidate();
            return true;
        }
        return false;
    }

    bool OnChar(wchar_t ch) override {
        if (owner_->editable_) {
            if (ch == 0x08) { if (!owner_->edit_text_.empty()) owner_->edit_text_.pop_back(); }
            else if (ch >= 0x20) owner_->edit_text_.push_back(ch);
            else return false;
            owner_->selected_ = -1;
            Rebuild(true);
            owner_->Invalidate();
            return true;
        }
        if (ch < 0x20) return false;
        owner_->TypeJump(ch);
        return true;
    }

    bool OnWheel(float delta) override {
        if (MaxScroll() <= 0.0f) return false;
        scroll_y_ = Clamp(scroll_y_ - delta * kRowHeight * 2.5f, 0.0f, MaxScroll());
        Invalidate();
        return true;
    }

    void OnMouseMove(Point local, uint32_t) override {
        if (dragging_) {
            const ScrollThumb thumb = Thumb();
            const float thumb_h = thumb.visible ? thumb.rect.h : 20.0f;
            const float track = std::max(1.0f, absolute_.h - thumb_h);
            const float t = Clamp((local.y - drag_grab_) / track, 0.0f, 1.0f);
            scroll_y_ = t * MaxScroll();
            Invalidate();
            return;
        }
        const ptrdiff_t row = RowAt(local);
        if (row != focus_) { focus_ = row; Invalidate(); }
    }

    void OnMouseDown(Point local, uint32_t buttons) override {
        if (!(buttons & MK_LBUTTON)) return;
        Focus();
        if (BeginScrollDrag(local)) return;
        focus_ = RowAt(local);
        if (focus_ >= 0 && static_cast<size_t>(focus_) < rows_.size() &&
            rows_[static_cast<size_t>(focus_)].header) {
            Invalidate();
            return;
        }
        Commit();
    }

    void OnMouseUp(Point, uint32_t) override { dragging_ = false; }

private:
    struct DropRow {
        bool header = false;
        size_t index = 0;
    };

    float RowH(size_t row) const noexcept {
        return rows_[row].header ? kHeaderHeight : kRowHeight;
    }
    float RowTop(size_t row) const {
        if (uniform_) return static_cast<float>(row) * kRowHeight;
        float y = 0.0f;
        for (size_t i = 0; i < row && i < rows_.size(); ++i) y += RowH(i);
        return y;
    }
    size_t FirstVisible() const {
        if (rows_.empty()) return 0;
        if (uniform_) {
            return std::min(rows_.size() - 1,
                            static_cast<size_t>(scroll_y_ / kRowHeight));
        }
        float acc = 0.0f;
        for (size_t i = 0; i < rows_.size(); ++i) {
            const float h = RowH(i);
            if (acc + h > scroll_y_) return i;
            acc += h;
        }
        return rows_.size() - 1;
    }
    ptrdiff_t FirstItemRow() const {
        for (size_t i = 0; i < rows_.size(); ++i) {
            if (!rows_[i].header) return static_cast<ptrdiff_t>(i);
        }
        return -1;
    }
    ptrdiff_t LastItemRow() const {
        for (size_t i = rows_.size(); i-- > 0;) {
            if (!rows_[i].header) return static_cast<ptrdiff_t>(i);
        }
        return -1;
    }
    void MoveFocus(ptrdiff_t delta) {
        if (rows_.empty() || delta == 0) return;
        const int dir = delta > 0 ? 1 : -1;
        ptrdiff_t steps = delta > 0 ? delta : -delta;
        ptrdiff_t i = focus_;
        if (i < 0) {
            focus_ = dir > 0 ? FirstItemRow() : LastItemRow();
            return;
        }
        while (steps-- > 0) {
            ptrdiff_t next = i + dir;
            while (next >= 0 && next < static_cast<ptrdiff_t>(rows_.size()) &&
                   rows_[static_cast<size_t>(next)].header) {
                next += dir;
            }
            if (next < 0 || next >= static_cast<ptrdiff_t>(rows_.size())) break;
            i = next;
        }
        focus_ = i;
    }
    float MaxScroll() const {
        return std::max(0.0f, kPopupPad * 2.0f + content_h_ - absolute_.h);
    }
    ScrollThumb Thumb() const {
        return MakeScrollThumb(absolute_, kPopupPad * 2.0f + content_h_,
                               scroll_y_, 1.0f, true);
    }
    bool BeginScrollDrag(Point local) {
        if (MaxScroll() <= 0.5f) return false;
        const Point world{absolute_.x + local.x, absolute_.y + local.y};
        if (world.x < absolute_.Right() - kBarHit) return false;
        const ScrollThumb thumb = Thumb();
        dragging_ = true;
        if (thumb.visible && thumb.rect.Contains(world)) {
            drag_grab_ = world.y - thumb.rect.y;
        } else {
            const float thumb_h = thumb.visible ? thumb.rect.h : 20.0f;
            const float track = std::max(1.0f, absolute_.h - thumb_h);
            const float t = Clamp((local.y - thumb_h * 0.5f) / track, 0.0f, 1.0f);
            scroll_y_ = t * MaxScroll();
            drag_grab_ = thumb_h * 0.5f;
            Invalidate();
        }
        return true;
    }
    ptrdiff_t RowAt(Point local) const {
        const float y = local.y - kPopupPad + scroll_y_;
        if (local.x < 0.0f || local.x > absolute_.w || y < 0.0f) return -1;
        if (uniform_) {
            const ptrdiff_t row = static_cast<ptrdiff_t>(y / kRowHeight);
            return row >= 0 && row < static_cast<ptrdiff_t>(rows_.size()) ? row : -1;
        }
        float acc = 0.0f;
        for (size_t i = 0; i < rows_.size(); ++i) {
            const float h = RowH(i);
            if (y < acc + h) return static_cast<ptrdiff_t>(i);
            acc += h;
        }
        return -1;
    }
    void ScrollFocusIntoView() {
        if (focus_ < 0) { scroll_y_ = 0.0f; return; }
        // Rebuild 在 LayoutFlyout 之前，absolute_ 仍是空；此时按 view=0 滚会把焦点顶出视口。
        const float view = absolute_.h - kPopupPad * 2.0f;
        if (view < kRowHeight) return;
        const float top = RowTop(static_cast<size_t>(focus_));
        const float h = RowH(static_cast<size_t>(focus_));
        if (top < scroll_y_) scroll_y_ = top;
        else if (top + h > scroll_y_ + view) scroll_y_ = top + h - view;
        scroll_y_ = Clamp(scroll_y_, 0.0f, MaxScroll());
    }
    void Commit() {
        if (focus_ < 0 || focus_ >= static_cast<ptrdiff_t>(rows_.size())) return;
        const DropRow& drop = rows_[static_cast<size_t>(focus_)];
        if (drop.header) return;
        if (owner_->multi_) {
            owner_->ToggleItem(drop.index);
            Invalidate();
            return;
        }
        const ptrdiff_t selected = static_cast<ptrdiff_t>(drop.index);
        const bool changed = owner_->selected_ != selected;
        owner_->selected_ = selected;
        if (owner_->editable_) owner_->edit_text_ = owner_->items_[drop.index];
        owner_->Invalidate();
        WindowImpl::CloseTransient(window_);
        if (changed) owner_->changed_.Emit(owner_->selected_, owner_->selected_);
    }

    ComboBox* owner_ = nullptr;
    std::vector<DropRow> rows_;
    std::vector<size_t> matches_;
    ptrdiff_t focus_ = -1;
    float scroll_y_ = 0.0f;
    float drag_grab_ = 0.0f;
    float content_h_ = 0.0f;
    bool dragging_ = false;
    bool uniform_ = true;
};

ComboBox::ComboBox() : popup_(std::make_unique<DropdownPopup>(this)) {
    Clip(true);
}
ComboBox::~ComboBox() {
    if (window_ && WindowImpl::TransientActive(window_, popup_.get())) WindowImpl::CloseTransient(window_);
}

void ComboBox::RelayoutParent() { Control::RelayoutParent(); }

ComboBox& ComboBox::Enabled(bool value) {
    Control::Enabled(value);
    for (size_t i = 0; i < ChildCount(); ++i) Child(i).Enabled(value);
    return *this;
}

ComboBox& ComboBox::AddItem(std::wstring_view text) {
    items_.emplace_back(text);
    RelayoutParent();
    return *this;
}

ComboBox& ComboBox::Items(std::vector<std::wstring> items) {
    items_ = std::move(items);
    FilterSelection();
    if (editable_ && selected_ >= 0) edit_text_ = items_[static_cast<size_t>(selected_)];
    SyncChips();
    RelayoutParent();
    return *this;
}

ComboBox& ComboBox::MaxDropDownRows(size_t value) {
    max_dropdown_rows_ = std::clamp(value, size_t{1}, size_t{32});
    return *this;
}

ComboBox& ComboBox::ClearItems() {
    items_.clear();
    groups_.clear();
    selected_ = -1;
    selected_set_.clear();
    edit_text_.clear();
    jump_.clear();
    SyncChips();
    RelayoutParent();
    return *this;
}

namespace {
void ReloadComboModel(ComboBox& box, ItemsModel* model) {
    std::vector<std::wstring> items;
    if (model) {
        ItemRow row;
        const size_t n = model->Count();
        items.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            model->Get(i, row);
            items.push_back(std::move(row.text));
        }
    }
    box.Items(std::move(items));
}
} // namespace

ComboBox& ComboBox::Bind(ItemsModel& model) {
    owned_model_.reset();
    model_ = &model;
    auto reload = [this] { ReloadComboModel(*this, model_); };
    model_reset_ = ScopedConnection(model.OnReset(reload));
    model_inserted_ = ScopedConnection(model.OnInserted([reload](size_t, size_t) { reload(); }));
    model_removed_ = ScopedConnection(model.OnRemoved([reload](size_t, size_t) { reload(); }));
    model_changed_ = ScopedConnection(model.OnChanged([reload](size_t, size_t) { reload(); }));
    model_detached_ = ScopedConnection(model.OnDetached([this] {
        model_ = nullptr;
        owned_model_.reset();
        ClearItems();
    }));
    reload();
    return *this;
}

ComboBox& ComboBox::Bind(std::shared_ptr<ItemsModel> model) {
    if (!model) return *this;
    ItemsModel& ref = *model;
    Bind(ref);
    owned_model_ = std::move(model);
    return *this;
}

ComboBox& ComboBox::Groups(std::vector<ComboGroup> groups) {
    groups_ = std::move(groups);
    if (dropdown_open_) popup_->Rebuild(false);
    Invalidate();
    return *this;
}

ComboBox& ComboBox::Editable(bool value) {
    if (multi_ || editable_ == value) return *this;
    editable_ = value;
    if (editable_) {
        if (selected_ >= 0) edit_text_ = items_[static_cast<size_t>(selected_)];
    } else {
        edit_text_.clear();
    }
    Invalidate();
    return *this;
}

ComboBox& ComboBox::MultiSelect(bool on) {
    if (multi_ == on) return *this;
    multi_ = on;
    if (multi_) {
        editable_ = false;
        edit_text_.clear();
        selected_set_.clear();
        if (selected_ >= 0) selected_set_.push_back(selected_);
    } else {
        if (!selected_set_.empty()) selected_ = selected_set_.back();
        selected_set_.clear();
    }
    SyncChips();
    Relayout();
    Invalidate();
    return *this;
}

bool ComboBox::ItemPicked(size_t data) const noexcept {
    const ptrdiff_t row = static_cast<ptrdiff_t>(data);
    if (multi_) return std::binary_search(selected_set_.begin(), selected_set_.end(), row);
    return selected_ == row;
}

bool ComboBox::IsSelected(size_t index) const {
    if (index >= items_.size()) return false;
    return ItemPicked(index);
}

std::vector<size_t> ComboBox::SelectedIndices() const {
    std::vector<size_t> out;
    if (multi_) {
        out.reserve(selected_set_.size());
        for (ptrdiff_t row : selected_set_) {
            if (row >= 0) out.push_back(static_cast<size_t>(row));
        }
    } else if (selected_ >= 0) {
        out.push_back(static_cast<size_t>(selected_));
    }
    return out;
}

void ComboBox::FilterSelection() {
    if (selected_ >= static_cast<ptrdiff_t>(items_.size())) selected_ = -1;
    selected_set_.erase(std::remove_if(selected_set_.begin(), selected_set_.end(),
                                       [this](ptrdiff_t i) {
                                           return i < 0 || i >= static_cast<ptrdiff_t>(items_.size());
                                       }),
                        selected_set_.end());
}

ComboBox& ComboBox::SelectedIndex(ptrdiff_t index) {
    if (index < -1 || index >= static_cast<ptrdiff_t>(items_.size())) return *this;
    if (!multi_ && selected_ == index) return *this;
    selected_ = index;
    if (multi_) {
        selected_set_.clear();
        if (selected_ >= 0) selected_set_.push_back(selected_);
        SyncChips();
        Relayout();
    } else if (editable_ && selected_ >= 0) {
        edit_text_ = items_[static_cast<size_t>(selected_)];
    }
    Invalidate();
    changed_.Emit(selected_, selected_);
    return *this;
}

ComboBox& ComboBox::SelectedIndices(std::vector<ptrdiff_t> indices) {
    indices.erase(std::remove_if(indices.begin(), indices.end(),
                                 [this](ptrdiff_t i) {
                                     return i < 0 || i >= static_cast<ptrdiff_t>(items_.size());
                                 }),
                  indices.end());
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    if (!multi_) {
        SelectedIndex(indices.empty() ? ptrdiff_t{-1} : indices.back());
        return *this;
    }
    selected_set_ = std::move(indices);
    selected_ = selected_set_.empty() ? ptrdiff_t{-1} : selected_set_.back();
    SyncChips();
    Relayout();
    Invalidate();
    changed_.Emit(selected_, selected_);
    return *this;
}

ComboBox& ComboBox::ClearSelection() {
    if (selected_ < 0 && selected_set_.empty()) return *this;
    selected_ = -1;
    selected_set_.clear();
    if (editable_) edit_text_.clear();
    SyncChips();
    Relayout();
    Invalidate();
    changed_.Emit(selected_, selected_);
    return *this;
}

void ComboBox::ToggleItem(size_t data) {
    if (!multi_ || data >= items_.size()) return;
    const ptrdiff_t row = static_cast<ptrdiff_t>(data);
    const auto it = std::lower_bound(selected_set_.begin(), selected_set_.end(), row);
    if (it != selected_set_.end() && *it == row) selected_set_.erase(it);
    else selected_set_.insert(it, row);
    selected_ = row;
    SyncChips();
    Relayout();
    Invalidate();
    changed_.Emit(selected_, selected_);
}

void ComboBox::SyncChips() {
    auto compact = [this] {
        for (size_t i = ChildCount(); i-- > 0;) {
            if (!ChildVisible(i)) Remove(Child(i));
        }
    };
    compact();
    if (!multi_) {
        while (ChildCount() > 0) Remove(Child(0));
        return;
    }
    if (ChildCount() == selected_set_.size()) {
        for (size_t i = 0; i < ChildCount(); ++i) {
            auto& chip = static_cast<Chip&>(Child(i));
            const std::wstring& want = items_[static_cast<size_t>(selected_set_[i])];
            if (chip.Text() != want) chip.Text(want);
        }
        return;
    }
    while (ChildCount() > 0) Remove(Child(0));
    for (ptrdiff_t data : selected_set_) {
        auto& chip = Add<Chip>(items_[static_cast<size_t>(data)]);
        chip.Closable(true).Enabled(enabled_);
        const size_t captured = static_cast<size_t>(data);
        chip.OnClosed([this, captured] {
            for (size_t i = 0; i < ChildCount(); ++i) {
                if (i < selected_set_.size() &&
                    static_cast<size_t>(selected_set_[i]) == captured) {
                    Child(i).Visible(false);
                    break;
                }
            }
            const ptrdiff_t row = static_cast<ptrdiff_t>(captured);
            const auto it = std::lower_bound(selected_set_.begin(), selected_set_.end(), row);
            if (it != selected_set_.end() && *it == row) selected_set_.erase(it);
            selected_ = selected_set_.empty() ? ptrdiff_t{-1} : selected_set_.back();
            Relayout();
            Invalidate();
            changed_.Emit(selected_, selected_);
        });
    }
}

std::wstring ComboBox::SelectedText() const {
    if (multi_) {
        std::wstring text;
        for (size_t n = 0; n < selected_set_.size(); ++n) {
            if (n) text += L", ";
            text += items_[static_cast<size_t>(selected_set_[n])];
        }
        return text;
    }
    if (editable_ && !edit_text_.empty()) return edit_text_;
    return selected_ >= 0 ? items_[static_cast<size_t>(selected_)] : std::wstring();
}

Size ComboBox::Measure(Size available, const Theme& theme) {
    SyncChips();
    const bool finite = AxisFinite(available.w);
    const float width = finite ? available.w : kFieldWidth;
    if (!multi_ || ChildCount() == 0) return {width, theme.input_height};

    // Grow 的 Row 先以无限宽测一遍再按份额重测。无限宽时按 kFieldWidth 折行
    // 会把多行高度写进交叉轴，父级再 Stretch 就把空场拉高。
    const float inner = finite ? std::max(0.0f, width - kChipPadH * 2.0f - kChevronArea)
                               : kInf;
    float line_w = 0.0f;
    int rows = 0;
    auto end_line = [&] {
        ++rows;
        line_w = 0.0f;
    };
    for (size_t i = 0; i < ChildCount(); ++i) {
        if (!ChildVisible(i)) continue;
        const float cw = MeasureChildAt(i, {kInf, kInf}, theme).w;
        if (line_w > 0.0f && line_w + kChipGap + cw > inner + 0.01f) end_line();
        if (line_w > 0.0f) line_w += kChipGap;
        line_w += cw;
    }
    if (line_w > 0.0f || rows == 0) end_line();
    if (rows < 1) rows = 1;
    const float height = kChipPadV * 2.0f + static_cast<float>(rows) * kChipH +
                         static_cast<float>(rows - 1) * kChipGap;
    return {width, std::max(theme.input_height, height)};
}

void ComboBox::Arrange(const Rect& absolute) {
    absolute_ = absolute;
    SyncChips();
    if (!multi_ || ChildCount() == 0) return;
    const float inner = std::max(0.0f, absolute.w - kChipPadH * 2.0f - kChevronArea);
    float x = kChipPadH;
    float y = kChipPadV;
    for (size_t i = 0; i < ChildCount(); ++i) {
        if (!ChildVisible(i)) continue;
        const Size d = ChildDesired(i);
        if (x > kChipPadH && x + d.w > kChipPadH + inner) {
            x = kChipPadH;
            y += kChipH + kChipGap;
        }
        SetChildBounds(Child(i), {x, y, d.w, kChipH});
        ArrangeChildAt(i);
        x += d.w + kChipGap;
    }
}

void ComboBox::OnFocusChanged(bool focused) { focused_ = focused; Animate(); Invalidate(); }
void ComboBox::OnMouseEnter() { Control::OnMouseEnter(); Animate(); }
void ComboBox::OnMouseLeave() { Control::OnMouseLeave(); Animate(); }

void ComboBox::OnMouseDown(Point, uint32_t buttons) {
    if (!enabled_ || !(buttons & MK_LBUTTON)) return;
    pressed_ = true;
    Animate();
}

void ComboBox::OnMouseUp(Point local, uint32_t) {
    if (!enabled_) return;
    pressed_ = false;
    Animate();
    if (local.x >= 0.0f && local.y >= 0.0f && local.x <= absolute_.w && local.y <= absolute_.h) {
        OpenPopup();
    }
}

void ComboBox::OpenPopup(bool from_typing) {
    if (items_.empty() || !window_) return;
    popup_->Rebuild(editable_ && from_typing);
    if (popup_->MatchCount() == 0) return;
    dropdown_open_ = true;
    Animate();
    Invalidate();
    WindowImpl::ShowTransient(window_, popup_.get(), this, absolute_.w, false,
                              [this] { dropdown_open_ = false; Animate(); Invalidate(); });
}

void ComboBox::TypeJump(wchar_t ch) {
    if (items_.empty() || ch < 0x20) return;
    if (jump_age_ > kJumpTimeout) jump_.clear();
    jump_.push_back(ch);
    jump_age_ = 0.0f;
    Animate();

    ptrdiff_t found = -1;
    if (JumpIsRepeat(jump_)) {
        const size_t start = selected_ >= 0 ? static_cast<size_t>(selected_) + 1 : 0;
        const std::wstring_view prefix(jump_.data(), 1);
        for (size_t n = 0; n < items_.size(); ++n) {
            const size_t i = (start + n) % items_.size();
            if (StartsFolded(items_[i], prefix)) {
                found = static_cast<ptrdiff_t>(i);
                break;
            }
        }
    } else {
        for (size_t i = 0; i < items_.size(); ++i) {
            if (StartsFolded(items_[i], jump_)) {
                found = static_cast<ptrdiff_t>(i);
                break;
            }
        }
    }
    if (found < 0) return;
    if (multi_) {
        selected_ = found;
        Invalidate();
    } else if (selected_ != found) {
        SelectedIndex(found);
    }
    if (dropdown_open_) popup_->FocusDataIndex(static_cast<size_t>(found));
}

bool ComboBox::OnChar(wchar_t ch) {
    if (!enabled_) return false;
    if (editable_) {
        if (ch == 0x08) { if (!edit_text_.empty()) edit_text_.pop_back(); }
        else if (ch >= 0x20) edit_text_.push_back(ch);
        else return false;
        selected_ = -1;
        Invalidate();
        OpenPopup(true);
        return true;
    }
    if (ch < 0x20) return false;
    TypeJump(ch);
    return true;
}

bool ComboBox::OnKey(uint32_t vk) {
    if (!enabled_ || items_.empty()) return false;
    if (vk == VK_SPACE || vk == VK_RETURN || vk == VK_F4) { OpenPopup(); return true; }
    if (vk == VK_DOWN && (GetKeyState(VK_MENU) & 0x8000) != 0) { OpenPopup(); return true; }
    if (vk == VK_DOWN || vk == VK_UP) {
        const ptrdiff_t next = Clamp(selected_ + (vk == VK_DOWN ? 1 : -1), ptrdiff_t{0},
                                     static_cast<ptrdiff_t>(items_.size()) - 1);
        if (multi_) {
            selected_ = next;
            Invalidate();
        } else {
            SelectedIndex(next);
        }
        return true;
    }
    return false;
}

bool ComboBox::OnAnimate(float dt) {
    const bool lit = enabled_ && (hovered_ || focused_ || dropdown_open_);
    bool active = Control::OnAnimate(dt);
    active |= EaseTo(glow_t_, lit ? 1.0f : 0.0f, dt, 12.0f);
    active |= EaseTo(chevron_t_, dropdown_open_ ? 1.0f : 0.0f, dt, 12.0f);
    if (!jump_.empty()) {
        jump_age_ += dt;
        if (jump_age_ > kJumpTimeout) {
            jump_.clear();
            jump_age_ = 0.0f;
        } else {
            active = true;
        }
    }
    return active;
}

void ComboBox::Draw(Painter& painter, const Theme& theme) {
    const float radius = theme.radius_control;
    Color fill = theme.fill_input;
    Color border{theme.accent.r, theme.accent.g, theme.accent.b,
                 Lerp(0.20f, 0.50f, glow_t_) * theme.glow_intensity};
    Color label = Mix(Color{theme.text.r, theme.text.g, theme.text.b, 0.70f}, theme.text, glow_t_);
    if (!enabled_) { fill = theme.fill_input_disabled; border = theme.control_stroke; label = theme.text_disabled; }
    else if (dropdown_open_ || focused_) { fill = theme.fill_input_focus; border = theme.accent; painter.DrawGlow(absolute_, radius, theme.glow_sm); }
    painter.FillRoundedRect(absolute_, radius, fill);
    painter.DrawInnerLight(absolute_, radius, theme.edge_light, Color{0.0f, 0.0f, 0.0f, 0.35f});
    painter.StrokeRoundedRect(absolute_, radius, border);
    painter.DrawChevron({absolute_.Right() - kChevronArea * 0.5f, absolute_.y + absolute_.h * 0.5f},
                        16.0f, 180.0f * chevron_t_, enabled_ ? theme.text_secondary : theme.text_disabled, 1.6f);
    if (multi_ && ChildCount() > 0) return;
    const std::wstring selected = SelectedText();
    const std::wstring& text = selected.empty() ? placeholder_ : selected;
    if (!text.empty()) painter.DrawText(text, {absolute_.x + kPadX, absolute_.y,
                                               absolute_.w - kPadX - kChevronArea, absolute_.h},
                                        TextRole::Body, selected.empty() ? theme.text_secondary : label);
}

ComboBox& ComboBox::BindSelectedIndex(Property<int>& p) {
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
