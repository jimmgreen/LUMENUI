#include "lumen/TreeTable.h"
#include "lumen/Painter.h"
#include <windows.h>
#include <algorithm>
#include <utility>

namespace lumen {

TreeTable& TreeTable::AddColumn(std::wstring_view title, float width) {
    if (columns_.size() >= kMaxColumns) return *this;
    columns_.push_back({std::wstring(title), std::max(0.0f, width)});
    Invalidate();
    return *this;
}

void TreeTable::Roots(size_t count) {
    root_count_ = count;
    root_ids_.resize(count);
    for (size_t i = 0; i < count; ++i) root_ids_[i] = i;
    RebuildVisible();
}

void TreeTable::ApplyTreeModel() {
    if (!tree_model_) {
        Roots(0);
        return;
    }
    ChildCount([this](size_t id) { return tree_model_ ? tree_model_->ChildCount(id) : 0; });
    ChildAt([this](size_t id, size_t index) {
        return tree_model_ ? tree_model_->Child(id, index) : 0;
    });
    ItemText([this](size_t id, std::wstring& s) {
        ItemRow row;
        if (tree_model_) tree_model_->Get(id, row);
        s = std::move(row.text);
    });
    ItemGlyph([this](size_t id, std::wstring& s) {
        ItemRow row;
        if (tree_model_) tree_model_->Get(id, row);
        s = std::move(row.glyph);
    });
    const size_t n = tree_model_->ChildCount(TreeModel::kRoot);
    root_count_ = n;
    root_ids_.resize(n);
    for (size_t i = 0; i < n; ++i) root_ids_[i] = tree_model_->Child(TreeModel::kRoot, i);
    RebuildVisible();
}

TreeTable& TreeTable::Bind(TreeModel& model) {
    owned_tree_.reset();
    tree_model_ = &model;
    ApplyTreeModel();
    tree_reset_ = ScopedConnection(model.OnReset([this] { ApplyTreeModel(); }));
    tree_detached_ = ScopedConnection(model.OnDetached([this] {
        tree_model_ = nullptr;
        owned_tree_.reset();
        Roots(0);
    }));
    return *this;
}

TreeTable& TreeTable::Bind(std::shared_ptr<TreeModel> model) {
    if (!model) return *this;
    TreeModel& ref = *model;
    Bind(ref);
    owned_tree_ = std::move(model);
    return *this;
}

bool TreeTable::HasChildren(size_t id) const {
    return child_count_ && child_count_(id) > 0;
}

TreeTable& TreeTable::Expand(size_t id, bool on) {
    const bool was = Expanded(id);
    if (was == on) return *this;
    if (on && !HasChildren(id)) return *this;
    if (on) expanded_.insert(id);
    else expanded_.erase(id);
    RebuildVisible();
    Invalidate();
    expanded_changed_.Emit(id, on);
    return *this;
}

TreeTable& TreeTable::CollapseAll() {
    if (expanded_.empty()) return *this;
    expanded_.clear();
    RebuildVisible();
    Invalidate();
    return *this;
}

TreeTable& TreeTable::ExpandAll() {
    if (!child_count_ || !child_at_) return *this;
    std::vector<size_t> stack;
    for (size_t i = root_ids_.size(); i-- > 0;) stack.push_back(root_ids_[i]);
    size_t steps = 0;
    while (!stack.empty() && steps++ <= 1000000) {
        const size_t id = stack.back();
        stack.pop_back();
        const size_t children = child_count_(id);
        if (!children) continue;
        expanded_.insert(id);
        for (size_t i = 0; i < children; ++i) stack.push_back(child_at_(id, i));
    }
    RebuildVisible();
    return *this;
}

TreeTable& TreeTable::SetFlatData(const std::vector<size_t>& parents) {
    const size_t n = parents.size();
    flat_children_.assign(n, {});
    root_ids_.clear();
    for (size_t i = 0; i < n; ++i) {
        const size_t p = parents[i];
        if (p == kNone || p >= n) {
            root_ids_.push_back(i);
        } else {
            flat_children_[p].push_back(i);
        }
    }
    child_count_ = [this](size_t id) {
        return id < flat_children_.size() ? flat_children_[id].size() : size_t{0};
    };
    child_at_ = [this](size_t id, size_t index) { return flat_children_[id][index]; };
    root_count_ = root_ids_.size();
    RebuildVisible();
    return *this;
}

TreeTable& TreeTable::Refresh() {
    RebuildVisible();
    RelayoutParent();
    return *this;
}

void TreeTable::RebuildVisible() {
    const size_t keep_id = SelectedId();
    size_t keep_parent = kNone;
    if (selected_row_ >= 0 && static_cast<size_t>(selected_row_) < visible_parent_.size()) {
        keep_parent = visible_parent_[static_cast<size_t>(selected_row_)];
    }
    visible_.clear();
    depth_.clear();
    visible_parent_.clear();
    visible_.reserve(root_count_);
    depth_.reserve(root_count_);
    visible_parent_.reserve(root_count_);
    struct Frame {
        size_t id;
        int depth;
        size_t parent;
    };
    std::vector<Frame> stack;
    stack.reserve(16);
    for (size_t i = root_ids_.size(); i-- > 0;) {
        stack.push_back({root_ids_[i], 0, kNone});
    }
    while (!stack.empty()) {
        const Frame frame = stack.back();
        stack.pop_back();
        visible_.push_back(frame.id);
        depth_.push_back(frame.depth);
        visible_parent_.push_back(frame.parent);
        if (!Expanded(frame.id) || !child_count_) continue;
        const size_t children = child_count_(frame.id);
        if (!children || !child_at_) continue;
        for (size_t i = children; i-- > 0;) {
            stack.push_back({child_at_(frame.id, i), frame.depth + 1, frame.id});
        }
    }
    ClampSelection(keep_id, keep_parent);
    ClampScroll();
    Invalidate();
}

void TreeTable::ClampSelection(size_t keep_id, size_t keep_parent) {
    if (keep_id != kNone) {
        const ptrdiff_t row = RowOfId(keep_id);
        if (row >= 0) {
            selected_row_ = row;
            return;
        }
        if (keep_parent != kNone) {
            const ptrdiff_t parent_row = RowOfId(keep_parent);
            if (parent_row >= 0) {
                selected_row_ = parent_row;
                return;
            }
        }
    }
    if (visible_.empty()) {
        selected_row_ = -1;
        return;
    }
    if (selected_row_ < 0) return;
    selected_row_ = std::min(selected_row_, static_cast<ptrdiff_t>(visible_.size()) - 1);
}

void TreeTable::ClampScroll() {
    target_offset_ = Clamp(target_offset_, 0.0f, MaxScroll());
    scroll_offset_ = Clamp(scroll_offset_, 0.0f, MaxScroll());
}

float TreeTable::HeaderHeight() const noexcept {
    return columns_.empty() ? 0.0f : kHeaderH;
}

float TreeTable::BodyHeight() const noexcept {
    return std::max(0.0f, absolute_.h - HeaderHeight());
}

float TreeTable::MaxScroll() const {
    const float content = static_cast<float>(visible_.size()) * std::max(theme_row_height_, 1.0f);
    return std::max(0.0f, content - BodyHeight());
}

size_t TreeTable::SelectedId() const noexcept {
    return selected_row_ >= 0 && selected_row_ < static_cast<ptrdiff_t>(visible_.size())
               ? visible_[static_cast<size_t>(selected_row_)]
               : kNone;
}

ptrdiff_t TreeTable::RowOfId(size_t id) const {
    for (size_t i = 0; i < visible_.size(); ++i) {
        if (visible_[i] == id) return static_cast<ptrdiff_t>(i);
    }
    return -1;
}

TreeTable& TreeTable::SelectedId(size_t id) {
    const ptrdiff_t row = RowOfId(id);
    if (row < 0) return *this;
    SelectRow(row);
    return *this;
}

TreeTable& TreeTable::RevealId(size_t id) {
    if (!child_count_ || !child_at_) return *this;
    std::vector<std::pair<size_t, size_t>> all;
    all.reserve(root_count_ + visible_.size());
    std::vector<std::pair<size_t, size_t>> stack;
    for (size_t i = root_ids_.size(); i-- > 0;) stack.push_back({root_ids_[i], kNone});
    while (!stack.empty()) {
        const auto [node, parent] = stack.back();
        stack.pop_back();
        all.push_back({node, parent});
        const size_t children = child_count_(node);
        for (size_t i = 0; i < children; ++i) {
            stack.push_back({child_at_(node, i), node});
        }
    }
    std::vector<size_t> chain;
    size_t node = id;
    size_t guard = 0;
    while (node != kNone && guard++ <= all.size()) {
        chain.push_back(node);
        size_t parent = kNone;
        for (const auto& entry : all) {
            if (entry.first == node) {
                parent = entry.second;
                break;
            }
        }
        if (parent == kNone) break;
        node = parent;
    }
    for (size_t i = chain.size(); i-- > 1;) expanded_.insert(chain[i]);
    RebuildVisible();
    ScrollToId(id);
    return *this;
}

void TreeTable::ScrollToId(size_t id) {
    const ptrdiff_t row = RowOfId(id);
    if (row < 0) return;
    ScrollRowIntoView(row);
}

void TreeTable::ScrollRowIntoView(ptrdiff_t row) {
    if (row < 0) return;
    const float row_h = std::max(theme_row_height_, 1.0f);
    const float top = static_cast<float>(row) * row_h;
    const float bottom = top + row_h;
    const float body_h = BodyHeight();
    if (top < scroll_offset_) {
        target_offset_ = top;
    } else if (bottom > scroll_offset_ + body_h) {
        target_offset_ = bottom - body_h;
    }
    target_offset_ = Clamp(target_offset_, 0.0f, MaxScroll());
    Animate();
}

void TreeTable::SelectRow(ptrdiff_t row) {
    if (row < -1 || row >= static_cast<ptrdiff_t>(visible_.size())) return;
    if (selected_row_ == row) return;
    selected_row_ = row;
    if (row >= 0) ScrollRowIntoView(row);
    Invalidate();
    selection_changed_.Emit(SelectedId());
}

void TreeTable::MoveSelection(ptrdiff_t delta) {
    if (visible_.empty()) return;
    const ptrdiff_t next =
        selected_row_ < 0 ? (delta > 0 ? 0 : static_cast<ptrdiff_t>(visible_.size()) - 1)
                          : Clamp(selected_row_ + delta, ptrdiff_t{0},
                                  static_cast<ptrdiff_t>(visible_.size()) - 1);
    SelectRow(next);
}

void TreeTable::ColumnMetrics(float inner_w, float* xs, float* ws, size_t cap) const {
    const size_t n = std::min(columns_.size(), cap);
    float fixed = 0.0f;
    int flex = 0;
    for (size_t i = 0; i < n; ++i) {
        if (columns_[i].width > 0.5f) fixed += columns_[i].width;
        else ++flex;
    }
    const float min_flex = static_cast<float>(flex) * kMinFlex;
    const float extra = (flex > 0 && inner_w > fixed + min_flex)
                            ? (inner_w - fixed - min_flex) / static_cast<float>(flex)
                            : 0.0f;
    float x = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        ws[i] = columns_[i].width > 0.5f ? columns_[i].width : (kMinFlex + extra);
        xs[i] = x;
        x += ws[i];
    }
}

