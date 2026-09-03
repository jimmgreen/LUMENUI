// lumen/TreeView.h — 虚拟化层级树：数据回调式，展平可见行缓存在输入路径重建，绘制零分配。
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

class TreeView : public ControlOf<TreeView> {
public:
    static constexpr size_t kNone = static_cast<size_t>(-1);

    // 根节点数量。数据结构变化（增删节点）后重新调用即可重建视图。
    void Roots(size_t count);
    size_t Roots() const noexcept { return root_count_; }

    // 数据源回调：id 为调用方稳定索引。
    TreeView& ChildCount(std::function<size_t(size_t id)> provider) {
        child_count_ = std::move(provider);
        RebuildVisible();
        return *this;
    }
    TreeView& ChildAt(std::function<size_t(size_t id, size_t index)> provider) {
        child_at_ = std::move(provider);
        RebuildVisible();
        return *this;
    }
    // 数据提供回调：写入文本/字形（复用控件缓冲的容量，绘制路径零分配）。
    TreeView& ItemText(std::function<void(size_t id, std::wstring&)> provider) {
        item_text_ = std::move(provider);
        Invalidate();
        return *this;
    }
    TreeView& ItemGlyph(std::function<void(size_t id, std::wstring&)> provider) {
        item_glyph_ = std::move(provider);
        Invalidate();
        return *this;
    }

    // 无子节点的 id 恒为折叠态。展开状态变化触发 OnExpandedChanged 并重建可见缓存。
    bool Expanded(size_t id) const { return expanded_.count(id) != 0; }
    TreeView& Expand(size_t id, bool on = true);
    TreeView& Collapse(size_t id) { return Expand(id, false); }
    TreeView& ToggleExpanded(size_t id) { return Expand(id, !Expanded(id)); }
    TreeView& CollapseAll();
    // 全部展开（一次 DFS；大数据量请按需展开）。
    TreeView& ExpandAll();
    // 数据增删后同步视图：重建展平缓存并钳制选中/滚动。
    TreeView& Refresh();

    // 平铺数据入口：parents[i] 为第 i 个节点的父 id（kNone = 根），数组顺序即兄弟顺序。
    // 一维数组 + 父引用的现成数据无需手写 ChildCount/ChildAt 回调；内部持索引。
    TreeView& SetFlatData(const std::vector<size_t>& parents);

    size_t SelectedId() const noexcept;
    TreeView& SelectedId(size_t id);   // 自动滚动到可见（不自动展开）
    // 展开祖先链并滚到该节点。
    TreeView& RevealId(size_t id);
    void ScrollToId(size_t id);

    TreeView& OnSelectionChanged(std::function<void(size_t id)> handler) {
        selection_changed_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindSelectionChanged(std::function<void(size_t id)> handler) {
        return selection_changed_.Connect(std::move(handler));
    }
    TreeView& OnActivate(std::function<void(size_t id)> handler) {
        activate_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindActivate(std::function<void(size_t id)> handler) {
        return activate_.Connect(std::move(handler));
    }
    TreeView& OnExpandedChanged(std::function<void(size_t id, bool expanded)> handler) {
        expanded_changed_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindExpandedChanged(std::function<void(size_t id, bool expanded)> handler) {
        return expanded_changed_.Connect(std::move(handler));
    }
    TreeView& Bind(TreeModel& model);
    TreeView& Bind(std::shared_ptr<TreeModel> model);

    // 当前展平可见行数（测试/诊断用）。
    size_t VisibleCount() const noexcept { return visible_.size(); }
    size_t VisibleIdAt(size_t row) const { return visible_[row]; }

protected:
    friend class WindowImpl;
    Size Measure(Size, const Theme&) override;
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
    void RebuildVisible();
    void ApplyTreeModel();
    void ClampSelection(size_t keep_id, size_t keep_parent);
    void ClampScroll();
    float MaxScroll() const;
    ptrdiff_t RowAt(Point local) const;                 // 可见行下标，-1 无
    int DepthAt(ptrdiff_t row) const { return depth_[static_cast<size_t>(row)]; }
    bool HasChildren(size_t id) const;
    Rect VerticalTrack() const noexcept;
    ScrollThumb Thumb(float expand) const noexcept;
    bool BeginScrollDrag(Point local);
    void MoveSelection(ptrdiff_t delta);
    void SelectRow(ptrdiff_t row);                      // 可见行下标
    void ScrollRowIntoView(ptrdiff_t row);
    ptrdiff_t RowOfId(size_t id) const;                 // 线性查可见缓存

    size_t root_count_ = 0;
    std::vector<size_t> root_ids_;   // 实际根 id（SetFlatData 不必 0..count-1）
    std::unordered_set<size_t> expanded_;
    std::vector<std::vector<size_t>> flat_children_;   // SetFlatData 的索引（回调按引用捕获）
    std::vector<size_t> visible_;   // 展平的可见 id（绘制路径只读）
    std::vector<int> depth_;        // 与 visible_ 平行的层级（根 = 0）
    std::vector<size_t> visible_parent_;   // 与 visible_ 平行的父 id（kNone = 根）
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
    std::wstring draw_text_;    // 绘制期复用（容量跨帧保留，零堆）
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
