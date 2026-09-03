// lumen/TreeTable.h — 树形展开 + 多列（TreeView 与 Table 的交叉）。第一列是树。
// Events: OnSelectionChanged / BindSelectionChanged / OnActivate / BindActivate / OnExpandedChanged / BindExpandedChanged
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ControlOf.h"
#include "ItemsModel.h"
#include "Signal.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <vector>

namespace lumen {

class TreeTable : public ControlOf<TreeTable> {
public:
    static constexpr size_t kNone = static_cast<size_t>(-1);

    TreeTable& AddColumn(std::wstring_view title, float width = 0.0f);
    size_t ColumnCount() const noexcept { return columns_.size(); }

    void Roots(size_t count);
    size_t Roots() const noexcept { return root_count_; }

    TreeTable& ChildCount(std::function<size_t(size_t id)> provider) {
        child_count_ = std::move(provider);
        RebuildVisible();
        return *this;
    }
    TreeTable& ChildAt(std::function<size_t(size_t id, size_t index)> provider) {
        child_at_ = std::move(provider);
        RebuildVisible();
        return *this;
    }
    TreeTable& ItemText(std::function<void(size_t id, std::wstring&)> provider) {
        item_text_ = std::move(provider);
        Invalidate();
        return *this;
    }
    TreeTable& ItemGlyph(std::function<void(size_t id, std::wstring&)> provider) {
        item_glyph_ = std::move(provider);
        Invalidate();
        return *this;
    }
    // 额外列（col 0 若未设 ItemText 也走这里）。绘制路径写进复用缓冲。
    TreeTable& CellText(std::function<void(size_t id, size_t col, std::wstring&)> provider) {
        cell_text_ = std::move(provider);
        Invalidate();
        return *this;
    }

    bool Expanded(size_t id) const { return expanded_.count(id) != 0; }
    TreeTable& Expand(size_t id, bool on = true);
    TreeTable& Collapse(size_t id) { return Expand(id, false); }
    TreeTable& ToggleExpanded(size_t id) { return Expand(id, !Expanded(id)); }
    TreeTable& CollapseAll();
    TreeTable& ExpandAll();
    TreeTable& Refresh();
    TreeTable& SetFlatData(const std::vector<size_t>& parents);

    size_t SelectedId() const noexcept;
    TreeTable& SelectedId(size_t id);
    TreeTable& RevealId(size_t id);
    void ScrollToId(size_t id);

    TreeTable& OnSelectionChanged(std::function<void(size_t id)> handler) {
        selection_changed_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindSelectionChanged(std::function<void(size_t id)> handler) {
        return selection_changed_.Connect(std::move(handler));
    }
    TreeTable& OnActivate(std::function<void(size_t id)> handler) {
        activate_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindActivate(std::function<void(size_t id)> handler) {
        return activate_.Connect(std::move(handler));
    }
    TreeTable& OnExpandedChanged(std::function<void(size_t id, bool expanded)> handler) {
        expanded_changed_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindExpandedChanged(std::function<void(size_t id, bool expanded)> handler) {
        return expanded_changed_.Connect(std::move(handler));
    }
    TreeTable& Bind(TreeModel& model);
    TreeTable& Bind(std::shared_ptr<TreeModel> model);

    size_t VisibleCount() const noexcept { return visible_.size(); }
    size_t VisibleIdAt(size_t row) const { return visible_[row]; }

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool Focusable() const noexcept override { return true; }
    bool OnKey(uint32_t vk) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseDoubleClick(Point local) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseUp(Point local, uint32_t buttons) override;
    void OnMouseLeave() override;
    bool OnWheel(float delta) override;
    bool CapturesOverlay(Point p) const override;
    void OnFocusChanged(bool focused) override;
    bool OnAnimate(float dt_seconds) override;

private:
    static constexpr size_t kMaxColumns = 8;
    static constexpr float kHeaderH = 32.0f;
    static constexpr float kIndent = 16.0f;
    static constexpr float kExpanderZone = 16.0f;
    static constexpr float kBarHit = 10.0f;
    static constexpr float kCellPadX = 10.0f;
    static constexpr float kMinFlex = 80.0f;

    struct Column {
        std::wstring title;
        float width = 0.0f;
    };

    void RebuildVisible();
    void ApplyTreeModel();
    void ClampSelection(size_t keep_id, size_t keep_parent);
    void ClampScroll();
    float MaxScroll() const;
    float HeaderHeight() const noexcept;
    float BodyHeight() const noexcept;
    float BodyTop() const noexcept { return HeaderHeight(); }
    ptrdiff_t RowAt(Point local) const;
    int DepthAt(ptrdiff_t row) const { return depth_[static_cast<size_t>(row)]; }
    bool HasChildren(size_t id) const;
    Rect VerticalTrack() const noexcept;
    ScrollThumb Thumb(float expand) const noexcept;
    bool BeginScrollDrag(Point local);
    void MoveSelection(ptrdiff_t delta);
    void SelectRow(ptrdiff_t row);
    void ScrollRowIntoView(ptrdiff_t row);
    ptrdiff_t RowOfId(size_t id) const;
    void ColumnMetrics(float inner_w, float* xs, float* ws, size_t cap) const;

    size_t root_count_ = 0;
    std::vector<size_t> root_ids_;
    std::unordered_set<size_t> expanded_;
    std::vector<std::vector<size_t>> flat_children_;
    std::vector<size_t> visible_;
    std::vector<int> depth_;
    std::vector<size_t> visible_parent_;
    std::vector<Column> columns_;
    ptrdiff_t selected_row_ = -1;

    float scroll_offset_ = 0.0f;
    float target_offset_ = 0.0f;
    float theme_row_height_ = 28.0f;
    float expand_progress_ = 0.0f;
    ptrdiff_t hover_row_ = -1;
    bool dragging_ = false;
    float drag_grab_ = 0.0f;

    std::function<size_t(size_t)> child_count_;
    std::function<size_t(size_t, size_t)> child_at_;
    std::function<void(size_t, std::wstring&)> item_text_;
    std::function<void(size_t, std::wstring&)> item_glyph_;
    std::function<void(size_t, size_t, std::wstring&)> cell_text_;
    std::wstring draw_text_;
    std::wstring draw_glyph_;
    Signal<size_t> selection_changed_;
    Signal<size_t> activate_;
    Signal<size_t, bool> expanded_changed_;
    TreeModel* tree_model_ = nullptr;
    std::shared_ptr<TreeModel> owned_tree_;
    ScopedConnection tree_detached_;
    ScopedConnection tree_reset_;
};

} // namespace lumen