Size TreeTable::Measure(Size available, const Theme& theme) {
    theme_row_height_ = theme.list_row_height;
    const float w = (available.w > 0.5f && available.w < 1.0e4f) ? available.w : 480.0f;
    return {w, 240.0f};
}

void TreeTable::OnFocusChanged(bool focused) {
    Control::OnFocusChanged(focused);
    Invalidate();
}

bool TreeTable::OnAnimate(float dt_seconds) {
    bool moving = EaseTo(scroll_offset_, target_offset_, dt_seconds, 20.0f, 0.1f);
    moving |= EaseTo(expand_progress_, (hovered_ || dragging_) ? 1.0f : 0.0f, dt_seconds, 18.0f);
    return moving || Control::OnAnimate(dt_seconds);
}

Rect TreeTable::VerticalTrack() const noexcept {
    const float top = BodyTop();
    return {absolute_.Right() - kBarHit, absolute_.y + top, kBarHit, BodyHeight()};
}

ScrollThumb TreeTable::Thumb(float expand) const noexcept {
    const float content = static_cast<float>(visible_.size()) * std::max(theme_row_height_, 1.0f);
    const Rect body{absolute_.x, absolute_.y + BodyTop(), absolute_.w, BodyHeight()};
    return MakeScrollThumb(body, content, scroll_offset_, expand, true);
}

