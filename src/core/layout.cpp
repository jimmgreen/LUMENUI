#include "lumen/Panel.h"
#include "lumen/Painter.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace lumen {
namespace {

constexpr float kInf = 1.0e5f;
constexpr float kFiniteCap = 1.0e4f;

bool AxisFinite(float v) noexcept { return v >= 0.0f && v < kFiniteCap; }

// 按嵌套深度租用：子级 Measure/Arrange 不得覆盖父级还在读的缓冲。
struct LayoutScratch {
    std::vector<float> along;
    std::vector<float> desired_w;
    std::vector<float> desired_h;
    std::vector<size_t> vis;
    std::vector<float> col_w;
    std::vector<float> row_h;
};

thread_local std::vector<std::unique_ptr<LayoutScratch>> tls_pool;
thread_local int tls_depth = 0;

struct ScratchScope {
    LayoutScratch& s;
    ScratchScope() : s(Acquire()) { ++tls_depth; }
    ~ScratchScope() { --tls_depth; }
    ScratchScope(const ScratchScope&) = delete;
    ScratchScope& operator=(const ScratchScope&) = delete;

private:
    static LayoutScratch& Acquire() {
        if (tls_depth >= static_cast<int>(tls_pool.size())) {
            tls_pool.push_back(std::make_unique<LayoutScratch>());
        }
        return *tls_pool[static_cast<size_t>(tls_depth)];
    }
};

} // namespace

StackPanel& StackPanel::Comfortable() {
    spacing_ = 12.0f;
    padding_h_ = 16.0f;
    padding_v_ = 16.0f;
    Relayout();
    return *this;
}

StackPanel& StackPanel::Dense() {
    spacing_ = 4.0f;
    padding_h_ = 8.0f;
    padding_v_ = 8.0f;
    Relayout();
    return *this;
}

// ---- Spacer ----

Spacer::Spacer() { grow_weight_ = 1.0f; }

Spacer::Spacer(float gap) : gap_(std::max(0.0f, gap)) {}

Size Spacer::Measure(Size, const Theme&) {
    return {gap_, gap_};
}

void Spacer::Draw(Painter&, const Theme&) {}

// ---- StackPanel ----

Size StackPanel::Measure(Size available, const Theme& theme) {
    const bool vertical = orientation_ == Orientation::Vertical;
    const float pad_main = vertical ? padding_v_ : padding_h_;
    const float pad_cross = vertical ? padding_h_ : padding_v_;
    const float available_cross =
        std::max(0.0f, (vertical ? available.w : available.h) - pad_cross * 2.0f);
    const float available_main =
        (vertical ? available.h : available.w) - pad_main * 2.0f;
    const Size unconstrained =
        vertical ? Size{available_cross, kInf} : Size{kInf, available_cross};

    float grow_total = 0.0f;
    int visible = 0;
    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        ++visible;
        grow_total += std::max(0.0f, Child(i).GrowWeight());
    }
    const float spacing_total =
        visible > 1 ? spacing_ * static_cast<float>(visible - 1) : 0.0f;

    float non_grow_main = 0.0f;
    float cross = 0.0f;
    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        const Size desired = MeasureChildAt(i, unconstrained, theme);
        const float child_cross = vertical ? desired.w : desired.h;
        cross = std::max(cross, child_cross);
        if (Child(i).GrowWeight() <= 0.0f) {
            non_grow_main += vertical ? desired.h : desired.w;
        }
    }

    float grow_main = 0.0f;
    if (grow_total > 0.0f && AxisFinite(available_main)) {
        const float leftover = std::max(0.0f, available_main - non_grow_main - spacing_total);
        for (size_t i = 0; i < children_.size(); ++i) {
            if (!ChildVisible(i)) continue;
            const float weight = Child(i).GrowWeight();
            if (weight <= 0.0f) continue;
            const float share = leftover * (weight / grow_total);
            const Size child_av =
                vertical ? Size{available_cross, share} : Size{share, available_cross};
            const Size desired = MeasureChildAt(i, child_av, theme);
            grow_main += share;
            cross = std::max(cross, vertical ? desired.w : desired.h);
        }
    } else if (grow_total > 0.0f) {
        for (size_t i = 0; i < children_.size(); ++i) {
            if (!ChildVisible(i)) continue;
            if (Child(i).GrowWeight() <= 0.0f) continue;
            const Size& d = ChildDesired(i);
            grow_main += vertical ? d.h : d.w;
        }
    }

    const float along = pad_main * 2.0f + non_grow_main + grow_main + spacing_total;
    return vertical ? Size{cross + padding_h_ * 2.0f, along}
                    : Size{along, cross + padding_v_ * 2.0f};
}

