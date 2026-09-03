// lumen/Table.h — 虚拟化表格：表头 + 可见行复用交互控件（CheckBox/Button/TextBox）。
// Events: OnSelectionChanged / BindSelectionChanged / OnSortChanged / BindSortChanged / OnCellEdited / BindCellEdited / OnFrozenChanged / BindFrozenChanged
// Keys: 焦点控件处理 Enter/Space/方向键等，详见 OnKey
// Layout: Grow / FillCross / Margin 走 ControlOf；默认尺寸见 Measure
#pragma once
#include "ItemsModel.h"
#include "Panel.h"
#include "Signal.h"
#include "TextBox.h"
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace lumen {

class TableColumnRef;

enum class CellKind { Text, CheckBox, Button, TextBox, Progress, Icon };
enum class ColumnAggregate { None, Count, Sum, Average, Min, Max };

struct TableSortKey {
    int col = -1;
    int direction = 1;  // 1 升 / -1 降
};

class Table : public PanelOf<Table> {
public:
    Table();
    ~Table() override;
    // width 为 DIP；0 表示弹性列（多列均分剩余）。视口不够时保底 96 DIP，超出横向滚动。
    // AddColumn(L"On", 64.0f).CheckBox(get, set).Sortable(true)；可隐式转回列下标。
    // 表头右侧图钉切换 Frozen；列边界 ±4 DIP 拖拽改宽。
    TableColumnRef AddColumn(std::wstring_view title, float width = 0.0f);

    Table& RowCount(size_t count);
    size_t RowCount() const noexcept { return row_count_; }
    // 订阅模型：CellText 走 ItemRow.text / cells；模型须活过表格。
    Table& Bind(ItemsModel& model);
    Table& Bind(std::shared_ptr<ItemsModel> model);
    template <class T>
    Table& Bind(VectorModel<T>& model) {
        Bind(static_cast<ItemsModel&>(model));
        typed_get_ = [&model](size_t i) -> void* {
            return i < model.Count() ? static_cast<void*>(&model.At(i)) : nullptr;
        };
        typed_touch_ = [&model](size_t i) {
            if (i < model.Count()) model.At(i, T(model.At(i)));
        };
        return *this;
    }
    template <class T, class M>
    Table& Column(std::wstring_view title, M T::* mem, float width = 0.0f) {
        if (!typed_get_ || !typed_touch_) {
            Control::DebugTrap(L"LUMEN_CHECK: Table::Column requires Bind(VectorModel<T>&) first");
            return *this;
        }
        const int col = AddColumn(title, width);
        if constexpr (std::is_same_v<M, std::wstring>) {
            BindTextBox(
                col,
                [this, mem](size_t i) -> std::wstring {
                    void* row = typed_get_ ? typed_get_(i) : nullptr;
                    return row ? static_cast<T*>(row)->*mem : std::wstring{};
                },
                [this, mem](size_t i, std::wstring v) {
                    void* row = typed_get_ ? typed_get_(i) : nullptr;
                    if (!row) return;
                    static_cast<T*>(row)->*mem = std::move(v);
                    if (typed_touch_) typed_touch_(i);
                });
        } else if constexpr (std::is_same_v<M, bool>) {
            BindCheckBox(
                col,
                [this, mem](size_t i) {
                    void* row = typed_get_ ? typed_get_(i) : nullptr;
                    return row ? static_cast<T*>(row)->*mem : false;
                },
                [this, mem](size_t i, bool v) {
                    void* row = typed_get_ ? typed_get_(i) : nullptr;
                    if (!row) return;
                    static_cast<T*>(row)->*mem = v;
                    if (typed_touch_) typed_touch_(i);
                });
        } else if constexpr (std::is_same_v<M, float> || std::is_same_v<M, double>) {
            BindProgress(col, [this, mem](size_t i) {
                void* row = typed_get_ ? typed_get_(i) : nullptr;
                return row ? static_cast<float>(static_cast<T*>(row)->*mem) : 0.0f;
            });
        } else {
            Control::DebugTrap(L"LUMEN_CHECK: Table::Column unsupported member type");
        }
        return *this;
    }

    // 仅服务 CellKind::Text。写入 out（复用调用方缓冲，绘制路径零分配）。
    Table& CellText(std::function<void(size_t row, size_t col, std::wstring& out)> provider) {
        cell_text_ = std::move(provider);
        Invalidate();
        return *this;
    }

    Table& ColumnKind(int col, CellKind kind);
    Table& BindCheckBox(int col, std::function<bool(size_t)> get,
                        std::function<void(size_t, bool)> set);
    Table& BindButton(int col, std::wstring caption,
                      std::function<void(size_t)> on_click);
    Table& BindTextBox(int col, std::function<std::wstring(size_t)> get,
                       std::function<void(size_t, std::wstring)> set);
    Table& BindProgress(int col, std::function<float(size_t)> get);
    Table& BindIcon(int col, std::function<void(size_t, std::wstring&)> get);
    Table& RowHeight(float dip);

    // 视图行下标（排序/展开后的显示位置）。要拿数据行请用 SelectedDataIndex()。
    ptrdiff_t SelectedIndex() const noexcept { return selected_; }
    // 数据行下标：排序后回调里查自家数据数组用它，避免视图/数据错位。
    ptrdiff_t SelectedDataIndex() const noexcept {
        return selected_ >= 0
                   ? static_cast<ptrdiff_t>(DataRowAt(static_cast<size_t>(selected_)))
                   : -1;
    }
    Table& SelectedIndex(ptrdiff_t index);
    void ScrollTo(size_t index);
    Table& OnSelectionChanged(std::function<void(ptrdiff_t, ptrdiff_t)> handler) {
        selection_changed_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindSelectionChanged(std::function<void(ptrdiff_t, ptrdiff_t)> handler) {
        return selection_changed_.Connect(std::move(handler));
    }

    // ---- 基础排序 ----
    // 表头点击循环 无 → 升 → 降 → 无；换列直接按新列升序。
    // Shift+点击把该列追加为次键（最多 3 级）；行数变化会清除排序。
    Table& Sortable(int col, bool on);
    bool Sortable(int col) const noexcept;
    void SortBy(int col, int direction, bool append = false);  // direction: 1 升 / -1 降 / 0 清除
    int SortedColumn() const noexcept { return sort_col_; }
    int SortDirection() const noexcept { return sort_col_ >= 0 ? sort_dir_ : 0; }
    const std::vector<TableSortKey>& SortKeys() const noexcept { return sort_keys_; }
    // 默认按 CellText 字典序比较数据行；提供后优先（a、b 为数据行下标）。
    Table& RowComparator(std::function<bool(size_t a, size_t b, int col)> less);
    Table& OnSortChanged(std::function<void(int col, int direction)> handler) {
        sort_changed_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindSortChanged(std::function<void(int col, int direction)> handler) {
        return sort_changed_.Connect(std::move(handler));
    }

    // ---- 单元格编辑 ----
    // 开启后双击 CellKind::Text 的单元格进入行内编辑（回车/失焦提交，Esc 取消）。
    Table& CellEditEnabled(bool on = true);
    bool CellEditEnabled() const noexcept { return cell_edit_enabled_; }
    // 提交回调：data_row 已换算；文本未变化时不触发。
    Table& OnCellEdited(std::function<void(size_t data_row, int col, std::wstring text)> handler) {
        cell_edited_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindCellEdited(std::function<void(size_t data_row, int col, std::wstring text)> handler) {
        return cell_edited_.Connect(std::move(handler));
    }
    // 视图行 → 数据行。未排序时恒等；绑定回调与 CellText 收到的都是数据行，
    // 而本控件的选中/滚动 API（SelectedIndex、ScrollTo、RowAt）使用视图行。
    size_t DataRowAt(size_t view_row) const noexcept {
        return view_row < order_.size() ? order_[view_row] : view_row;
    }

    // ---- 列宽拖动 ----
    // 表头内列边界 ±4 DIP 命中即拖拽；按下时把所有弹性列固化为当前实宽，
    // 之后只改被拖的那一列，其余列像素宽不变；总宽超出视口才出横向滚动条。
    float ColumnWidth(int col) const;      // 声明宽；0 = 弹性
    void ColumnWidth(int col, float dip);
    Table& ColumnFrozen(int col, bool frozen);
    bool ColumnFrozen(int col) const noexcept;
    Table& OnFrozenChanged(std::function<void(int col, bool frozen)> handler) {
        frozen_changed_.Subscribe(std::move(handler));
        return *this;
    }
    Connection BindFrozenChanged(std::function<void(int col, bool frozen)> handler) {
        return frozen_changed_.Connect(std::move(handler));
    }
    float HorizontalOffset() const noexcept { return horizontal_offset_; }
    Table& ScrollToX(float value);

    // ---- 列显隐 / 重排 ----
    // 表头右键勾选显隐（至少留一列）；拖动表头（超过阈值）在冻结带/滚动带内重排。
    Table& ColumnVisible(int col, bool visible);
    bool ColumnVisible(int col) const noexcept;
    Table& MoveColumn(int from, int to);  // 数据列下标；视觉顺序独立，CellText 列号不变

    // ---- 键盘单元格 ----
    // 方向键走单元格；F2 编辑；Ctrl+C 复制当前行 TSV。
    int ActiveColumn() const noexcept { return active_col_; }
    Table& ActiveColumn(int col);
    bool CopySelection() const;

    // ---- 分组 + 粘性组头 ----
    // 按该列 CellText 分桶（组键优先于用户排序）；-1 清除。点击组头展开/折叠。
    Table& GroupBy(int col);
    int GroupColumn() const noexcept { return group_col_; }
    size_t GroupCount() const noexcept { return groups_.size(); }
    bool GroupExpanded(size_t group) const noexcept;
    Table& GroupExpanded(size_t group, bool expanded);

    // ---- 页脚聚合 ----
    Table& Footer(bool on);
    bool Footer() const noexcept { return footer_; }
    Table& Aggregate(int col, ColumnAggregate kind);
    ColumnAggregate ColumnAggregateKind(int col) const noexcept;

protected:
    friend class WindowImpl;
    Size Measure(Size available, const Theme& theme) override;
    void Arrange(const Rect& absolute) override;
    void Draw(Painter& painter, const Theme& theme) override;
    void DrawOverlay(Painter& painter, const Theme& theme) override;
    bool ClipChildren() const noexcept override { return true; }
    Rect ChildrenClipBounds() const noexcept override;
    bool Focusable() const noexcept override { return true; }
    AutomationControlType AutomationType() const noexcept override {
        return AutomationControlType::DataGrid;
    }
    uint32_t AutomationPatterns() const noexcept override {
        return kPatternSelection | kPatternValue;
    }
    std::wstring AutomationValue() const override {
        return AutomationItemName(static_cast<int>(selected_));
    }
    int AutomationSelectedIndex() const noexcept override { return static_cast<int>(selected_); }
    int AutomationItemCount() const noexcept override { return static_cast<int>(row_count_); }
    bool AutomationSelectIndex(int index) override {
        if (index < -1) return false;
        if (index >= 0 && static_cast<size_t>(index) >= row_count_) return false;
        SelectedIndex(index);
        return true;
    }
    std::wstring AutomationItemName(int index) const override {
        if (index < 0 || static_cast<size_t>(index) >= row_count_ || !cell_text_) return {};
        std::wstring out;
        cell_text_(DataRowAt(static_cast<size_t>(index)), 0, out);
        return out;
    }
    bool OnKey(uint32_t vk) override;
    bool ShowContextMenu(Point window_dip) override;
    void OnMouseEnter() override;
    void OnMouseDown(Point local, uint32_t buttons) override;
    void OnMouseMove(Point local, uint32_t buttons) override;
    void OnMouseUp(Point local, uint32_t buttons) override;
    void OnMouseLeave() override;
    bool OnWheel(float delta) override;
    bool CapturesOverlay(Point p) const override;
    void OnFocusChanged(bool focused) override;
    bool OnAnimate(float dt_seconds) override;

    void RelayoutParent();
    int AddColumnIndex(std::wstring_view title, float width);
    float HeaderHeight() const noexcept { return 32.0f; }
    float FooterHeight() const noexcept { return footer_ ? 28.0f : 0.0f; }
    float GroupBand() const noexcept { return 28.0f; }
    float RowHeight() const noexcept {
        return custom_row_height_ > 0.5f ? custom_row_height_ : theme_row_height_;
    }
    float BodyTop() const noexcept { return HeaderHeight(); }
    float BodyHeight() const noexcept;
    float MaxScroll() const;
    float MaxHorizontalScroll() const;
    void ClampScroll();
    void MoveSelection(ptrdiff_t delta);
    ptrdiff_t RowAt(Point local) const;
    void OnMouseDoubleClick(Point local) override;
    void CommitCellEdit();
    void CancelCellEdit();
    void HideCellEditor();
    void BeginCellEdit(ptrdiff_t row, int col);
    void ResetCellEditor();   // Clear() 重建子级后调用：编辑器指针已随销毁失效
    void ColumnMetrics(float inner_w, float* xs, float* ws, size_t cap,
                       float* frozen_width = nullptr,
                       float* scrollable_width = nullptr) const;
    void SnapFlexToPixels();
    int ColumnAt(float x) const;
    int ResizeBoundaryAt(Point local) const;
    int HeaderPinAt(Point local) const;
    Rect BodyViewport() const noexcept;
    Rect FooterRect() const noexcept;
    Rect VerticalTrack() const noexcept;
    Rect HorizontalTrack() const noexcept;
    ScrollThumb Thumb(float expand) const noexcept;
    ScrollThumb HorizontalThumb(float expand) const noexcept;
    bool BeginScrollDrag(Point local);
    void SyncSlots();
    void EnsurePool();
    uint64_t InteractiveFingerprint() const noexcept;
    size_t FillInteractive(size_t* out, size_t cap) const noexcept;
    bool IsInteractive(size_t col) const noexcept;
    size_t IndexOf(Control* ctl) const noexcept;
    CursorShape CursorAt(Point local) const override;
    void RebuildGroups();
    void ApplySort();
    int CompareCells(size_t a, size_t b, int col) const;
    float ContentHeight() const;
    float RowTop(size_t view_row) const;
    ptrdiff_t GroupAt(Point local) const;
    void EnsureColumnVisible(int col);
    void MoveActiveColumn(int delta);
    void PopupColumnMenu(Point window_dip);
    bool CellTextAt(size_t data_row, size_t col, std::wstring& out) const;
    std::wstring FooterText(size_t col) const;
    uint64_t ColumnFingerprint() const noexcept;

    struct ColumnDef {
        std::wstring title;
        float width = 0.0f;
        bool frozen = false;
        bool sortable = false;
        bool visible = true;
        CellKind kind = CellKind::Text;
        ColumnAggregate aggregate = ColumnAggregate::None;
        std::function<bool(size_t)> cb_get;
        std::function<void(size_t, bool)> cb_set;
        std::wstring btn_caption;
        std::function<void(size_t)> btn_click;
        std::function<std::wstring(size_t)> tb_get;
        std::function<void(size_t, std::wstring)> tb_set;
        std::function<float(size_t)> prog_get;
        std::function<void(size_t, std::wstring&)> icon_get;
    };

    struct Slot {
        Control* control = nullptr;
        size_t col = 0;
        ptrdiff_t row = -1;
        size_t child_index = static_cast<size_t>(-1);
    };

    std::vector<ColumnDef> columns_;
    std::vector<size_t> visual_;   // 视觉顺序 → 数据列
    std::vector<Slot> slots_;
    std::vector<size_t> order_;    // 视图行 → 数据行；空 = 恒等
    std::vector<TableSortKey> sort_keys_;
    int sort_col_ = -1;
    int sort_dir_ = 1;
    int resize_col_ = -1;
    int hover_split_ = -1;         // 表头悬停的列边界（左列下标）
    int header_press_col_ = -1;
    int drop_col_ = -1;
    int active_col_ = 0;
    int group_col_ = -1;
    float header_press_x_ = 0.0f;
    bool reorder_dragging_ = false;
    bool footer_ = false;
    struct Group {
        std::wstring key;
        size_t start = 0;   // 视图行起点
        size_t count = 0;
        bool expanded = true;
    };
    std::vector<Group> groups_;
    float resize_start_x_ = 0.0f;
    float resize_start_w_ = 0.0f;
    size_t row_count_ = 0;
    size_t pool_rows_ = 0;
    uint64_t pool_fp_ = 0;
    ptrdiff_t selected_ = -1;
    float scroll_offset_ = 0.0f;
    float target_offset_ = 0.0f;
    float horizontal_offset_ = 0.0f;
    float horizontal_target_ = 0.0f;
    float theme_row_height_ = 28.0f;
    float custom_row_height_ = 0.0f;
    float expand_progress_ = 0.0f;
    ptrdiff_t hover_row_ = -1;
    bool dragging_ = false;
    bool horizontal_dragging_ = false;
    float drag_grab_ = 0.0f;
    // 行内编辑器：回车/失焦提交，Esc 取消。
    class CellEditor : public TextBox {
    public:
        std::function<void()> committed;
        std::function<void()> cancelled;
        std::function<void()> focus_lost;
        // 外层 Table 不是嵌套类的友元，聚焦入口在此暴露。
        void FocusCaret() { Focus(); }

    protected:
        bool OnKey(uint32_t vk) override {
            if (vk == 0x0D) {           // VK_RETURN（公共头不引 windows.h）
                if (committed) committed();
                return true;
            }
            if (vk == 0x1B) {           // VK_ESCAPE
                if (cancelled) cancelled();
                return true;
            }
            return TextBox::OnKey(vk);
        }
        void OnFocusChanged(bool focused) override {
            TextBox::OnFocusChanged(focused);
            if (!focused && focus_lost) focus_lost();
        }
    };
    CellEditor* cell_editor_ = nullptr;
    ptrdiff_t edit_row_ = -1;
    int edit_col_ = -1;
    bool cell_edit_enabled_ = false;
    Signal<size_t, int, std::wstring> cell_edited_;
    std::function<void(size_t, size_t, std::wstring&)> cell_text_;
    ItemsModel* model_ = nullptr;
    std::function<void*(size_t)> typed_get_;
    std::function<void(size_t)> typed_touch_;
    ScopedConnection model_inserted_;
    ScopedConnection model_removed_;
    ScopedConnection model_changed_;
    ScopedConnection model_reset_;
    ScopedConnection model_detached_;
    std::shared_ptr<ItemsModel> owned_model_;
    mutable ItemRow model_row_;
    std::wstring draw_text_;    // 绘制期复用（容量跨帧保留，零堆）
    std::function<bool(size_t, size_t, int)> row_less_;
    Signal<int, int> sort_changed_;
    Signal<int, bool> frozen_changed_;
    Signal<ptrdiff_t, ptrdiff_t> selection_changed_;
    struct DrawCache;
    std::unique_ptr<DrawCache> draw_cache_;
};

// AddColumn 的流式代理：table.AddColumn(L"On", 64.0f).CheckBox(get, set).Sortable(true)。
// 可隐式转回列下标（int），既有按下标配置的 API 不受影响。
class TableColumnRef {
public:
    TableColumnRef(Table* table, int index) noexcept : table_(table), index_(index) {}

    operator int() const noexcept { return index_; }
    int Index() const noexcept { return index_; }

    TableColumnRef& Width(float dip) {
        table_->ColumnWidth(index_, dip);
        return *this;
    }
    TableColumnRef& Kind(CellKind kind) {
        table_->ColumnKind(index_, kind);
        return *this;
    }
    TableColumnRef& CheckBox(std::function<bool(size_t)> get,
                             std::function<void(size_t, bool)> set) {
        table_->BindCheckBox(index_, std::move(get), std::move(set));
        return *this;
    }
    TableColumnRef& Button(std::wstring caption, std::function<void(size_t)> on_click) {
        table_->BindButton(index_, std::move(caption), std::move(on_click));
        return *this;
    }
    TableColumnRef& TextBox(std::function<std::wstring(size_t)> get,
                            std::function<void(size_t, std::wstring)> set) {
        table_->BindTextBox(index_, std::move(get), std::move(set));
        return *this;
    }
    TableColumnRef& Progress(std::function<float(size_t)> get) {
        table_->BindProgress(index_, std::move(get));
        return *this;
    }
    TableColumnRef& Icon(std::function<void(size_t, std::wstring&)> get) {
        table_->BindIcon(index_, std::move(get));
        return *this;
    }
    TableColumnRef& Sortable(bool on = true) {
        table_->Sortable(index_, on);
        return *this;
    }
    TableColumnRef& Frozen(bool on = true) {
        table_->ColumnFrozen(index_, on);
        return *this;
    }
    TableColumnRef& Visible(bool on = true) {
        table_->ColumnVisible(index_, on);
        return *this;
    }
    TableColumnRef& Aggregate(ColumnAggregate kind) {
        table_->Aggregate(index_, kind);
        return *this;
    }

private:
    Table* table_;
    int index_;
};

} // namespace lumen