bool TreeTable::CapturesOverlay(Point p) const {
    if (dragging_) return true;
    if (MaxScroll() <= 0.5f) return false;
    return VerticalTrack().Contains(p);
}

bool TreeTable::BeginScrollDrag(Point local) {
    if (MaxScroll() <= 0.5f) return false;
    const Point world{absolute_.x + local.x, absolute_.y + local.y};
    if (!VerticalTrack().Contains(world)) return false;
    const ScrollThumb thumb = Thumb(1.0f);
    dragging_ = true;
    const float body_local_y = local.y - BodyTop();
    if (thumb.visible && thumb.rect.Contains(world)) {
        drag_grab_ = world.y - thumb.rect.y;
    } else {
        const float thumb_h = thumb.visible ? thumb.rect.h : 20.0f;
        const float track = std::max(1.0f, BodyHeight() - thumb_h);
        const float t = Clamp((body_local_y - thumb_h * 0.5f) / track, 0.0f, 1.0f);
        scroll_offset_ = target_offset_ = t * MaxScroll();
        drag_grab_ = thumb_h * 0.5f;
        Invalidate();
    }
    Animate();
    return true;
}

bool TreeTable::OnKey(uint32_t vk) {
    const float row_h = std::max(theme_row_height_, 1.0f);
    const ptrdiff_t page =
        std::max(ptrdiff_t{1}, static_cast<ptrdiff_t>(BodyHeight() / row_h));
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
        SelectRow(0);
        return true;
    case VK_END:
        if (!visible_.empty()) SelectRow(static_cast<ptrdiff_t>(visible_.size()) - 1);
        return true;
    case VK_RIGHT:
        if (selected_row_ >= 0) {
            const size_t id = visible_[static_cast<size_t>(selected_row_)];
            if (HasChildren(id) && !Expanded(id)) {
                Expand(id, true);
            } else if (child_count_ && child_count_(id) > 0) {
                SelectRow(selected_row_ + 1);
            }
        }
        return true;
    case VK_LEFT:
        if (selected_row_ >= 0) {
            const size_t id = visible_[static_cast<size_t>(selected_row_)];
            if (Expanded(id)) {
                Expand(id, false);
            } else {
                const size_t parent = visible_parent_[static_cast<size_t>(selected_row_)];
                if (parent != kNone) {
                    const ptrdiff_t row = RowOfId(parent);
                    if (row >= 0) SelectRow(row);
                }
            }
        }
        return true;
    case VK_RETURN:
        if (selected_row_ >= 0) {
            activate_.Emit(visible_[static_cast<size_t>(selected_row_)]);
        }
        return true;
    default:
        return false;
    }
}