void StackPanel::Arrange(const Rect& absolute) {
    absolute_ = absolute;
    const bool vertical = orientation_ == Orientation::Vertical;
    const float main_size = vertical ? absolute.h : absolute.w;
    const float cross_size = vertical ? absolute.w : absolute.h;
    const float pad_main = vertical ? padding_v_ : padding_h_;
    const float pad_cross = vertical ? padding_h_ : padding_v_;
    const float inner_main = std::max(0.0f, main_size - pad_main * 2.0f);
    const float inner_cross = std::max(0.0f, cross_size - pad_cross * 2.0f);

    float grow_total = 0.0f;
    float natural_sum = 0.0f;
    int visible = 0;
    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        ++visible;
        const float weight = Child(i).GrowWeight();
        grow_total += std::max(0.0f, weight);
        if (weight <= 0.0f) {
            const Size& d = ChildDesired(i);
            natural_sum += vertical ? d.h : d.w;
        }
    }
    const float spacing_all =
        visible > 1 ? spacing_ * static_cast<float>(visible - 1) : 0.0f;
    const float leftover = std::max(0.0f, inner_main - natural_sum - spacing_all);

    ScratchScope scratch;
    std::vector<float>& along_size = scratch.s.along;
    along_size.assign(children_.size(), 0.0f);
    float used = spacing_all;
    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        const float weight = Child(i).GrowWeight();
        const Size& d = ChildDesired(i);
        const float natural = vertical ? d.h : d.w;
        const float along = (weight > 0.0f && grow_total > 0.0f)
                                ? leftover * (weight / grow_total)
                                : natural;
        along_size[i] = along;
        used += along;
    }

    float extra = inner_main - used;
    float origin = pad_main;
    float gap = spacing_;
    if (grow_total <= 0.0f && extra > 0.0f) {
        switch (main_align_) {
        case MainAlign::Center:
            origin += extra * 0.5f;
            break;
        case MainAlign::End:
            origin += extra;
            break;
        case MainAlign::SpaceBetween:
            if (visible > 1) {
                gap = spacing_ + extra / static_cast<float>(visible - 1);
            }
            break;
        case MainAlign::Start:
        default:
            break;
        }
    }

    float position = origin;
    bool first = true;
    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        if (!first) position += gap;
        first = false;
        const Size& desired = ChildDesired(i);
        const float along = along_size[i];
        const float cross = vertical ? desired.w : desired.h;
        float cross_pos = pad_cross;
        // Stretch 交叉轴拉满容器内宽，不得按期望尺寸撑出父级（折叠侧栏会溢到内容区）。
        float cross_extent = inner_cross;
        const bool stretch = cross_align_ == CrossAlign::Stretch || Child(i).FillsCross();
        if (!stretch && cross_align_ == CrossAlign::Start) {
            cross_extent = cross;
        } else if (!stretch && cross_align_ == CrossAlign::Center) {
            cross_extent = cross;
            cross_pos = pad_cross + (inner_cross - cross) * 0.5f;
        } else if (!stretch && cross_align_ == CrossAlign::End) {
            cross_extent = cross;
            cross_pos = pad_cross + std::max(0.0f, inner_cross - cross);
        }
        const Rect slot = vertical ? Rect{cross_pos, position, cross_extent, along}
                                   : Rect{position, cross_pos, along, cross_extent};
        position += along;
        SetChildBounds(Child(i), slot);
    }
    // 先写完所有 bounds 再递归 Arrange：子级会租用更深一层 scratch。
    for (size_t i = 0; i < children_.size(); ++i) {
        if (ChildVisible(i)) ArrangeChildAt(i);
    }
}

