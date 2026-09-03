// lumen/ListView.h — 虚拟化列表：行按需绘制，十万行也只画可见行。
// Events: OnGroupExpandedChanged / BindGroupExpandedChanged / OnSelectionChanged / BindSelectionChanged / OnActivate / BindActivate / OnReordered / BindReordered
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "Animate.h"
#include "ItemsModel.h"
#include "Panel.h"
#include "Signal.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace lumen {

class EmptyState;

struct ListGroup {
    std::wstring id;
    std::wstring title;
    size_t item_count = 0;
    bool expanded = true;
};

// 水平滑出的行操作。Leading = 右滑露出；Trailing = 左滑露出。
// invoke 收到的是视图行下标；读数据请用 DataIndex(view)。
struct ListSwipeAction {
    std::wstring label;
    std::wstring glyph;
    std::function<void(size_t view)> invoke;
};

class ListView : public PanelOf<ListView> {
public:
    ListView();
    ~ListView() override;
    ListView& ItemCount(size_t count, bool play_enter = true);
    size_t ItemCount() const noexcept { return item_count_; }
    // 订阅模型变更：插入（单条）走行高补间；删除/Reset 同步 Count。模型须活过列表。
    ListView& Bind(ItemsModel& model);
    ListView& Bind(std::shared_ptr<ItemsModel> model);
    ListView& Groups(std::vector<ListGroup> groups);
    const std::vector<ListGroup>& Groups() const noexcept { return groups_; }
    ListView& GroupExpanded(std::wstring_view id, bool expanded);
    bool GroupExpanded(std::wstring_view id) const;
    ListView& OnGroupExpandedChanged(std::function<void(std::wstring_view, bool)> handler) {
        group_expanded_changed_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindGroupExpandedChanged(std::function<void(std::wstring_view, bool)> handler) {
        return group_expanded_changed_.Connect(std::move(handler));
    }

    // 数据提供回调：写入行文本/字形（复用控件缓冲的容量，绘制路径零分配）。
    // 下标为数据行（重排后视图行请用 DataIndex）。行不存在则写空。
    ListView& ItemText(std::function<void(size_t, std::wstring&)> provider) {
        item_text_ = std::move(provider);
        return *this;
    }
    ListView& ItemGlyph(std::function<void(size_t, std::wstring&)> provider) {
        item_glyph_ = std::move(provider);
        return *this;
    }

    // 单选模式（默认）下 SelectedIndex 即选中行；多选模式下它是焦点行
    // （点击/键盘最后到达的行），Shift 范围以 keyboard_anchor_ 为锚。
    ptrdiff_t SelectedIndex() const noexcept { return selected_; }
    ptrdiff_t SelectedDataIndex() const noexcept {
        return selected_ >= 0 ? static_cast<ptrdiff_t>(DataIndex(static_cast<size_t>(selected_)))
                              : ptrdiff_t{-1};
    }
    ListView& SelectedIndex(ptrdiff_t index);   // -1 = 无选中；自动滚动到可见

    // 视图行 → 数据行。未重排时恒等。
    size_t DataIndex(size_t view) const noexcept {
        return view < order_.size() ? order_[view] : view;
    }

    // 多选：Ctrl+点击切换、Shift+点击/Shift+方向键取范围、Ctrl+A 全选、Esc 清空。
    ListView& MultiSelect(bool on) {
        multi_select_ = on;
        selected_set_.clear();
        Invalidate();
        return *this;
    }
    bool MultiSelect() const noexcept { return multi_select_; }
    bool IsSelected(size_t index) const;
    std::vector<size_t> SelectedIndices() const;          // 升序
    void SelectedIndices(std::vector<ptrdiff_t> indices);  // 整体替换（越界/重复剔除）
    void ClearSelection();
    size_t SelectionCount() const noexcept {
        return multi_select_ ? selected_set_.size() : (selected_ >= 0 ? size_t{1} : size_t{0});
    }

    void ScrollTo(size_t index);

    ListView& OnSelectionChanged(std::function<void(ptrdiff_t, ptrdiff_t)> handler) {
        selection_changed_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindSelectionChanged(std::function<void(ptrdiff_t, ptrdiff_t)> handler) {
        return selection_changed_.Connect(std::move(handler));
    }
    ListView& OnActivate(std::function<void(size_t)> handler) {
        activate_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindActivate(std::function<void(size_t)> handler) {
        return activate_.Connect(std::move(handler));
    }

    // 拖动重排：松手落点插入。分组时只允许组内互换。
    ListView& CanReorder(bool on) {
        can_reorder_ = on;
        return *this;
    }
    bool CanReorder() const noexcept { return can_reorder_; }
    ListView& MoveItem(size_t from, size_t to);
    ListView& OnReordered(std::function<void(size_t from, size_t to)> handler) {
        reordered_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindReordered(std::function<void(size_t from, size_t to)> handler) {
        return reordered_.Connect(std::move(handler));
    }

    ListView& SwipeLeading(ListSwipeAction action) {
        swipe_leading_ = std::move(action);
        return *this;
    }
    ListView& SwipeTrailing(ListSwipeAction action) {
        swipe_trailing_ = std::move(action);
        return *this;
    }

    // 空列表时内嵌 EmptyState。未配置则只画空底。
    ListView& EmptyTitle(std::wstring_view value);
    ListView& EmptyHint(std::wstring_view value);
    ListView& EmptyGlyph(std::wstring_view value);
    ListView& EmptyAction(std::wstring_view label, std::function<void()> on_click);

    // 调用方先改 count 再 AnimateInserted；AnimateRemoved 播完再在 done 里改 count。
    ListView& AnimateInserted(size_t index);
    ListView& AnimateRemoved(size_t index, std::function<void()> done = {});

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Arrange(const Rect& absolute) override;
    void Draw(Painter& painter, const Theme& theme) override;
    bool Focusable() const noexcept override { return true; }
    AutomationControlType AutomationType() const noexcept override {
        return AutomationControlType::List;
    }
    uint32_t AutomationPatterns() const noexcept override {
        return kPatternSelection | kPatternValue;
    }
    bool AutomationCanSelectMultiple() const noexcept override { return multi_select_; }
    std::wstring AutomationValue() const override {
        return AutomationItemName(static_cast<int>(selected_));
    }
    int AutomationSelectedIndex() const noexcept override { return static_cast<int>(selected_); }
    int AutomationItemCount() const noexcept override { return static_cast<int>(item_count_); }
    bool AutomationSelectIndex(int index) override {
        if (index < -1) return false;
        if (index >= 0 && static_cast<size_t>(index) >= item_count_) return false;
        SelectedIndex(index);
        return true;
    }
    std::wstring AutomationItemName(int index) const override {
        if (index < 0 || static_cast<size_t>(index) >= item_count_ || !item_text_) return {};
        std::wstring out;
        item_text_(DataIndex(static_cast<size_t>(index)), out);
        return out;
    }
    bool OnKey(uint32_t vk) override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseDoubleClick(Point local) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseUp(Point local, uint32_t buttons) override;
    void OnMouseLeave() override;
    bool OnWheel(float delta) override;
    bool CapturesOverlay(Point p) const override;
    bool CanPan() const noexcept override;
    void PanBy(float dx, float dy) override;
    void PanFling(float vx, float vy) override;
    bool PrefersDragOverPan() const noexcept override;
    CursorShape CursorAt(Point local) const override;
    void OnFocusChanged(bool focused) override;
    bool OnAnimate(float dt_seconds) override;

    void RelayoutParent();
    void BeginEnter();
    float ItemEnter(size_t visible_index) const noexcept;
    float RowHeight(const Theme& theme) const noexcept { return theme.list_row_height; }
    ptrdiff_t RowAt(Point local) const;
    void ClampScroll();
    void MoveSelection(ptrdiff_t delta);
    void SelectRangeTo(ptrdiff_t row);   // 以 anchor 为起点重画选区到 row
    void ToggleSelected(ptrdiff_t row);
    void SelectJump(ptrdiff_t index, bool to_end);
    void FilterSelection();              // 行数收缩后剔除越界选中
    void RebuildGroups();
    float ContentHeight() const;
    ptrdiff_t GroupAt(Point local) const;
    ptrdiff_t GroupForItem(ptrdiff_t item) const;
    float ItemTop(size_t index) const;
    float RowExtent(size_t index) const;
    void ToggleGroup(size_t group);
    void MoveGroupedFocus(int direction);
    float MaxScroll() const;
    Rect VerticalTrack() const noexcept;
    ScrollThumb Thumb(float expand) const noexcept;
    bool BeginScrollDrag(Point local);
    void EnsureOrder();
    void RemapSelection(size_t from, size_t to);
    EmptyState& EnsureEmpty();
    void SyncEmpty();
    bool HasSwipe() const noexcept;
    float SwipeActionWidth(const ListSwipeAction& action) const;
    void BeginPress(Point local, ptrdiff_t row);
    void ResetPress();
    void ApplySwipeX(float x);
    void EndSwipe();
    void EndReorder();
    void FinishMut();
    bool LivePaint() const noexcept;

    size_t item_count_ = 0;
    std::vector<ListGroup> groups_;
    std::vector<size_t> group_offsets_;
    std::vector<size_t> order_;            // 视图行 → 数据行；空 = 恒等
    ptrdiff_t selected_ = -1;
    std::vector<ptrdiff_t> selected_set_;  // 升序；仅多选模式使用（绘制路径只读，零分配）
    bool multi_select_ = false;
    float scroll_offset_ = 0.0f;     // 当前滚动（DIP，平滑插值）
    float target_offset_ = 0.0f;     // 目标滚动
    float theme_row_height_ = 28.0f;
    float expand_progress_ = 0.0f;   // 滚动条悬停展开 2.5 → 5px
    ptrdiff_t hover_row_ = -1;
    bool dragging_ = false;
    float drag_grab_ = 0.0f;
    ptrdiff_t keyboard_anchor_ = -1; // Shift 选区起点
    ptrdiff_t focus_group_ = -1;
    ptrdiff_t hover_group_ = -1;
    std::function<void(size_t, std::wstring&)> item_text_;
    std::function<void(size_t, std::wstring&)> item_glyph_;
    ItemsModel* model_ = nullptr;
    std::shared_ptr<ItemsModel> owned_model_;
    ScopedConnection model_inserted_;
    ScopedConnection model_removed_;
    ScopedConnection model_changed_;
    ScopedConnection model_reset_;
    ScopedConnection model_detached_;
    mutable ItemRow model_row_;
    mutable ptrdiff_t model_cache_ = -1;
    std::wstring draw_text_;    // 绘制期复用（容量跨帧保留，零堆）
    std::wstring draw_glyph_;
    Signal<ptrdiff_t, ptrdiff_t> selection_changed_;
    Signal<size_t> activate_;
    Signal<std::wstring_view, bool> group_expanded_changed_;
    Signal<size_t, size_t> reordered_;
    bool enter_playing_ = false;
    float enter_elapsed_ = 10.0f;
    bool can_reorder_ = false;
    ListSwipeAction swipe_leading_;
    ListSwipeAction swipe_trailing_;
    EmptyState* empty_ = nullptr;
    Point press_local_{};
    ptrdiff_t press_row_ = -1;
    bool press_armed_ = false;
    bool swipe_dragging_ = false;
    bool reorder_dragging_ = false;
    bool pan_vertical_ = false;
    ptrdiff_t swipe_row_ = -1;
    float swipe_x_ = 0.0f;
    ptrdiff_t drop_row_ = -1;
    enum class MutKind : uint8_t { None, Insert, Remove };
    MutKind mut_kind_ = MutKind::None;
    ptrdiff_t mut_index_ = -1;
    Tween mut_tween_{};
    std::function<void()> mut_done_;
    struct DrawCache;
    std::unique_ptr<DrawCache> draw_cache_;
};

} // namespace lumen