ptrdiff_t TreeTable::RowAt(Point local) const {
    if (local.x < 0.0f || local.x > absolute_.w) return -1;
    const float y = local.y - BodyTop() + scroll_offset_;
    if (y < 0.0f) return -1;
    const float row_h = std::max(theme_row_height_, 1.0f);
    const ptrdiff_t row = static_cast<ptrdiff_t>(y / row_h);
    if (row >= static_cast<ptrdiff_t>(visible_.size())) return -1;
    return row;
}

void TreeTable::OnMouseDown(Point local, uint32_t buttons) {
    if (!(buttons & 0x0001)) return;
    Focus();
    if (BeginScrollDrag(local)) return;
    if (local.y < BodyTop()) return;
    const ptrdiff_t row = RowAt(local);
    if (row < 0) return;
    SelectRow(row);
    const size_t id = visible_[static_cast<size_t>(row)];
    float xs[kMaxColumns]{};
    float ws[kMaxColumns]{};
    const size_t n = std::min(columns_.size(), kMaxColumns);
    if (n > 0) ColumnMetrics(absolute_.w, xs, ws, n);
    const float tree_w = n > 0 ? ws[0] : absolute_.w;
    if (local.x > tree_w) return;
    const float indent = 4.0f + static_cast<float>(DepthAt(row)) * kIndent;
    if (HasChildren(id) && local.x >= indent && local.x < indent + kExpanderZone) {
        ToggleExpanded(id);
    }
}