// ---- Grid ----

Grid::Grid(int equal_columns) {
    const int n = std::max(1, equal_columns);
    tracks_.assign(static_cast<size_t>(n), 1.0f);
}

Grid& Grid::Columns(int equal_columns) {
    const int n = std::max(1, equal_columns);
    tracks_.assign(static_cast<size_t>(n), 1.0f);
    Relayout();
    return *this;
}

Grid& Grid::Gap(float column, float row) {
    gap_x_ = std::max(0.0f, column);
    gap_y_ = std::max(0.0f, row);
    Relayout();
    return *this;
}

Grid& Grid::Padding(float horizontal, float vertical) {
    padding_h_ = horizontal;
    padding_v_ = vertical;
    Relayout();
    return *this;
}

namespace {

struct GridPlan {
    std::vector<size_t>* vis = nullptr;
    std::vector<float>* col_w = nullptr;
    std::vector<float>* row_h = nullptr;
    int n_cols = 1;
    int n_rows = 0;
};

int ColCount(const std::vector<float>& tracks) {
    return std::max(1, static_cast<int>(tracks.size()));
}

void CollectVisible(const Panel& panel, GridPlan& plan) {
    plan.vis->clear();
    for (size_t i = 0; i < panel.ChildCount(); ++i) {
        if (panel.Child(i).Visible()) plan.vis->push_back(i);
    }
}

void SizeTracks(GridPlan& plan, const std::vector<float>& tracks, float inner_w,
                float gap_x, bool width_finite, const std::vector<float>* auto_from_desired) {
    plan.n_cols = ColCount(tracks);
    plan.col_w->assign(static_cast<size_t>(plan.n_cols), 0.0f);
    const float gaps = gap_x * static_cast<float>(std::max(0, plan.n_cols - 1));

    float fr_sum = 0.0f;
    float auto_sum = 0.0f;
    for (int c = 0; c < plan.n_cols; ++c) {
        const float track = c < static_cast<int>(tracks.size()) ? tracks[static_cast<size_t>(c)] : 1.0f;
        if (track > 0.0f) {
            fr_sum += track;
            continue;
        }
        float w = 0.0f;
        if (auto_from_desired) {
            for (int i = 0; i < static_cast<int>(plan.vis->size()); ++i) {
                if (i % plan.n_cols != c) continue;
                w = std::max(w, (*auto_from_desired)[(*plan.vis)[static_cast<size_t>(i)]]);
            }
        }
        (*plan.col_w)[static_cast<size_t>(c)] = w;
        auto_sum += w;
    }

    if (fr_sum > 0.0f && width_finite) {
        const float leftover = std::max(0.0f, inner_w - gaps - auto_sum);
        for (int c = 0; c < plan.n_cols; ++c) {
            const float track = c < static_cast<int>(tracks.size()) ? tracks[static_cast<size_t>(c)] : 1.0f;
            if (track > 0.0f) {
                (*plan.col_w)[static_cast<size_t>(c)] = leftover * (track / fr_sum);
            }
        }
    }
}

GridPlan BindGrid(LayoutScratch& scratch) {
    return GridPlan{&scratch.vis, &scratch.col_w, &scratch.row_h, 1, 0};
}

} // namespace

Size Grid::Measure(Size available, const Theme& theme) {
    if (tracks_.empty()) tracks_.assign(1, 1.0f);
    ScratchScope scratch;
    GridPlan plan = BindGrid(scratch.s);
    CollectVisible(*this, plan);
    plan.n_cols = ColCount(tracks_);
    const int n_items = static_cast<int>(plan.vis->size());
    plan.n_rows = n_items == 0 ? 0 : (n_items + plan.n_cols - 1) / plan.n_cols;

    const float inner_w = available.w - padding_h_ * 2.0f;
    const bool width_finite = AxisFinite(available.w);

    std::vector<float>& desired_w = scratch.s.desired_w;
    desired_w.assign(children_.size(), 0.0f);
    for (int c = 0; c < plan.n_cols; ++c) {
        const float track = tracks_[static_cast<size_t>(c)];
        if (track > 0.0f) continue;
        for (int i = 0; i < n_items; ++i) {
            if (i % plan.n_cols != c) continue;
            const Size d = MeasureChildAt((*plan.vis)[static_cast<size_t>(i)], {kInf, kInf}, theme);
            desired_w[(*plan.vis)[static_cast<size_t>(i)]] = d.w;
        }
    }

    if (!width_finite) {
        for (int i = 0; i < n_items; ++i) {
            const Size d = MeasureChildAt((*plan.vis)[static_cast<size_t>(i)], {kInf, kInf}, theme);
            desired_w[(*plan.vis)[static_cast<size_t>(i)]] = d.w;
        }
    }

    SizeTracks(plan, tracks_, inner_w, gap_x_, width_finite, &desired_w);

    if (!width_finite) {
        for (int c = 0; c < plan.n_cols; ++c) {
            if (tracks_[static_cast<size_t>(c)] <= 0.0f) continue;
            float w = 0.0f;
            for (int i = 0; i < n_items; ++i) {
                if (i % plan.n_cols != c) continue;
                w = std::max(w, desired_w[(*plan.vis)[static_cast<size_t>(i)]]);
            }
            (*plan.col_w)[static_cast<size_t>(c)] = w;
        }
    }

    plan.row_h->assign(static_cast<size_t>(std::max(plan.n_rows, 0)), 0.0f);
    for (int i = 0; i < n_items; ++i) {
        const int c = i % plan.n_cols;
        const int r = i / plan.n_cols;
        const Size d = MeasureChildAt((*plan.vis)[static_cast<size_t>(i)],
                                      {(*plan.col_w)[static_cast<size_t>(c)], kInf}, theme);
        (*plan.row_h)[static_cast<size_t>(r)] =
            std::max((*plan.row_h)[static_cast<size_t>(r)], d.h);
    }

    float total_w = padding_h_ * 2.0f + gap_x_ * static_cast<float>(std::max(0, plan.n_cols - 1));
    for (float w : *plan.col_w) total_w += w;
    float total_h = padding_v_ * 2.0f + gap_y_ * static_cast<float>(std::max(0, plan.n_rows - 1));
    for (float h : *plan.row_h) total_h += h;
    return {total_w, total_h};
}