void TreeTable::OnMouseDoubleClick(Point local) {
    if (local.y < BodyTop()) return;
    const ptrdiff_t row = RowAt(local);
    if (row < 0) return;
    const size_t id = visible_[static_cast<size_t>(row)];
    if (HasChildren(id)) {
        ToggleExpanded(id);
        return;
    }
    activate_.Emit(id);
}

void TreeTable::OnMouseMove(Point local, uint32_t buttons) {
    (void)buttons;
    if (dragging_) {
        const ScrollThumb thumb = Thumb(1.0f);
        const float thumb_h = thumb.visible ? thumb.rect.h : 20.0f;
        const float track = std::max(1.0f, BodyHeight() - thumb_h);
        const float body_local_y = local.y - BodyTop();
        const float t = Clamp((body_local_y - drag_grab_) / track, 0.0f, 1.0f);
        scroll_offset_ = target_offset_ = t * MaxScroll();
        Invalidate();
        return;
    }
    const ptrdiff_t row = RowAt(local);
    if (row != hover_row_) {
        hover_row_ = row;
        Animate();
        Invalidate();
    }
}

void TreeTable::OnMouseUp(Point, uint32_t) {
    dragging_ = false;
    Animate();
}

void TreeTable::OnMouseLeave() {
    Control::OnMouseLeave();
    hover_row_ = -1;
    Animate();
    Invalidate();
}

bool TreeTable::OnWheel(float delta) {
    if (MaxScroll() <= 0.0f) return false;
    const float row_h = std::max(theme_row_height_, 1.0f);
    target_offset_ = Clamp(target_offset_ - delta * row_h * 2.5f, 0.0f, MaxScroll());
    Animate();
    return true;
}