void Grid::Arrange(const Rect& absolute) {
    absolute_ = absolute;
    if (tracks_.empty()) tracks_.assign(1, 1.0f);
    ScratchScope scratch;
    GridPlan plan = BindGrid(scratch.s);
    CollectVisible(*this, plan);
    plan.n_cols = ColCount(tracks_);
    const int n_items = static_cast<int>(plan.vis->size());
    plan.n_rows = n_items == 0 ? 0 : (n_items + plan.n_cols - 1) / plan.n_cols;

    std::vector<float>& desired_w = scratch.s.desired_w;
    std::vector<float>& desired_h = scratch.s.desired_h;
    desired_w.assign(children_.size(), 0.0f);
    desired_h.assign(children_.size(), 0.0f);
    for (size_t vis_i : *plan.vis) {
        desired_w[vis_i] = ChildDesired(vis_i).w;
        desired_h[vis_i] = ChildDesired(vis_i).h;
    }

    const float inner_w = absolute.w - padding_h_ * 2.0f;
    SizeTracks(plan, tracks_, inner_w, gap_x_, AxisFinite(absolute.w), &desired_w);

    plan.row_h->assign(static_cast<size_t>(std::max(plan.n_rows, 0)), 0.0f);
    for (int i = 0; i < n_items; ++i) {
        const int r = i / plan.n_cols;
        (*plan.row_h)[static_cast<size_t>(r)] =
            std::max((*plan.row_h)[static_cast<size_t>(r)],
                     desired_h[(*plan.vis)[static_cast<size_t>(i)]]);
    }

    float row_sum = 0.0f;
    for (float h : *plan.row_h) row_sum += h;
    const float inner_h = std::max(0.0f, absolute.h - padding_v_ * 2.0f);
    const float row_gaps = gap_y_ * static_cast<float>(std::max(0, plan.n_rows - 1));
    const float extra_h = inner_h - row_sum - row_gaps;
    if (extra_h > 0.0f && plan.n_rows > 0) {
        const float add = extra_h / static_cast<float>(plan.n_rows);
        for (float& h : *plan.row_h) h += add;
    }

    float y = padding_v_;
    for (int r = 0; r < plan.n_rows; ++r) {
        float x = padding_h_;
        for (int c = 0; c < plan.n_cols; ++c) {
            const int i = r * plan.n_cols + c;
            if (i < n_items) {
                const size_t index = (*plan.vis)[static_cast<size_t>(i)];
                SetChildBounds(Child(index),
                               {x, y, (*plan.col_w)[static_cast<size_t>(c)],
                                (*plan.row_h)[static_cast<size_t>(r)]});
            }
            x += (*plan.col_w)[static_cast<size_t>(c)] + gap_x_;
        }
        y += (*plan.row_h)[static_cast<size_t>(r)] + gap_y_;
    }
    for (int i = 0; i < n_items; ++i) {
        ArrangeChildAt((*plan.vis)[static_cast<size_t>(i)]);
    }
}

// ---- WrapPanel ----

Size WrapPanel::Measure(Size available, const Theme& theme) {
    const bool horizontal = orientation_ == Orientation::Horizontal;
    const float pad_main = horizontal ? padding_h_ : padding_v_;
    const float gap_main = horizontal ? gap_x_ : gap_y_;
    const float gap_cross = horizontal ? gap_y_ : gap_x_;
    const float limit_raw = horizontal ? available.w : available.h;
    const float limit = AxisFinite(limit_raw) ? std::max(0.0f, limit_raw - pad_main * 2.0f) : kInf;

    float line_main = 0.0f;
    float line_cross = 0.0f;
    float used_main = 0.0f;
    float used_cross = 0.0f;
    int line_items = 0;
    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        const Size desired = MeasureChildAt(i, {kInf, kInf}, theme);
        const float main = horizontal ? desired.w : desired.h;
        const float cross = horizontal ? desired.h : desired.w;
        if (line_items > 0 && line_main + gap_main + main > limit + 0.01f) {
            used_main = std::max(used_main, line_main);
            used_cross += (used_cross > 0.0f ? gap_cross : 0.0f) + line_cross;
            line_main = 0.0f;
            line_cross = 0.0f;
            line_items = 0;
        }
        if (line_items > 0) line_main += gap_main;
        line_main += main;
        line_cross = std::max(line_cross, cross);
        ++line_items;
    }
    if (line_items > 0) {
        used_main = std::max(used_main, line_main);
        used_cross += (used_cross > 0.0f ? gap_cross : 0.0f) + line_cross;
    }
    return horizontal ? Size{used_main + padding_h_ * 2.0f, used_cross + padding_v_ * 2.0f}
                      : Size{used_cross + padding_h_ * 2.0f, used_main + padding_v_ * 2.0f};
}