void TreeTable::Draw(Painter& painter, const Theme& theme) {
    theme_row_height_ = theme.list_row_height;
    const float row_h = std::max(theme_row_height_, 1.0f);
    ClampScroll();
    painter.PushClip(absolute_);
    painter.FillRoundedRect(absolute_, theme.radius_control, theme.fill_input);

    float xs[kMaxColumns]{};
    float ws[kMaxColumns]{};
    const size_t n = std::min(columns_.size(), kMaxColumns);
    if (n > 0) ColumnMetrics(absolute_.w, xs, ws, n);
    const float header_h = HeaderHeight();

    if (header_h > 0.5f && n > 0) {
        const Rect header{absolute_.x, absolute_.y, absolute_.w, header_h};
        painter.FillRect(header, theme.fill_input_hover);
        for (size_t c = 0; c < n; ++c) {
            const float cell_w = std::max(0.0f, ws[c] - kCellPadX * 2.0f);
            if (cell_w > 0.5f && !columns_[c].title.empty()) {
                painter.DrawText(columns_[c].title,
                                 {absolute_.x + xs[c] + kCellPadX, header.y, cell_w, header.h},
                                 TextRole::CaptionStrong, theme.text_secondary);
            }
            if (c + 1 < n && ws[c] > 1.0f) {
                painter.FillRect({absolute_.x + xs[c] + ws[c], header.y + 8.0f, 1.0f,
                                  std::max(0.0f, header.h - 16.0f)},
                                 theme.stroke_divider);
            }
        }
        painter.FillRect({absolute_.x, absolute_.y + header_h - 1.0f, absolute_.w, 1.0f},
                         theme.stroke_divider);
    }

    const float body_h = BodyHeight();
    const float body_y = absolute_.y + header_h;
    if (body_h > 0.5f) {
        painter.PushClip({absolute_.x, body_y, absolute_.w, body_h});
        const ptrdiff_t first = static_cast<ptrdiff_t>(scroll_offset_ / row_h);
        const ptrdiff_t count = static_cast<ptrdiff_t>(visible_.size());
        const ptrdiff_t visible_rows = static_cast<ptrdiff_t>(body_h / row_h) + 2;
        for (ptrdiff_t row = first; row < first + visible_rows && row < count; ++row) {
            if (row < 0) continue;
            const Rect row_rect{absolute_.x, body_y + static_cast<float>(row) * row_h - scroll_offset_,
                                absolute_.w, row_h};
            if (row == selected_row_) {
                painter.FillRect(row_rect, theme.fill_selected);
                painter.FillRoundedRect({row_rect.x, row_rect.y, 3.0f, row_rect.h}, 1.5f,
                                        theme.accent);
            } else if (row == hover_row_ && enabled_) {
                painter.FillRect(row_rect, theme.fill_hover);
            }

            const size_t id = visible_[static_cast<size_t>(row)];
            const int depth = depth_[static_cast<size_t>(row)];
            const float tree_w = n > 0 ? ws[0] : absolute_.w;
            float x = row_rect.x + 4.0f + static_cast<float>(depth) * kIndent;
            const bool row_hot = row == hover_row_ || row == selected_row_;
            if (HasChildren(id)) {
                painter.DrawChevron({x + kExpanderZone * 0.5f, row_rect.y + row_h * 0.5f}, 11.0f,
                                    Expanded(id) ? 0.0f : -90.0f,
                                    row_hot ? theme.text : theme.text_secondary, 1.7f);
            }
            x += kExpanderZone;
            draw_glyph_.clear();
            draw_text_.clear();
            if (item_glyph_) item_glyph_(id, draw_glyph_);
            if (item_text_) item_text_(id, draw_text_);
            else if (cell_text_) cell_text_(id, 0, draw_text_);
            if (!draw_glyph_.empty()) {
                painter.DrawIcon(draw_glyph_, {x, row_rect.y, 16.0f, row_rect.h}, 16.0f,
                                 theme.text_secondary);
                x += 22.0f;
            }
            const float text_right = row_rect.x + tree_w - 8.0f;
            if (!draw_text_.empty() && text_right > x) {
                painter.DrawText(draw_text_, {x, row_rect.y, text_right - x, row_rect.h},
                                 TextRole::Body, theme.text);
            }

            if (n > 1 && cell_text_) {
                for (size_t c = 1; c < n; ++c) {
                    const float cell_w = std::max(0.0f, ws[c] - kCellPadX * 2.0f);
                    if (cell_w <= 0.5f) continue;
                    draw_text_.clear();
                    cell_text_(id, c, draw_text_);
                    if (draw_text_.empty()) continue;
                    painter.DrawText(draw_text_,
                                     {row_rect.x + xs[c] + kCellPadX, row_rect.y, cell_w, row_h},
                                     TextRole::Caption, theme.text);
                }
            }
        }
        painter.PopClip();
    }

    const Color thumb_color =
        (hovered_ || dragging_) ? theme.scrollbar_thumb_hover : theme.scrollbar_thumb;
    painter.DrawScrollThumb(Thumb(expand_progress_), thumb_color);
    painter.PopClip();
}

} // namespace lumen