void WrapPanel::Arrange(const Rect& absolute) {
    absolute_ = absolute;
    const bool horizontal = orientation_ == Orientation::Horizontal;
    const float pad_main = horizontal ? padding_h_ : padding_v_;
    const float pad_cross = horizontal ? padding_v_ : padding_h_;
    const float gap_main = horizontal ? gap_x_ : gap_y_;
    const float gap_cross = horizontal ? gap_y_ : gap_x_;
    const float limit = std::max(0.0f, (horizontal ? absolute.w : absolute.h) - pad_main * 2.0f);

    float line_main = 0.0f;
    float line_cross = 0.0f;
    float origin_main = pad_main;
    float origin_cross = pad_cross;
    size_t line_begin = 0;
    auto flush = [&](size_t end) {
        float along = origin_main;
        for (size_t i = line_begin; i < end; ++i) {
            if (!ChildVisible(i)) continue;
            const Size& d = ChildDesired(i);
            const float main = horizontal ? d.w : d.h;
            const float cross = horizontal ? d.h : d.w;
            const Rect slot = horizontal ? Rect{along, origin_cross, main, line_cross}
                                         : Rect{origin_cross, along, line_cross, main};
            SetChildBounds(Child(i), slot);
            ArrangeChildAt(i);
            along += main + gap_main;
            (void)cross;
        }
        origin_cross += line_cross + gap_cross;
        origin_main = pad_main;
        line_main = 0.0f;
        line_cross = 0.0f;
        line_begin = end;
    };

    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        const Size& d = ChildDesired(i);
        const float main = horizontal ? d.w : d.h;
        const float cross = horizontal ? d.h : d.w;
        if (line_main > 0.0f && line_main + gap_main + main > limit + 0.01f) {
            flush(i);
        }
        if (line_main > 0.0f) line_main += gap_main;
        line_main += main;
        line_cross = std::max(line_cross, cross);
    }
    if (line_begin < children_.size()) flush(children_.size());
}

Size ZStack::Measure(Size available, const Theme& theme) {
    const float inner_w =
        AxisFinite(available.w) ? std::max(0.0f, available.w - padding_h_ * 2.0f) : kInf;
    const float inner_h =
        AxisFinite(available.h) ? std::max(0.0f, available.h - padding_v_ * 2.0f) : kInf;
    float max_w = 0.0f;
    float max_h = 0.0f;
    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        const Size d = MeasureChildAt(i, {inner_w, inner_h}, theme);
        max_w = std::max(max_w, d.w);
        max_h = std::max(max_h, d.h);
    }
    return {max_w + padding_h_ * 2.0f, max_h + padding_v_ * 2.0f};
}

void ZStack::Arrange(const Rect& absolute) {
    absolute_ = absolute;
    const float inner_w = std::max(0.0f, absolute.w - padding_h_ * 2.0f);
    const float inner_h = std::max(0.0f, absolute.h - padding_v_ * 2.0f);
    auto extent = [](Align align, float inner, float desired, bool stretch) {
        if (stretch || align == Align::Stretch) return inner;
        return std::min(desired, inner);
    };
    auto origin = [](Align align, float inner, float size) {
        if (align == Align::End) return std::max(0.0f, inner - size);
        if (align == Align::Start || align == Align::Stretch) return 0.0f;
        return std::max(0.0f, (inner - size) * 0.5f);
    };
    for (size_t i = 0; i < children_.size(); ++i) {
        if (!ChildVisible(i)) continue;
        const Size& d = ChildDesired(i);
        const bool fill = Child(i).FillsCross() || Child(i).GrowWeight() > 0.0f;
        const float w = extent(align_h_, inner_w, d.w, fill);
        const float h = extent(align_v_, inner_h, d.h, fill);
        const float x = padding_h_ + origin(align_h_, inner_w, w);
        const float y = padding_v_ + origin(align_v_, inner_h, h);
        SetChildBounds(Child(i), {x, y, w, h});
    }
    for (size_t i = 0; i < children_.size(); ++i) {
        if (ChildVisible(i)) ArrangeChildAt(i);
    }
}

} // namespace lumen
