#include "lumen/Table.h"
#include "lumen/Button.h"
#include "lumen/CheckBox.h"
#include "lumen/Clipboard.h"
#include "lumen/Painter.h"
#include "lumen/TextBox.h"
#include "lumen/Icons.h"
#include "../core/com_ptr.h"
#include "../core/window_impl.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <cwctype>
#include <cwchar>
#include <memory>
#include <set>
#include <windows.h>
#include <d2d1_3.h>

namespace lumen {
namespace { constexpr size_t kMaxColumns = 16; }
int Table::AutomationColumnCount() const noexcept {
    return static_cast<int>(std::count_if(columns_.begin(), columns_.end(),
        [](const auto& column) { return column.visible; }));
}

Rect Table::AutomationCellBounds(int row, int column) const {
    const int index = AutomationColumnAt(column);
    if (index < 0 || row < 0 || static_cast<size_t>(row) >= row_count_) return {};
    const float top = RowTop(static_cast<size_t>(row));
    if (top < 0.0f) return {};
    float xs[kMaxColumns]{}, ws[kMaxColumns]{};
    float frozen = 0.0f;
    ColumnMetrics(absolute_.w, xs, ws, columns_.size(), &frozen, nullptr);
    const float left = std::max(xs[index], columns_[static_cast<size_t>(index)].frozen ? 0.0f : frozen);
    const float right = std::min(xs[index] + ws[index], absolute_.w - ScrollbarOccupancy().w);
    const float y = std::max(BodyTop(), BodyTop() + top - scroll_offset_);
    const float bottom = std::min(BodyTop() + BodyHeight(), BodyTop() + top - scroll_offset_ + RowHeight());
    if (right <= left || bottom <= y) return {};
    return {absolute_.x + left, absolute_.y + y, right - left, bottom - y};
}

int Table::AutomationColumnAt(int column) const noexcept {
    if (column < 0) return -1;
    for (size_t i = 0; i < columns_.size(); ++i) {
        const size_t index = visual_.size() == columns_.size() ? visual_[i] : i;
        if (columns_[index].visible && column-- == 0) return static_cast<int>(index);
    }
    return -1;
}

std::wstring Table::AutomationCellValue(int row, int column) const {
    const int index = AutomationColumnAt(column);
    std::wstring value;
    if (row >= 0 && static_cast<size_t>(row) < row_count_ && index >= 0)
        CellTextAt(DataRowAt(static_cast<size_t>(row)), static_cast<size_t>(index), value);
    return value;
}

std::wstring Table::AutomationCellName(int row, int column) const {
    const int index = AutomationColumnAt(column);
    if (index < 0) return {};
    return columns_[static_cast<size_t>(index)].title + L": " + AutomationCellValue(row, column);
}

bool Table::AutomationCellReadOnly(int row, int column) const {
    const int index = AutomationColumnAt(column);
    return row < 0 || static_cast<size_t>(row) >= row_count_ || index < 0 ||
        !CellEditableAt(row, index);
}

bool Table::AutomationSetCellValue(int row, int column, std::wstring_view value) {
    if (AutomationCellReadOnly(row, column)) return false;
    return CommitCell(DataRowAt(static_cast<size_t>(row)), AutomationColumnAt(column), value);
}

namespace {
constexpr float kCellPadX = 12.0f;
constexpr float kBarHit = 10.0f;
constexpr float kHorizontalWheel = 72.0f;
constexpr float kCellInsetY = 4.0f; // vertical inset inside row for interactive controls
constexpr float kResizeHit = 4.0f;  // 列边界命中半宽（DIP）
constexpr float kMinColWidth = 40.0f;
constexpr float kMinFlexWidth = 96.0f;  // 弹性列在视口不够时保底，超出走横向滚动
constexpr float kPinSlot = 20.0f;
constexpr float kReorderSlop = 8.0f;
constexpr int kMaxSortKeys = 3;

bool SlotMatchesKind(Control* ctl, CellKind kind) {
    if (!ctl) return false;
    switch (kind) {
    case CellKind::CheckBox:
        return dynamic_cast<CheckBox*>(ctl) != nullptr;
    case CellKind::Button:
        return dynamic_cast<Button*>(ctl) != nullptr;
    case CellKind::TextBox:
        return dynamic_cast<TextBox*>(ctl) != nullptr;
    default:
        return false;
    }
}
}  // namespace

struct Table::DrawCache {
    struct Segment { size_t begin = 0, length = 0; float advance = 0; bool custom = false; };
    struct Cell {
        ptrdiff_t row = -1;
        size_t col = 0;
        std::wstring text;
        std::vector<Segment> segments;
        float width = 0, progress = 0;
        bool flow = false;
    };
    std::vector<Cell> cells;
    size_t cell_count = 0;
    bool footer_dirty = true, footer_full = true;
    size_t footer_begin = 0, footer_end = 0;
    struct AggregateValue { double number = 0; bool numeric = false; std::wstring text; };
    struct AggregateState {
        ColumnAggregate kind = ColumnAggregate::None;
        std::vector<AggregateValue> values;
        double sum = 0;
        size_t count = 0;
        std::multiset<double> numbers;
        std::multiset<std::wstring> strings;
    };
    std::vector<AggregateState> aggregates;
    std::vector<std::wstring> footers;
    ComPtr<ID2D1CommandList> pending;
    ComPtr<ID2D1CommandList> list;
    void* device = nullptr;
    void* pending_device = nullptr;
    uint64_t theme_stamp = 0;
    float scroll = 0.0f;
    float hscroll = 0.0f;
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    ptrdiff_t selected = -1;
    ptrdiff_t hover = -1;
    int hover_split = -1;
    int sort_col = -1;
    int sort_dir = 1;
    int active_col = 0;
    int drop_col = -1;
    int group_col = -1;
    bool focused = false;
    bool footer = false;
    size_t rows = 0;
    uint64_t cols = 0;
};

Table::Table() = default;
Table::~Table() = default;

void Table::RelayoutParent() { Control::RelayoutParent(); }

Table& Table::CellCharacterFont(std::wstring_view characters, std::wstring_view family) {
    cell_font_chars_ = characters;
    cell_font_family_ = family;
    Invalidate();
    return *this;
}

Table& Table::ColumnDisplay(int col, std::function<void(size_t, std::wstring&)> text) {
    if (col < 0 || static_cast<size_t>(col) >= columns_.size()) return *this;
    columns_[static_cast<size_t>(col)].display_get = std::move(text);
    InvalidateRows(0, row_count_);
    return *this;
}

bool Table::IsInteractive(size_t col) const noexcept {
    if (col >= columns_.size()) return false;
    const CellKind kind = columns_[col].kind;
    return kind == CellKind::CheckBox || kind == CellKind::Button || kind == CellKind::TextBox;
}

size_t Table::IndexOf(Control* ctl) const noexcept {
    for (size_t i = 0; i < ChildCount(); ++i) {
        if (&Child(i) == ctl) return i;
    }
    return ChildCount();
}

TableColumnRef Table::AddColumn(std::wstring_view title, float width) {
    return TableColumnRef{this, AddColumnIndex(title, width)};
}

Table& Table::ColumnKey(int col, std::wstring_view key) {
    if (col < 0 || static_cast<size_t>(col) >= columns_.size()) return *this;
    for (size_t i = 0; !key.empty() && i < columns_.size(); ++i)
        if (i != static_cast<size_t>(col) && columns_[i].key == key) return *this;
    columns_[static_cast<size_t>(col)].key = key;
    return *this;
}

std::wstring_view Table::ColumnKey(int col) const noexcept {
    return col >= 0 && static_cast<size_t>(col) < columns_.size() ? columns_[static_cast<size_t>(col)].key : std::wstring_view{};
}

Table& Table::ColumnSizing(int col, float minimum, float preferred, float maximum, float weight) {
    if (col < 0 || static_cast<size_t>(col) >= columns_.size() || !std::isfinite(minimum) ||
        !std::isfinite(preferred) || !std::isfinite(maximum) || !std::isfinite(weight)) return *this;
    auto& c = columns_[static_cast<size_t>(col)];
    c.minimum = std::max(1.0f, minimum);
    c.maximum = std::max(c.minimum, maximum);
    c.preferred = Clamp(preferred, c.minimum, c.maximum);
    c.weight = std::max(0.0f, weight);
    if (c.width > 0.5f) c.width = Clamp(c.width, c.minimum, c.maximum);
    RelayoutParent();
    return *this;
}

std::vector<TableColumnState> Table::CaptureColumns() const {
    std::vector<TableColumnState> result;
    for (size_t i = 0; i < columns_.size(); ++i) {
        const auto& c = columns_[visual_.size() == columns_.size() ? visual_[i] : i];
        if (!c.key.empty()) result.push_back({c.key, c.width, c.visible, c.frozen});
    }
    return result;
}

void Table::RestoreColumns(const std::vector<TableColumnState>& state) {
    UpdateScope update;
    std::vector<size_t> order;
    for (const auto& saved : state) {
        for (size_t i = 0; i < columns_.size(); ++i) {
            auto& c = columns_[i];
            if (saved.key.empty() || saved.key != c.key || std::find(order.begin(), order.end(), i) != order.end()) continue;
            order.push_back(i);
            ColumnWidth(static_cast<int>(i), saved.width);
            c.visible = saved.visible;
            c.frozen = saved.frozen;
            break;
        }
    }
    for (size_t i = 0; i < columns_.size(); ++i)
        if (std::find(order.begin(), order.end(), i) == order.end()) order.push_back(i);
    if (!columns_.empty() && std::none_of(columns_.begin(), columns_.end(), [](const ColumnDef& c) { return c.visible; }))
        columns_[order.front()].visible = true;
    visual_ = std::move(order);
    CancelCellEdit();
    ClampScroll(); SyncSlots(); RelayoutParent();
    columns_changed_.EmitDeferred();
}

int Table::AddColumnIndex(std::wstring_view title, float width) {
    if (columns_.size() >= kMaxColumns) return -1;
    columns_.push_back(ColumnDef{std::wstring(title), std::max(0.0f, width)});
    visual_.push_back(columns_.size() - 1);
    slots_.clear();
    pool_rows_ = 0;
    Clear();
    ResetCellEditor();
    RelayoutParent();
    return static_cast<int>(columns_.size()) - 1;
}

Table& Table::ColumnFrozen(int col, bool frozen) {
    UpdateScope update;
    if (col < 0 || col >= static_cast<int>(columns_.size())) return *this;
    ColumnDef& column = columns_[static_cast<size_t>(col)];
    if (column.frozen == frozen) return *this;
    column.frozen = frozen;
    ClampScroll();
    SyncSlots();
    Invalidate();
    columns_changed_.EmitDeferred();
    frozen_changed_.Emit(col, frozen);
    return *this;
}

bool Table::ColumnFrozen(int col) const noexcept {
    return col >= 0 && col < static_cast<int>(columns_.size()) &&
           columns_[static_cast<size_t>(col)].frozen;
}

Table& Table::ScrollToX(float value) {
    horizontal_target_ = value;
    ClampScroll();
    if (!window_) {
        horizontal_offset_ = horizontal_target_;
        SyncSlots();
    } else {
        Animate();
    }
    Invalidate();
    return *this;
}

Table& Table::ColumnVisible(int col, bool visible) {
    UpdateScope update;
    if (col < 0 || col >= static_cast<int>(columns_.size())) return *this;
    ColumnDef& column = columns_[static_cast<size_t>(col)];
    if (column.visible == visible) return *this;
    if (!visible) {
        size_t shown = 0;
        for (const ColumnDef& c : columns_) {
            if (c.visible) ++shown;
        }
        if (shown <= 1) return *this;
    }
    // 显隐会改变列位置，先结束旧位置上的编辑再通知订阅方调整布局。
    HideCellEditor();
    column.visible = visible;
    if (!visible && active_col_ == col) MoveActiveColumn(1);
    ClampScroll();
    SyncSlots();
    RelayoutParent();
    Invalidate();
    columns_changed_.EmitDeferred();
    column_visibility_changed_.Emit(col, visible);
    return *this;
}

bool Table::ColumnVisible(int col) const noexcept {
    return col >= 0 && col < static_cast<int>(columns_.size()) &&
           columns_[static_cast<size_t>(col)].visible;
}

Table& Table::MoveColumn(int from, int to) {
    UpdateScope update;
    if (from == to || from < 0 || to < 0) return *this;
    if (from >= static_cast<int>(columns_.size()) || to >= static_cast<int>(columns_.size())) {
        return *this;
    }
    if (visual_.size() != columns_.size()) {
        visual_.resize(columns_.size());
        for (size_t i = 0; i < visual_.size(); ++i) visual_[i] = i;
    }
    const size_t src = static_cast<size_t>(from);
    const size_t dst = static_cast<size_t>(to);
    size_t from_i = visual_.size();
    size_t to_i = visual_.size();
    for (size_t i = 0; i < visual_.size(); ++i) {
        if (visual_[i] == src) from_i = i;
        if (visual_[i] == dst) to_i = i;
    }
    if (from_i >= visual_.size() || to_i >= visual_.size() || from_i == to_i) return *this;
    const bool src_frozen = columns_[src].frozen;
    const bool dst_frozen = columns_[dst].frozen;
    if (src_frozen != dst_frozen) return *this;
    const size_t value = visual_[from_i];
    visual_.erase(visual_.begin() + static_cast<ptrdiff_t>(from_i));
    if (to_i > from_i) --to_i;
    visual_.insert(visual_.begin() + static_cast<ptrdiff_t>(to_i), value);
    columns_changed_.EmitDeferred();
    SyncSlots();
    Invalidate();
    return *this;
}

Table& Table::ActiveColumn(int col) {
    if (col < 0 || col >= static_cast<int>(columns_.size())) return *this;
    if (!columns_[static_cast<size_t>(col)].visible) return *this;
    if (active_col_ == col) return *this;
    active_col_ = col;
    EnsureColumnVisible(col);
    Invalidate();
    return *this;
}

Table& Table::GroupBy(int col) {
    if (col >= static_cast<int>(columns_.size())) return *this;
    const int next = col < 0 ? -1 : col;
    if (group_col_ == next) return *this;
    group_col_ = next;
    ApplySort();
    return *this;
}

bool Table::GroupExpanded(size_t group) const noexcept {
    return group < groups_.size() && groups_[group].expanded;
}

Table& Table::GroupExpanded(size_t group, bool expanded) {
    if (group >= groups_.size() || groups_[group].expanded == expanded) return *this;
    groups_[group].expanded = expanded;
    ClampScroll();
    SyncSlots();
    Invalidate();
    return *this;
}

Table& Table::Footer(bool on) {
    if (draw_cache_) draw_cache_->footer_dirty = draw_cache_->footer_full = true;
    if (footer_ == on) return *this;
    footer_ = on;
    ClampScroll();
    SyncSlots();
    RelayoutParent();
    Invalidate();
    return *this;
}

Table& Table::Aggregate(int col, ColumnAggregate kind) {
    if (draw_cache_) draw_cache_->footer_dirty = draw_cache_->footer_full = true;
    if (col < 0 || col >= static_cast<int>(columns_.size())) return *this;
    columns_[static_cast<size_t>(col)].aggregate = kind;
    if (footer_) Invalidate();
    return *this;
}

ColumnAggregate Table::ColumnAggregateKind(int col) const noexcept {
    if (col < 0 || col >= static_cast<int>(columns_.size())) return ColumnAggregate::None;
    return columns_[static_cast<size_t>(col)].aggregate;
}

Table& Table::ColumnKind(int col, CellKind kind) {
    if (col < 0 || col >= static_cast<int>(columns_.size())) return *this;
    auto& c = columns_[static_cast<size_t>(col)];
    if (c.kind == kind) return *this;
    c.kind = kind;
    slots_.clear();
    pool_rows_ = 0;
    Clear();
    ResetCellEditor();
    SyncSlots();
    return *this;
}

Table& Table::BindCheckBox(int col, std::function<bool(size_t)> get,
                           std::function<void(size_t, bool)> set) {
    if (draw_cache_) draw_cache_->footer_dirty = draw_cache_->footer_full = true;
    if (col < 0 || col >= static_cast<int>(columns_.size())) return *this;
    auto& c = columns_[static_cast<size_t>(col)];
    c.numeric = c.numeric_text = false;
    c.kind = CellKind::CheckBox;
    c.cb_get = std::move(get);
    c.cb_set = std::move(set);
    slots_.clear();
    pool_rows_ = 0;
    Clear();
    ResetCellEditor();
    SyncSlots();
    return *this;
}

Table& Table::BindButton(int col, std::wstring caption,
                         std::function<void(size_t)> on_click) {
    if (draw_cache_) draw_cache_->footer_dirty = draw_cache_->footer_full = true;
    if (col < 0 || col >= static_cast<int>(columns_.size())) return *this;
    auto& c = columns_[static_cast<size_t>(col)];
    c.numeric = c.numeric_text = false;
    c.kind = CellKind::Button;
    c.btn_caption = std::move(caption);
    c.btn_click = std::move(on_click);
    slots_.clear();
    pool_rows_ = 0;
    Clear();
    ResetCellEditor();
    SyncSlots();
    return *this;
}

Table& Table::BindTextBox(int col, std::function<std::wstring(size_t)> get,
                          std::function<void(size_t, std::wstring)> set) {
    if (draw_cache_) draw_cache_->footer_dirty = draw_cache_->footer_full = true;
    if (col < 0 || col >= static_cast<int>(columns_.size())) return *this;
    auto& c = columns_[static_cast<size_t>(col)];
    c.numeric = c.numeric_text = false;
    c.kind = CellKind::TextBox;
    c.tb_get = std::move(get);
    c.tb_set = std::move(set);
    slots_.clear();
    pool_rows_ = 0;
    Clear();
    ResetCellEditor();
    SyncSlots();
    return *this;
}

Table& Table::BindProgress(int col, std::function<float(size_t)> get) {
    if (draw_cache_) draw_cache_->footer_dirty = draw_cache_->footer_full = true;
    if (col < 0 || col >= static_cast<int>(columns_.size())) return *this;
    auto& c = columns_[static_cast<size_t>(col)];
    c.numeric = c.numeric_text = false;
    const bool was = IsInteractive(static_cast<size_t>(col));
    c.kind = CellKind::Progress;
    c.prog_get = std::move(get);
    if (was) {
        slots_.clear();
        pool_rows_ = 0;
        Clear();
        ResetCellEditor();
        SyncSlots();
    } else {
        Invalidate();
    }
    return *this;
}

Table& Table::BindNumber(int col, std::function<double(size_t)> get) {
    if (draw_cache_) draw_cache_->footer_dirty = draw_cache_->footer_full = true;
    if (col < 0 || col >= static_cast<int>(columns_.size())) return *this;
    auto& c = columns_[static_cast<size_t>(col)];
    const bool was = IsInteractive(static_cast<size_t>(col));
    c.kind = CellKind::Text;
    c.num_get = std::move(get);
    c.num_valid = {};
    c.num_set = {};
    c.numeric = true;
    if (was) {
        slots_.clear();
        pool_rows_ = 0;
        Clear();
        ResetCellEditor();
        SyncSlots();
    } else {
        Invalidate();
    }
    return *this;
}

Table& Table::BindNumber(int col, std::function<double(size_t)> get,
                         std::function<void(size_t, double)> set) {
    BindNumber(col, std::move(get));
    if (col < 0 || col >= static_cast<int>(columns_.size())) return *this;
    columns_[static_cast<size_t>(col)].num_set = std::move(set);
    return *this;
}

Table& Table::CellEditable(std::function<bool(size_t data_row, int col)> predicate) {
    cell_editable_ = std::move(predicate);
    Invalidate();
    return *this;
}

Table& Table::BindNullableNumber(int col, std::function<std::optional<double>(size_t)> get,
                                 std::function<void(size_t, std::optional<double>)> set) {
    if (draw_cache_) draw_cache_->footer_dirty = draw_cache_->footer_full = true;
    if (col < 0 || static_cast<size_t>(col) >= columns_.size()) return *this;
    ColumnKind(col, CellKind::Text);
    auto& c = columns_[static_cast<size_t>(col)];
    c.numeric = false;
    c.numeric_text = true;
    c.text_get = [this, col, get](size_t row, std::wstring& out) {
        auto value = get(row);
        if (value) FormatNumber(*value, columns_[static_cast<size_t>(col)].precision, out);
        else out.clear();
    };
    c.text_valid = [](std::wstring_view text) { double value{}; return text.empty() || StrictParseNumber(text, value); };
    c.text_set = [set](size_t row, std::wstring_view text) {
        double value{};
        if (text.empty()) set(row, std::nullopt);
        else if (StrictParseNumber(text, value)) set(row, value);
    };
    c.compare = [get](size_t a, size_t b) { const auto x = get(a), y = get(b); return x < y ? -1 : (x > y ? 1 : 0); };
    return *this;
}

Table& Table::BindChoice(int col, std::vector<std::wstring> choices,
                         std::function<std::wstring(size_t)> get,
                         std::function<void(size_t, std::wstring)> set) {
    if (draw_cache_) draw_cache_->footer_dirty = draw_cache_->footer_full = true;
    if (col < 0 || static_cast<size_t>(col) >= columns_.size()) return *this;
    ColumnKind(col, CellKind::Text);
    auto& c = columns_[static_cast<size_t>(col)];
    c.numeric = c.numeric_text = false;
    c.numeric = false;
    c.choices = std::move(choices);
    c.text_get = [get](size_t row, std::wstring& out) { out = get(row); };
    c.text_valid = [this, col](std::wstring_view text) {
        const auto& values = columns_[static_cast<size_t>(col)].choices;
        return std::find(values.begin(), values.end(), text) != values.end();
    };
    c.text_set = [set](size_t row, std::wstring_view text) { set(row, std::wstring(text)); };
    c.compare = [get](size_t a, size_t b) { return get(a).compare(get(b)); };
    return *this;
}

// 严格数字解析：完整消费 + 有限值。空/`12abc`/`1e999` 都是非法草稿。
bool Table::StrictParseNumber(std::wstring_view text, double& out) {
    if (text.empty()) return false;
    const std::wstring tmp(text);
    wchar_t* end = nullptr;
    const double v = wcstod(tmp.c_str(), &end);
    if (end == tmp.c_str()) return false;
    while (end < tmp.c_str() + tmp.size()) {
        if (!std::iswspace(static_cast<wint_t>(*end))) return false;
        ++end;
    }
    if (!std::isfinite(v)) return false;
    out = v;
    return true;
}

Table& Table::ColumnPrecision(int col, int decimals) {
    if (draw_cache_) draw_cache_->footer_dirty = draw_cache_->footer_full = true;
    if (col < 0 || col >= static_cast<int>(columns_.size())) return *this;
    columns_[static_cast<size_t>(col)].precision = decimals < 0 ? -1 : std::min(decimals, 12);
    Invalidate();
    return *this;
}

CellKind Table::ColumnKind(int col) const {
    return col >= 0 && static_cast<size_t>(col) < columns_.size() ? columns_[col].kind
                                                                  : CellKind::Text;
}

// 精度 >= 0 定点；否则 %.10g 自适应修整（2 → "2"，-3.6 → "-3.6"，0.1+0.2 → "0.3"）。
void Table::FormatNumber(double value, int precision, std::wstring& out) const {
    wchar_t buf[48]{};
    if (precision >= 0) {
        swprintf(buf, 48, L"%.*f", precision, value);
    } else {
        swprintf(buf, 48, L"%.10g", value);
    }
    out = buf;
}

Table& Table::BindIcon(int col, std::function<void(size_t, std::wstring&)> get) {
    if (draw_cache_) draw_cache_->footer_dirty = draw_cache_->footer_full = true;
    if (col < 0 || col >= static_cast<int>(columns_.size())) return *this;
    auto& c = columns_[static_cast<size_t>(col)];
    c.numeric = c.numeric_text = false;
    const bool was = IsInteractive(static_cast<size_t>(col));
    c.kind = CellKind::Icon;
    c.icon_get = std::move(get);
    if (was) {
        slots_.clear();
        pool_rows_ = 0;
        Clear();
        ResetCellEditor();
        SyncSlots();
    } else {
        Invalidate();
    }
    return *this;
}

Table& Table::RowHeight(float dip) {
    custom_row_height_ = std::max(0.0f, dip);
    ClampScroll();
    SyncSlots();
    RelayoutParent();
    return *this;
}

Table& Table::CellEditEnabled(bool on) {
    if (cell_edit_enabled_ == on) return *this;
    cell_edit_enabled_ = on;
    if (!on) CancelCellEdit();
    return *this;
}

void Table::ResetCellEditor() {
    cell_editor_ = nullptr;   // 子级已随 Clear() 销毁，下次编辑懒重建
    edit_row_ = -1;
    edit_col_ = -1;
}

void Table::CommitCellEdit(bool strict) {
    if (!cell_editor_ || edit_row_ < 0 || edit_col_ < 0) return;
    const std::wstring text = cell_editor_->Text();
    const size_t data = DataRowAt(static_cast<size_t>(edit_row_));
    const int col = edit_col_;
    WeakRef<Table> self(this);
    if (!CommitCell(data, col, text)) {
        if (!self) return;
        if (!strict) CancelCellEdit();
        return;
    }
    if (!self) return;
    HideCellEditor();
}

bool Table::CommitCell(size_t data, int col, std::wstring_view text) {
    if (!Enabled() || data >= row_count_ || col < 0 || static_cast<size_t>(col) >= columns_.size()) return false;
    if (cell_editable_ && !cell_editable_(data, col)) return false;
    const ColumnDef& c = columns_[static_cast<size_t>(col)];
    WeakRef<Table> self(this);
    CellEdit edit{data, col, model_ ? model_->RowKey(data) : 0, {}, std::wstring(text)};
    CellTextAt(data, static_cast<size_t>(col), edit.before);
    double number = 0.0;
    if ((c.numeric && (!c.num_set || !StrictParseNumber(text, number) || (c.num_valid && !c.num_valid(number)))) ||
        (c.text_valid && !c.text_valid(text))) {
        edit_error_ = App::Strings().invalid_format;
    } else {
        const auto error = cell_validate_ ? cell_validate_(edit) : std::wstring{};
        if (!self) return false;
        edit_error_ = error;
    }
    AccessibleHelp(edit_error_);
    if (cell_editor_) cell_editor_->AccessibleHelp(edit_error_);
    if (!edit_error_.empty()) { Invalidate(); return false; }
    if (edit.before == edit.after) return true;
    if (c.numeric) c.num_set(data, number);
    else if (c.text_set) c.text_set(data, text);
    else if (c.kind == CellKind::TextBox && c.tb_set) c.tb_set(data, std::wstring(text));
    if (!self) return true;
    InvalidateRows(data, 1);
    cell_edited_.Emit(data, col, edit.after);
    if (!self) return true;
    edit_committed_.Emit(edit);
    return true;
}

bool Table::PasteRow(size_t data, int first_column, std::wstring_view tsv) {
    if (!Enabled() || data >= row_count_ || tsv.find_first_of(L"\r\n") != std::wstring_view::npos) return false;
    std::vector<size_t> columns;
    bool started = false;
    for (size_t i = 0; i < columns_.size(); ++i) {
        const size_t col = visual_.size() == columns_.size() ? visual_[i] : i;
        if (static_cast<int>(col) == first_column) started = true;
        if (started && columns_[col].visible) columns.push_back(col);
    }
    std::vector<CellEdit> edits;
    std::vector<double> numbers;
    WeakRef<Table> self(this);
    size_t begin = 0;
    for (size_t i = 0;; ++i) {
        if (i >= columns.size()) return false;
        const size_t col = columns[i];
        const auto& c = columns_[col];
        if ((cell_editable_ && !cell_editable_(data, static_cast<int>(col))) ||
            (!c.num_set && !c.text_set && !c.tb_set)) return false;
        const size_t end = tsv.find(L'\t', begin);
        const auto text = tsv.substr(begin, end == std::wstring_view::npos ? end : end - begin);
        CellEdit edit{data, static_cast<int>(col), model_ ? model_->RowKey(data) : 0, {}, std::wstring(text)};
        CellTextAt(data, col, edit.before);
        double number = 0.0;
        if ((c.numeric && (!StrictParseNumber(text, number) || (c.num_valid && !c.num_valid(number)))) ||
            (c.text_valid && !c.text_valid(text))) edit_error_ = App::Strings().invalid_format;
        else {
            const auto error = cell_validate_ ? cell_validate_(edit) : std::wstring{};
            if (!self) return false;
            edit_error_ = error;
        }
        if (!edit_error_.empty()) { AccessibleHelp(edit_error_); return false; }
        numbers.push_back(number); edits.push_back(std::move(edit));
        if (end == std::wstring_view::npos) break;
        begin = end + 1;
    }
    const auto error = row_validate_ ? row_validate_(edits) : std::wstring{};
    if (!self) return false;
    edit_error_ = error;
    AccessibleHelp(edit_error_);
    if (!error.empty()) return false;
    CancelCellEdit();
    if (!self) return false;
    {
        UpdateScope update;
        for (size_t i = 0; i < edits.size(); ++i) {
            const auto& edit = edits[i];
            if (edit.before == edit.after) continue;
            const auto& c = columns_[static_cast<size_t>(edit.column)];
            if (c.numeric) c.num_set(data, numbers[i]);
            else if (c.text_set) c.text_set(data, edit.after);
            else if (c.tb_set) c.tb_set(data, edit.after);
            if (!self) return true;
        }
    }
    if (!self) return true;
    InvalidateRows(data, 1);
    for (const auto& edit : edits) if (edit.before != edit.after) {
        cell_edited_.Emit(data, edit.column, edit.after);
        if (!self) return true;
        edit_committed_.Emit(edit);
        if (!self) return true;
    }
    return true;
}

void Table::CancelCellEdit() {
    if (!cell_editor_ || edit_row_ < 0) return;
    const size_t data = DataRowAt(static_cast<size_t>(edit_row_));
    CellEdit edit{data, edit_col_, model_ ? model_->RowKey(data) : 0, {}, cell_editor_->Text()};
    CellTextAt(data, static_cast<size_t>(edit_col_), edit.before);
    HideCellEditor();
    edit_error_.clear();
    edit_cancelled_.Emit(edit);
}

void Table::HideCellEditor() {
    if (!cell_editor_) return;
    const size_t idx = IndexOf(cell_editor_);
    if (idx < ChildCount()) SetChildVisibility(idx, false);
    edit_row_ = -1;
    edit_col_ = -1;
    // 编辑期间收到的值变更在此收尾重排：提交的行此时才移动，输入中不动。
    if (sort_dirty_) {
        sort_dirty_ = false;
        ApplySort();
        return;
    }
    Invalidate();
}

bool Table::CellEditableAt(ptrdiff_t row, int col) const {
    if (!cell_edit_enabled_ || !cell_text_ || row < 0 || col < 0 ||
        col >= static_cast<int>(columns_.size())) {
        return false;
    }
    const ColumnDef& c = columns_[static_cast<size_t>(col)];
    if (c.kind != CellKind::Text) return false;
    if (c.numeric && !c.num_set) return false;   // 只读数字列不进编辑
    if (RowTop(static_cast<size_t>(row)) < 0.0f) return false;   // 折叠组内的行
    if (cell_editable_ && !cell_editable_(DataRowAt(static_cast<size_t>(row)), col)) return false;
    return true;
}

// Tab/Shift+Tab：视图序下一个/上一个可编辑格；越界则提交收尾。当前格由
// BeginCellEdit 内部的 CommitCellEdit 先行提交。
void Table::MoveEditableCell(bool back) {
    if (edit_row_ < 0 || edit_col_ < 0 || columns_.empty()) {
        CommitCellEdit(true);
        return;
    }
    const int cols = static_cast<int>(columns_.size());
    const int from = static_cast<int>(edit_row_) * cols + edit_col_;
    const int total = static_cast<int>(row_count_) * cols;
    for (int step = 1; step <= total; ++step) {
        const int idx = back ? from - step : from + step;
        if (idx < 0 || idx >= total) break;
        const int col = idx % cols;
        const ptrdiff_t row = static_cast<ptrdiff_t>((idx - col) / cols);
        if (!CellEditableAt(row, col)) continue;
        BeginCellEdit(row, col);
        return;
    }
    CommitCellEdit(true);
}

void Table::BeginCellEdit(ptrdiff_t row, int col) {
    if (!CellEditableAt(row, col)) return;
    CommitCellEdit(true);
    if (edit_row_ >= 0) return;   // 上一格拒绝提交，不能切换并丢弃非法草稿。
    if (!columns_[static_cast<size_t>(col)].choices.empty() && window_) {
        const size_t data = DataRowAt(static_cast<size_t>(row));
        const uint64_t key = model_ ? model_->RowKey(data) : 0;
        CellEdit edit{data, col, key, {}, {}};
        CellTextAt(data, static_cast<size_t>(col), edit.before);
        edit.after = edit.before;
        auto self = std::make_shared<WeakRef<Table>>(this);
        edit_started_.Emit(edit);
        if (!*self) return;
        Menu menu;
        bool committed = false;
        for (const auto& choice : columns_[static_cast<size_t>(col)].choices) {
            menu.AddItem(choice, [self, edit, choice, &committed] {
                if (!*self) return;
                auto* table = self->Get();
                const ptrdiff_t data_row = edit.row_key && table->model_ ? table->model_->FindKey(edit.row_key) : static_cast<ptrdiff_t>(edit.row);
                if (data_row >= 0) committed = table->CommitCell(static_cast<size_t>(data_row), edit.column, choice);
            }).Checked(choice == edit.before);
        }
        float xs[kMaxColumns]{}, ws[kMaxColumns]{};
        ColumnMetrics(absolute_.w, xs, ws, columns_.size());
        menu.Popup(*window_, {absolute_.x + xs[col], absolute_.y + BodyTop() + RowTop(static_cast<size_t>(row)) - scroll_offset_ + RowHeight()});
        if (*self && !committed) edit_cancelled_.Emit(edit);
        return;
    }

    if (!cell_editor_) {
        cell_editor_ = &Add<CellEditor>();
        SetChildVisibility(IndexOf(cell_editor_), false);
        cell_editor_->committed = [this] { CommitCellEdit(true); };
        cell_editor_->cancelled = [this] { CancelCellEdit(); };
        cell_editor_->focus_lost = [this] { CommitCellEdit(false); };   // 失焦：数字非法回退
        cell_editor_->tab_move = [this] {
            MoveEditableCell((GetKeyState(VK_SHIFT) & 0x8000) != 0);
        };
    }
    cell_editor_->numeric = columns_[static_cast<size_t>(col)].numeric;
    cell_editor_->number_valid = columns_[static_cast<size_t>(col)].num_valid;
    edit_row_ = row;
    edit_col_ = col;

    float xs[kMaxColumns]{};
    float ws[kMaxColumns]{};
    float frozen_width = 0.0f;
    ColumnMetrics(absolute_.w, xs, ws, columns_.size(), &frozen_width, nullptr);
    const float row_h = std::max(RowHeight(), 1.0f);
    const float y = BodyTop() + RowTop(static_cast<size_t>(row)) - scroll_offset_;
    const bool frozen = columns_[static_cast<size_t>(col)].frozen;
    const float left = frozen ? 0.0f : frozen_width;
    const float right = absolute_.w - (MaxScroll() > 0.5f ? kBarHit : 0.0f);
    const float cell_x = std::max(xs[static_cast<size_t>(col)] + 2.0f, left + 2.0f);
    const float cell_right = std::min(xs[static_cast<size_t>(col)] + ws[static_cast<size_t>(col)] - 2.0f,
                                      right);
    if (cell_right - cell_x < 24.0f) return;
    SetChildBounds(*cell_editor_,
                   {cell_x, y + 2.0f, cell_right - cell_x, row_h - 4.0f});
    const size_t idx = IndexOf(cell_editor_);
    SetChildVisibility(idx, true);
    ArrangeChildAt(idx);
    draw_text_.clear();
    CellTextAt(DataRowAt(static_cast<size_t>(row)), static_cast<size_t>(col), draw_text_);
    cell_editor_->Text(draw_text_);
    cell_editor_->FocusCaret();
    Invalidate();
    edit_error_.clear();
    const size_t data = DataRowAt(static_cast<size_t>(row));
    edit_started_.Emit(CellEdit{data, col, model_ ? model_->RowKey(data) : 0, draw_text_, draw_text_});
}

void Table::OnMouseDoubleClick(Point local) {
    const ptrdiff_t row = RowAt(local);
    if (row < 0) return;
    const int col = ColumnAt(local.x);
    if (col < 0) return;
    if (cell_double_click_) {
        cell_double_click_(DataRowAt(static_cast<size_t>(row)), col);
        return;
    }
    BeginCellEdit(row, col);
}

Table& Table::RowCount(size_t count) {
    if (draw_cache_) draw_cache_->footer_dirty = draw_cache_->footer_full = true;
    const bool reset_view = count != row_count_ && (sort_col_ >= 0 || group_col_ >= 0);
    const int prev_sort = sort_col_;
    if (reset_view) {
        // 数据行身份随行数变化不再可靠，直接回恒等视图
        sort_col_ = -1;
        sort_dir_ = 1;
        sort_keys_.clear();
        order_.clear();
        groups_.clear();
    }
    row_count_ = count;
    if (group_col_ >= 0) ApplySort();
    if (reset_view && prev_sort >= 0) sort_changed_.Emit(-1, 0);
    ClampScroll();
    SyncSlots();
    RelayoutParent();
    return *this;
}

Table& Table::Bind(ItemsModel& model) {
    typed_get_ = {};
    typed_touch_ = {};
    owned_model_.reset();
    model_ = &model;
    cell_text_ = [this](size_t row, size_t col, std::wstring& out) {
        if (!model_) {
            out.clear();
            return;
        }
        model_->Get(row, model_row_);
        if (col < model_row_.cells.size()) {
            out = model_row_.cells[col];
            return;
        }
        if (col == 0) out = model_row_.text;
        else out.clear();
    };
    model_inserted_ = ScopedConnection(model.OnInserted([this](size_t, size_t) {
        if (!model_) return;
        CancelCellEdit();
        if (draw_cache_) draw_cache_->footer_dirty = draw_cache_->footer_full = true;
        row_count_ = model_->Count();
        ApplySort();
        SelectKey(selected_key_);
        RelayoutParent();
    }));
    model_removed_ = ScopedConnection(model.OnRemoved([this](size_t, size_t) {
        if (!model_) return;
        CancelCellEdit();
        if (draw_cache_) draw_cache_->footer_dirty = draw_cache_->footer_full = true;
        row_count_ = model_->Count();
        ApplySort();
        SelectKey(selected_key_);
        RelayoutParent();
    }));
    model_changed_ = ScopedConnection(model.OnChanged([this](size_t index, size_t count) {
        MarkFooterRows(index, count);
        // 值变更可能改排序/分组键：重算视图映射（R20——旧实现只刷新不重排，
        // 排序箭头指向旧序）。单元格编辑器打开期间延后到编辑收尾，避免正在
        // 输入的行突然移动。
        if (edit_row_ >= 0) {
            sort_dirty_ = true;
            SyncSlots();
            InvalidateRows(index, count);
            return;
        }
        if (sort_col_ >= 0 || group_col_ >= 0) {
            ApplySort();   // 行会移动：由 ApplySort 统一失效整帧
            return;
        }
        // R21：无排序时的逐行更新走增量失效——只标脏可见的受影响行带 + 页脚，
        // 1 万行表单行刷新不再整窗重绘。
        InvalidateRows(index, count);
        SyncSlots();   // 可见窗口池同步；文本未变的槽自动跳过
    }));
    model_reset_ = ScopedConnection(model.OnReset([this] {
        if (!model_) return;
        CancelCellEdit();
        if (draw_cache_) draw_cache_->footer_dirty = draw_cache_->footer_full = true;
        row_count_ = model_->Count();
        ApplySort();
        SelectKey(selected_key_);
        RelayoutParent();
    }));
    RowCount(model.Count());
    model_detached_ = ScopedConnection(model.OnDetached([this] {
        typed_get_ = {};
        typed_touch_ = {};
        model_ = nullptr;
        CancelCellEdit();
        owned_model_.reset();
        RowCount(0);
    }));
    return *this;
}

Table& Table::Bind(std::shared_ptr<ItemsModel> model) {
    if (!model) return *this;
    ItemsModel& ref = *model;
    Bind(ref);
    owned_model_ = std::move(model);
    return *this;
}

Table& Table::Sortable(int col, bool on) {
    if (col < 0 || col >= static_cast<int>(columns_.size())) return *this;
    auto& c = columns_[static_cast<size_t>(col)];
    if (c.sortable == on) return *this;
    c.sortable = on;
    if (!on && col == sort_col_) SortBy(col, 0);
    return *this;
}

bool Table::Sortable(int col) const noexcept {
    return col >= 0 && col < static_cast<int>(columns_.size()) &&
           columns_[static_cast<size_t>(col)].sortable;
}

Table& Table::RowComparator(std::function<bool(size_t, size_t, int)> less) {
    row_less_ = std::move(less);
    return *this;
}

float Table::ColumnWidth(int col) const {
    if (col < 0 || col >= static_cast<int>(columns_.size())) return 0.0f;
    return columns_[static_cast<size_t>(col)].width;
}

void Table::ColumnWidth(int col, float dip) {
    if (col < 0 || col >= static_cast<int>(columns_.size()) || !std::isfinite(dip)) return;
    auto& c = columns_[static_cast<size_t>(col)];
    c.width = dip > 0.5f ? Clamp(dip, c.minimum > 0.0f ? c.minimum : 1.0f, c.maximum) : 0.0f;
    SyncSlots();
    RelayoutParent();
    columns_changed_.EmitDeferred();
}

void Table::SortBy(int col, int direction, bool append) {
    if (col < 0 || col >= static_cast<int>(columns_.size())) return;
    direction = direction > 0 ? 1 : (direction < 0 ? -1 : 0);

    if (!append || direction == 0) {
        if (direction == 0) {
            sort_keys_.clear();
        } else {
            sort_keys_.clear();
            sort_keys_.push_back({col, direction});
        }
    } else {
        bool found = false;
        for (TableSortKey& key : sort_keys_) {
            if (key.col != col) continue;
            key.direction = direction;
            found = true;
            break;
        }
        if (!found) {
            if (static_cast<int>(sort_keys_.size()) >= kMaxSortKeys) sort_keys_.erase(sort_keys_.begin());
            sort_keys_.push_back({col, direction});
        }
    }
    ApplySort();
}

int Table::CompareCells(size_t a, size_t b, int col) const {
    if (col < 0 || col >= static_cast<int>(columns_.size())) return 0;
    const ColumnDef& c = columns_[static_cast<size_t>(col)];
    if (c.compare) return c.compare(a, b);
    if (c.numeric && c.num_get) {
        // 数字列按原始数值比较（2,10,100 正确），不受显示格式影响。
        const double va = c.num_get(a);
        const double vb = c.num_get(b);
        if (va < vb) return -1;
        if (vb < va) return 1;
        return 0;
    }
    if (row_less_) {
        if (row_less_(a, b, col)) return -1;
        if (row_less_(b, a, col)) return 1;
        return 0;
    }
    if (!cell_text_) return a < b ? -1 : (a > b ? 1 : 0);
    std::wstring ta, tb;
    cell_text_(a, static_cast<size_t>(col), ta);
    cell_text_(b, static_cast<size_t>(col), tb);
    if (ta < tb) return -1;
    if (tb < ta) return 1;
    return 0;
}

void Table::ApplySort() {
    const int prev_col = sort_col_;
    const int prev_dir = SortDirection();
    const ptrdiff_t selected_data =
        selected_ >= 0 ? static_cast<ptrdiff_t>(DataRowAt(static_cast<size_t>(selected_))) : -1;

    sort_col_ = sort_keys_.empty() ? -1 : sort_keys_.front().col;
    sort_dir_ = sort_keys_.empty() ? 1 : sort_keys_.front().direction;
    order_.clear();
    const bool need_order = row_count_ > 1 && (group_col_ >= 0 || !sort_keys_.empty()) &&
                            (row_less_ || cell_text_);
    if (need_order) {
        order_.resize(row_count_);
        for (size_t i = 0; i < row_count_; ++i) order_[i] = i;
        std::stable_sort(order_.begin(), order_.end(), [this](size_t a, size_t b) {
            if (group_col_ >= 0) {
                const int g = CompareCells(a, b, group_col_);
                if (g != 0) return g < 0;
            }
            for (const TableSortKey& key : sort_keys_) {
                const int cmp = CompareCells(a, b, key.col);
                if (cmp == 0) continue;
                return key.direction > 0 ? cmp < 0 : cmp > 0;
            }
            return a < b;
        });
    }

    RebuildGroups();

    if (selected_data >= 0) {
        selected_ = -1;
        if (order_.empty()) {
            selected_ = selected_data;
        } else {
            for (size_t v = 0; v < order_.size(); ++v) {
                if (static_cast<ptrdiff_t>(order_[v]) == selected_data) {
                    selected_ = static_cast<ptrdiff_t>(v);
                    break;
                }
            }
        }
        if (selected_ >= 0) ScrollTo(static_cast<size_t>(selected_));
    }
    ClampScroll();
    SyncSlots();
    Invalidate();
    if (sort_col_ != prev_col || SortDirection() != prev_dir) {
        sort_changed_.Emit(sort_col_, SortDirection());
    }
}

void Table::RebuildGroups() {
    groups_.clear();
    const bool numeric_group = group_col_ >= 0 &&
                               static_cast<size_t>(group_col_) < columns_.size() &&
                               columns_[static_cast<size_t>(group_col_)].numeric;
    if (group_col_ < 0 || row_count_ == 0 || (!cell_text_ && !numeric_group)) return;
    Group current;
    current.expanded = true;
    for (size_t v = 0; v < row_count_; ++v) {
        std::wstring key;
        CellTextAt(DataRowAt(v), static_cast<size_t>(group_col_), key);
        if (v == 0) {
            current.key = std::move(key);
            current.start = 0;
            current.count = 1;
            continue;
        }
        if (key == current.key) {
            ++current.count;
            continue;
        }
        groups_.push_back(std::move(current));
        current = Group{std::move(key), v, 1, true};
    }
    groups_.push_back(std::move(current));
}

ptrdiff_t Table::ViewRowOf(size_t data_row) const {
    if (data_row >= row_count_) return -1;
    if (order_.empty()) return static_cast<ptrdiff_t>(data_row);
    for (size_t v = 0; v < order_.size(); ++v) {
        if (order_[v] == data_row) return static_cast<ptrdiff_t>(v);
    }
    return -1;
}

// 只把受影响数据行映射回视图行带 + 页脚标脏；视口外/折叠行零重绘。
void Table::RefreshRows(size_t index, size_t count) {
    InvalidateRows(index, count);
    if (!window_ || row_count_ == 0 || count == 0)
        return;
    const float body_top = BodyTop();
    const float body_bottom = body_top + BodyHeight();
    bool visible = false;
    for (size_t d = index; d < index + count && d < row_count_; ++d) {
        const ptrdiff_t view = ViewRowOf(d);
        if (view < 0) continue;
        const float top = RowTop(static_cast<size_t>(view));
        if (top < 0.0f) continue;
        const float y = body_top + top - scroll_offset_;
        const float bottom = y + std::max(RowHeight(), 1.0f);
        if (bottom > body_top && y < body_bottom) {
            visible = true;
            break;
        }
    }
    if (visible)
        SyncSlots();
}

void Table::InvalidateRows(size_t index, size_t count) {
    MarkFooterRows(index, count);
    if (!window_ || row_count_ == 0) return;
    const float row_h = std::max(RowHeight(), 1.0f);
    const float body_top = BodyTop();
    const float body_bottom = body_top + BodyHeight();
    Rect dirty{};
    bool any = false;
    for (size_t d = index; d < index + count && d < row_count_; ++d) {
        const ptrdiff_t view = ViewRowOf(d);
        if (view < 0) continue;
        const float top = RowTop(static_cast<size_t>(view));
        if (top < 0.0f) continue;   // 折叠组内的行
        const float y = body_top + top - scroll_offset_;
        if (y + row_h < body_top || y > body_bottom) continue;   // 视口外
        const Rect band{absolute_.x, absolute_.y + y, absolute_.w, row_h};
        dirty = any ? UnionRect(dirty, band) : band;
        any = true;
    }
    if (footer_) {
        const Rect f = FooterRect();
        dirty = any ? UnionRect(dirty, f) : f;
        any = true;
    }
    if (any) WindowImpl::InvalidateRegion(window_, dirty);
}

float Table::ContentHeight() const {
    const float row_h = std::max(RowHeight(), 1.0f);
    if (groups_.empty()) return static_cast<float>(row_count_) * row_h;
    float h = 0.0f;
    for (const Group& g : groups_) {
        h += GroupBand();
        if (g.expanded) h += static_cast<float>(g.count) * row_h;
    }
    return h;
}

float Table::RowTop(size_t view_row) const {
    const float row_h = std::max(RowHeight(), 1.0f);
    if (groups_.empty()) return static_cast<float>(view_row) * row_h;
    float y = 0.0f;
    for (const Group& g : groups_) {
        y += GroupBand();
        if (view_row < g.start) return -1.0f;
        if (view_row < g.start + g.count) {
            if (!g.expanded) return -1.0f;
            return y + static_cast<float>(view_row - g.start) * row_h;
        }
        if (g.expanded) y += static_cast<float>(g.count) * row_h;
    }
    return -1.0f;
}

float Table::MaxScroll() const {
    return std::max(0.0f, ContentHeight() - BodyHeight());
}

float Table::ColumnPixelWidth(int col, float viewport_width) const {
    if (col < 0 || static_cast<size_t>(col) >= columns_.size() || !columns_[col].visible) {
        return 0.0f;
    }
    float xs[kMaxColumns]{};
    float ws[kMaxColumns]{};
    ColumnMetrics(std::max(viewport_width, 1.0f), xs, ws, columns_.size());
    return ws[col];
}

float Table::ColumnsPixelWidth(float viewport_width) const {
    float xs[kMaxColumns]{};
    float ws[kMaxColumns]{};
    float frozen_width = 0.0f;
    float scrollable_width = 0.0f;
    ColumnMetrics(std::max(viewport_width, 1.0f), xs, ws, columns_.size(), &frozen_width,
                  &scrollable_width);
    return frozen_width + scrollable_width;
}

float Table::NaturalHeight(float viewport_width) const {
    float xs[kMaxColumns]{};
    float ws[kMaxColumns]{};
    float frozen_width = 0.0f;
    float scrollable_width = 0.0f;
    ColumnMetrics(std::max(viewport_width, 1.0f), xs, ws, columns_.size(), &frozen_width,
                  &scrollable_width);
    const bool hscroll = scrollable_width > std::max(0.0f, viewport_width - frozen_width);
    return HeaderHeight() + ContentHeight() + FooterHeight() + (hscroll ? kBarHit : 0.0f);
}

float Table::MaxHorizontalScroll() const {
    if (absolute_.w <= 0.5f || columns_.empty()) return 0.0f;
    float xs[kMaxColumns]{};
    float ws[kMaxColumns]{};
    float frozen_width = 0.0f;
    float scrollable_width = 0.0f;
    ColumnMetrics(absolute_.w, xs, ws, columns_.size(), &frozen_width, &scrollable_width);
    return std::max(0.0f, scrollable_width - std::max(0.0f, absolute_.w - frozen_width));
}

float Table::BodyHeight() const noexcept {
    const float bar = MaxHorizontalScroll() > 0.5f ? kBarHit : 0.0f;
    return std::max(0.0f, absolute_.h - BodyTop() - FooterHeight() - bar);
}

Rect Table::FooterRect() const noexcept {
    if (!footer_) return {};
    const float bar = MaxHorizontalScroll() > 0.5f ? kBarHit : 0.0f;
    return {absolute_.x, absolute_.Bottom() - bar - FooterHeight(), absolute_.w, FooterHeight()};
}

void Table::ClampScroll() {
    target_offset_ = Clamp(target_offset_, 0.0f, MaxScroll());
    scroll_offset_ = Clamp(scroll_offset_, 0.0f, MaxScroll());
    horizontal_target_ = Clamp(horizontal_target_, 0.0f, MaxHorizontalScroll());
    horizontal_offset_ = Clamp(horizontal_offset_, 0.0f, MaxHorizontalScroll());
}

Table& Table::SelectedIndex(ptrdiff_t index) {
    if (index < -1 || index >= static_cast<ptrdiff_t>(row_count_)) return *this;
    selected_key_ = model_ && index >= 0 ? model_->RowKey(DataRowAt(static_cast<size_t>(index))) : 0;
    if (selected_ == index) return *this;
    selected_ = index;
    if (index >= 0) ScrollTo(static_cast<size_t>(index));
    Invalidate();
    selection_changed_.Emit(selected_, SelectedDataIndex());
    return *this;
}

Table& Table::SelectKey(uint64_t key) {
    if (!key || !model_) {
        if (selected_ >= static_cast<ptrdiff_t>(row_count_)) SelectedIndex(-1);
        return *this;
    }
    const ptrdiff_t data = model_->FindKey(key);
    SelectedIndex(data < 0 ? -1 : ViewRowOf(static_cast<size_t>(data)));
    // Retain identity while filtered out so a later reset can restore it.
    selected_key_ = key;
    return *this;
}

void Table::ScrollTo(size_t index) {
    if (absolute_.IsEmpty()) return;
    const float row_h = std::max(RowHeight(), 1.0f);
    const float top = RowTop(index);
    if (top < 0.0f) return;
    const float bottom = top + row_h;
    const float body_h = BodyHeight();
    if (top < scroll_offset_) target_offset_ = top;
    else if (bottom > scroll_offset_ + body_h) target_offset_ = bottom - body_h;
    target_offset_ = Clamp(target_offset_, 0.0f, MaxScroll());
    if (!window_) { scroll_offset_ = target_offset_; SyncSlots(); Invalidate(); }
    else Animate();
}

void Table::MoveSelection(ptrdiff_t delta) {
    if (row_count_ == 0 || delta == 0) return;
    ptrdiff_t next =
        selected_ < 0 ? (delta > 0 ? 0 : static_cast<ptrdiff_t>(row_count_) - 1)
                      : Clamp(selected_ + delta, ptrdiff_t{0},
                              static_cast<ptrdiff_t>(row_count_) - 1);
    if (!groups_.empty()) {
        const int step = delta > 0 ? 1 : -1;
        while (next >= 0 && next < static_cast<ptrdiff_t>(row_count_) &&
               RowTop(static_cast<size_t>(next)) < 0.0f) {
            next += step;
        }
        if (next < 0 || next >= static_cast<ptrdiff_t>(row_count_)) return;
    }
    SelectedIndex(next);
}

Size Table::Measure(Size available, const Theme& theme) {
    if (custom_row_height_ <= 0.5f) theme_row_height_ = theme.list_row_height;
    const float w = (available.w > 0.5f && available.w < 1.0e4f) ? available.w : 320.0f;
    return {w, 240.0f};
}

void Table::Arrange(const Rect& absolute) {
    absolute_ = absolute;
    SyncSlots();
}

void Table::OnFocusChanged(bool focused) {
    Control::OnFocusChanged(focused);
    Invalidate();
}

bool Table::OnAnimate(float dt_seconds) {
    const float prev = scroll_offset_;
    const float prev_x = horizontal_offset_;
    bool moving = EaseTo(scroll_offset_, target_offset_, dt_seconds, 20.0f, 0.1f);
    moving |= EaseTo(horizontal_offset_, horizontal_target_, dt_seconds, 20.0f, 0.1f);
    moving |= EaseTo(expand_progress_,
                     (hovered_ || dragging_ || horizontal_dragging_) ? 1.0f : 0.0f,
                     dt_seconds, 18.0f);
    if (std::fabs(scroll_offset_ - prev) > 0.01f ||
        std::fabs(horizontal_offset_ - prev_x) > 0.01f) {
        SyncSlots();
    }
    if (moving) Invalidate();
    if (cell_flow_highlight_ && flow_active_ && MotionScale() > 0.0f) {
        flow_angle_ = std::fmod(flow_angle_ + dt_seconds * 2.8f, 6.28318530718f);
        Invalidate();
        moving = true;
    }
    const bool base = Control::OnAnimate(dt_seconds);
    return moving || base;
}

Rect Table::BodyViewport() const noexcept {
    return {absolute_.x, absolute_.y + BodyTop(), absolute_.w, BodyHeight()};
}

Rect Table::ChildrenClipBounds() const noexcept {
    Rect r = BodyViewport();
    if (!groups_.empty()) {
        r.y += GroupBand();
        r.h = std::max(0.0f, r.h - GroupBand());
    }
    return r;
}

Rect Table::VerticalTrack() const noexcept {
    return {absolute_.Right() - kBarHit, absolute_.y + BodyTop(), kBarHit, BodyHeight()};
}

Rect Table::HorizontalTrack() const noexcept {
    float xs[kMaxColumns]{};
    float ws[kMaxColumns]{};
    float frozen_width = 0.0f;
    ColumnMetrics(absolute_.w, xs, ws, columns_.size(), &frozen_width, nullptr);
    return {absolute_.x + frozen_width, absolute_.Bottom() - kBarHit,
            std::max(0.0f, absolute_.w - frozen_width), kBarHit};
}

ScrollThumb Table::Thumb(float expand) const noexcept {
    return MakeScrollThumb(BodyViewport(), ContentHeight(), scroll_offset_, expand, true);
}

ScrollThumb Table::HorizontalThumb(float expand) const noexcept {
    const Rect track = HorizontalTrack();
    return MakeScrollThumb(track, track.w + MaxHorizontalScroll(), horizontal_offset_, expand,
                           false);
}

bool Table::CapturesOverlay(Point p) const {
    if (dragging_ || horizontal_dragging_ || reorder_dragging_ || resize_col_ >= 0) return true;
    if (MaxScroll() > 0.5f && VerticalTrack().Contains(p)) return true;
    return MaxHorizontalScroll() > 0.5f && HorizontalTrack().Contains(p);
}

bool Table::BeginScrollDrag(Point local) {
    const Point world{absolute_.x + local.x, absolute_.y + local.y};
    if (MaxScroll() > 0.5f && VerticalTrack().Contains(world)) {
        const ScrollThumb thumb = Thumb(1.0f);
        dragging_ = true;
        const float local_body_y = local.y - BodyTop();
        if (thumb.visible && thumb.rect.Contains(world)) {
            drag_grab_ = world.y - thumb.rect.y;
        } else {
            const float thumb_h = thumb.visible ? thumb.rect.h : 20.0f;
            const float track = std::max(1.0f, BodyHeight() - thumb_h);
            const float t = Clamp((local_body_y - thumb_h * 0.5f) / track, 0.0f, 1.0f);
            scroll_offset_ = target_offset_ = t * MaxScroll();
            drag_grab_ = thumb_h * 0.5f;
            SyncSlots();
            Invalidate();
        }
        Animate();
        return true;
    }
    if (MaxHorizontalScroll() > 0.5f && HorizontalTrack().Contains(world)) {
        const ScrollThumb thumb = HorizontalThumb(1.0f);
        horizontal_dragging_ = true;
        if (thumb.visible && thumb.rect.Contains(world)) {
            drag_grab_ = world.x - thumb.rect.x;
        } else {
            const float thumb_w = thumb.visible ? thumb.rect.w : 20.0f;
            const float track = std::max(1.0f, HorizontalTrack().w - thumb_w);
            const float t = Clamp((local.x - (HorizontalTrack().x - absolute_.x) - thumb_w * 0.5f) /
                                      track,
                                  0.0f, 1.0f);
            horizontal_offset_ = horizontal_target_ = t * MaxHorizontalScroll();
            drag_grab_ = thumb_w * 0.5f;
            SyncSlots();
            Invalidate();
        }
        Animate();
        return true;
    }
    return false;
}

void Table::ColumnMetrics(float inner_w, float* xs, float* ws, size_t cap,
                          float* frozen_width, float* scrollable_width) const {
    const size_t n = std::min(columns_.size(), cap);
    size_t order[kMaxColumns];
    size_t count = 0;
    const bool use_visual = visual_.size() == columns_.size();
    if (use_visual) {
        for (size_t vi = 0; vi < visual_.size() && count < n; ++vi) {
            const size_t i = visual_[vi];
            if (i < n && columns_[i].visible) order[count++] = i;
        }
    } else {
        for (size_t i = 0; i < n; ++i) {
            if (columns_[i].visible) order[count++] = i;
        }
    }
    for (size_t i = 0; i < n; ++i) {
        xs[i] = 0.0f;
        ws[i] = 0.0f;
    }
    float used = 0.0f;
    for (size_t k = 0; k < count; ++k) {
        const size_t i = order[k];
        const auto& c = columns_[i];
        const float minimum = c.minimum > 0.0f ? c.minimum : (c.width > 0.5f ? 1.0f : kMinFlexWidth);
        ws[i] = Clamp(c.width > 0.5f ? c.width : c.preferred, minimum, c.maximum);
        used += ws[i];
    }
    for (size_t pass = 0; pass < count; ++pass) {
        const float remaining = inner_w - used;
        if (std::fabs(remaining) < 0.01f) break;
        float weights = 0.0f;
        for (size_t k = 0; k < count; ++k) {
            const size_t i = order[k]; const auto& c = columns_[i];
            const float minimum = c.minimum > 0.0f ? c.minimum : kMinFlexWidth;
            if (c.width <= 0.5f && ((remaining > 0 && ws[i] < c.maximum) || (remaining < 0 && ws[i] > minimum))) weights += c.weight;
        }
        if (weights <= 0.0f) break;
        for (size_t k = 0; k < count; ++k) {
            const size_t i = order[k]; const auto& c = columns_[i];
            if (c.width > 0.5f) continue;
            const float minimum = c.minimum > 0.0f ? c.minimum : kMinFlexWidth;
            if ((remaining > 0 && ws[i] >= c.maximum) || (remaining < 0 && ws[i] <= minimum)) continue;
            const float next = Clamp(ws[i] + remaining * c.weight / weights, minimum, c.maximum);
            used += next - ws[i]; ws[i] = next;
        }
    }
    float frozen_x = 0.0f;
    for (size_t k = 0; k < count; ++k) {
        const size_t i = order[k];
        if (columns_[i].frozen) {
            xs[i] = frozen_x;
            frozen_x += ws[i];
        }
    }
    float scrolling_x = frozen_x - horizontal_offset_;
    float scrolling_content = 0.0f;
    for (size_t k = 0; k < count; ++k) {
        const size_t i = order[k];
        if (columns_[i].frozen) continue;
        xs[i] = scrolling_x;
        scrolling_x += ws[i];
        scrolling_content += ws[i];
    }
    if (frozen_width) *frozen_width = frozen_x;
    if (scrollable_width) *scrollable_width = scrolling_content;
}

void Table::SnapFlexToPixels() {
    if (columns_.empty() || absolute_.w < 0.5f) return;
    float xs[kMaxColumns]{};
    float ws[kMaxColumns]{};
    ColumnMetrics(absolute_.w, xs, ws, columns_.size());
    for (size_t i = 0; i < columns_.size(); ++i) {
        if (!columns_[i].visible || columns_[i].width > 0.5f) continue;
        if (ws[i] > 0.5f) columns_[i].width = ws[i];
    }
}

ptrdiff_t Table::RowAt(Point local) const {
    if (local.y < BodyTop() || local.y >= BodyTop() + BodyHeight() || local.x < 0.0f ||
        local.x > absolute_.w) {
        return -1;
    }
    const float y = local.y - BodyTop() + scroll_offset_;
    if (y < 0.0f) return -1;
    const float row_h = std::max(RowHeight(), 1.0f);
    if (groups_.empty()) {
        const ptrdiff_t row = static_cast<ptrdiff_t>(y / row_h);
        if (row >= static_cast<ptrdiff_t>(row_count_)) return -1;
        return row;
    }
    float cursor = 0.0f;
    for (const Group& g : groups_) {
        cursor += GroupBand();
        if (!g.expanded) continue;
        const float extent = static_cast<float>(g.count) * row_h;
        if (y >= cursor && y < cursor + extent) {
            return static_cast<ptrdiff_t>(g.start) + static_cast<ptrdiff_t>((y - cursor) / row_h);
        }
        cursor += extent;
    }
    return -1;
}

ptrdiff_t Table::GroupAt(Point local) const {
    if (groups_.empty() || local.x < 0.0f || local.x > absolute_.w) return -1;
    if (local.y < BodyTop() || local.y >= BodyTop() + BodyHeight()) return -1;
    const float y = local.y - BodyTop() + scroll_offset_;
    const float row_h = std::max(RowHeight(), 1.0f);
    float cursor = 0.0f;
    ptrdiff_t sticky = -1;
    float sticky_cursor = 0.0f;
    for (size_t g = 0; g < groups_.size(); ++g) {
        if (sticky_cursor <= scroll_offset_) sticky = static_cast<ptrdiff_t>(g);
        sticky_cursor += GroupBand();
        if (groups_[g].expanded) sticky_cursor += static_cast<float>(groups_[g].count) * row_h;
        if (y >= cursor && y < cursor + GroupBand()) return static_cast<ptrdiff_t>(g);
        cursor += GroupBand();
        if (groups_[g].expanded) cursor += static_cast<float>(groups_[g].count) * row_h;
    }
    const float local_y = local.y - BodyTop();
    if (local_y >= 0.0f && local_y < GroupBand() && sticky >= 0) return sticky;
    return -1;
}

int Table::ColumnAt(float x) const {
    const size_t n = std::min(columns_.size(), kMaxColumns);
    if (n == 0) return -1;
    float xs[kMaxColumns]{};
    float ws[kMaxColumns]{};
    float frozen_width = 0.0f;
    ColumnMetrics(absolute_.w, xs, ws, n, &frozen_width, nullptr);
    for (size_t i = 0; i < n; ++i) {
        if (ws[i] < 0.5f) continue;
        if (columns_[i].frozen && x >= xs[i] && x < xs[i] + ws[i]) {
            return static_cast<int>(i);
        }
    }
    if (x < frozen_width) return -1;
    for (size_t i = 0; i < n; ++i) {
        if (ws[i] < 0.5f || columns_[i].frozen) continue;
        if (x >= xs[i] && x < xs[i] + ws[i]) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int Table::ResizeBoundaryAt(Point local) const {
    const size_t n = std::min(columns_.size(), kMaxColumns);
    if (n == 0) return -1;
    float xs[kMaxColumns]{};
    float ws[kMaxColumns]{};
    float frozen_width = 0.0f;
    ColumnMetrics(absolute_.w, xs, ws, n, &frozen_width, nullptr);
    for (size_t i = 0; i < n; ++i) {
        if (ws[i] < 0.5f) continue;
        const float bx = xs[i] + ws[i];
        if (!columns_[i].frozen && bx <= frozen_width + 1.0f) continue;
        if (bx < 1.0f || bx > absolute_.w - 2.0f) continue;
        if (std::fabs(local.x - bx) <= kResizeHit) return static_cast<int>(i);
    }
    return -1;
}

int Table::HeaderPinAt(Point local) const {
    if (local.y < 0.0f || local.y >= HeaderHeight()) return -1;
    const size_t n = std::min(columns_.size(), kMaxColumns);
    if (n == 0) return -1;
    float xs[kMaxColumns]{};
    float ws[kMaxColumns]{};
    ColumnMetrics(absolute_.w, xs, ws, n);
    for (size_t i = 0; i < n; ++i) {
        if (ws[i] < kPinSlot + 4.0f) continue;
        const float left = xs[i] + ws[i] - kPinSlot;
        const float right = xs[i] + ws[i] - 6.0f;
        if (local.x >= left && local.x < right) return static_cast<int>(i);
    }
    return -1;
}

CursorShape Table::CursorAt(Point local) const {
    if (local.y < HeaderHeight()) {
        if (ResizeBoundaryAt(local) >= 0) return CursorShape::SizeWE;
        if (HeaderPinAt(local) >= 0) return CursorShape::Hand;
    }
    return CursorShape::Arrow;
}

size_t Table::FillInteractive(size_t* out, size_t cap) const noexcept {
    size_t n = 0;
    const auto add = [&](size_t c) {
        if (n >= cap || c >= columns_.size()) return;
        if (IsInteractive(c) && columns_[c].visible) out[n++] = c;
    };
    if (visual_.size() == columns_.size()) {
        for (size_t c : visual_) add(c);
    } else {
        for (size_t c = 0; c < columns_.size(); ++c) add(c);
    }
    return n;
}

uint64_t Table::InteractiveFingerprint() const noexcept {
    size_t icols[kMaxColumns];
    const size_t n = FillInteractive(icols, kMaxColumns);
    uint64_t fp = n + 1;
    for (size_t k = 0; k < n; ++k) {
        fp = fp * 33u + icols[k] + 1;
        fp = fp * 33u + static_cast<unsigned>(columns_[icols[k]].kind) + 1;
    }
    return fp;
}

void Table::EnsurePool() {
    size_t icols[kMaxColumns];
    const size_t n_icols = FillInteractive(icols, kMaxColumns);
    if (n_icols == 0) {
        if (!slots_.empty()) {
            slots_.clear();
            pool_rows_ = 0;
            pool_fp_ = 0;
            Clear();
            ResetCellEditor();
        }
        return;
    }

    const float row_h = std::max(RowHeight(), 1.0f);
    const size_t rows = std::max(static_cast<size_t>(BodyHeight() / row_h) + 2, size_t{1});
    const uint64_t fp = InteractiveFingerprint();
    if (fp == pool_fp_ && rows <= pool_rows_ && ChildCount() == slots_.size() && !slots_.empty()) {
        return;
    }

    const bool grow = fp == pool_fp_ && !slots_.empty() && ChildCount() == slots_.size() &&
                      rows > pool_rows_;
    size_t begin = 0;
    const size_t total = rows * n_icols;
    if (grow) {
        begin = pool_rows_;
        children_.reserve(total);
        slots_.reserve(total);
    } else {
        slots_.clear();
        Clear();
        ResetCellEditor();
        children_.reserve(total);
        slots_.reserve(total);
    }
    pool_rows_ = rows;
    pool_fp_ = fp;

    for (size_t ri = begin; ri < rows; ++ri) {
        (void)ri;
        for (size_t k = 0; k < n_icols; ++k) {
            const size_t col = icols[k];
            const size_t slot_i = slots_.size();
            Control* ctl = nullptr;
            switch (columns_[col].kind) {
            case CellKind::CheckBox: {
                auto& box = Add<CheckBox>(L"");
                ctl = &box;
                box.OnToggled([this, slot_i](bool) {
                    if (slot_i >= slots_.size()) return;
                    auto* box = dynamic_cast<CheckBox*>(slots_[slot_i].control);
                    if (!box) return;
                    const ptrdiff_t row = slots_[slot_i].row;
                    const size_t c = slots_[slot_i].col;
                    if (row < 0 || c >= columns_.size() || !columns_[c].cb_set) return;
                    columns_[c].cb_set(DataRowAt(static_cast<size_t>(row)), box->Checked());
                });
                break;
            }
            case CellKind::Button: {
                auto& btn = Add<Button>(columns_[col].btn_caption, ButtonKind::Standard);
                btn.SizeClass(ButtonSize::Small);
                ctl = &btn;
                btn.OnClick([this, slot_i] {
                    if (slot_i >= slots_.size()) return;
                    const ptrdiff_t row = slots_[slot_i].row;
                    const size_t c = slots_[slot_i].col;
                    if (row < 0 || c >= columns_.size() || !columns_[c].btn_click) return;
                    columns_[c].btn_click(DataRowAt(static_cast<size_t>(row)));
                });
                break;
            }
            case CellKind::TextBox: {
                auto& tb = Add<TableTextBox>();
                ctl = &tb;
                tb.OnTextChanged([this, slot_i](std::wstring_view) {
                    if (slot_i >= slots_.size()) return;
                    auto* tb = dynamic_cast<TextBox*>(slots_[slot_i].control);
                    if (!tb) return;
                    const ptrdiff_t row = slots_[slot_i].row;
                    const size_t c = slots_[slot_i].col;
                    if (row < 0 || c >= columns_.size() || !columns_[c].tb_set) return;
                    columns_[c].tb_set(DataRowAt(static_cast<size_t>(row)), tb->Text());
                });
                break;
            }
            default:
                break;
            }
            if (ctl) {
                const size_t idx = ChildCount() - 1;
                SetChildVisibility(idx, false);
                slots_.push_back(Slot{ctl, col, -1, idx});
            }
        }
    }
}

void Table::SyncSlots() {
    if (absolute_.IsEmpty()) return;
    EnsurePool();
    if (slots_.empty()) return;

    size_t icols[kMaxColumns];
    const size_t n_icols = FillInteractive(icols, kMaxColumns);
    if (n_icols == 0) return;

    const float row_h = std::max(RowHeight(), 1.0f);
    const float body_h = BodyHeight();

    float xs[kMaxColumns]{};
    float ws[kMaxColumns]{};
    float frozen_width = 0.0f;
    ColumnMetrics(absolute_.w, xs, ws, columns_.size(), &frozen_width, nullptr);

    const float right_limit = absolute_.w - kBarHit;
    size_t slot_i = 0;
    bool full = false;
    const auto bind_row = [&](ptrdiff_t row, float y_local) {
        if (full) return;
        for (size_t k = 0; k < n_icols; ++k) {
            const size_t col = icols[k];
            if (slot_i >= slots_.size()) {
                full = true;
                return;
            }
            Slot& slot = slots_[slot_i++];
            slot.col = col;
            slot.row = row;
            Control* ctl = slot.control;
            if (!ctl) continue;

            const size_t idx = slot.child_index;
            if (idx >= ChildCount()) continue;

            const float left_limit = columns_[col].frozen ? 0.0f : frozen_width;
            float cell_x = std::max(xs[col] + kCellPadX, left_limit + kCellPadX);
            const float cell_right = std::min(xs[col] + ws[col] - kCellPadX, right_limit);
            float cell_w = std::max(0.0f, cell_right - cell_x);
            if (cell_w < 1.0f) {
                SetChildVisibility(idx, false);
                slot.row = -1;
                continue;
            }

            float ctrl_h = std::max(1.0f, row_h - kCellInsetY * 2.0f); // NOLINT
            float ctrl_w = cell_w;
            float ctrl_x = cell_x;
            const float ctrl_y = y_local + (row_h - ctrl_h) * 0.5f;
            if (columns_[col].kind == CellKind::CheckBox) {
                ctrl_w = 20.0f;
                ctrl_x = cell_x + (cell_w - ctrl_w) * 0.5f;
            }

            if (!SlotMatchesKind(ctl, columns_[col].kind)) {
                SetChildVisibility(idx, false);
                slot.row = -1;
                continue;
            }

            SetChildVisibility(idx, true);
            SetChildBounds(*ctl, {ctrl_x, ctrl_y, ctrl_w, ctrl_h});
            ArrangeChildAt(idx);

            switch (columns_[col].kind) {
            case CellKind::CheckBox:
                if (auto* box = dynamic_cast<CheckBox*>(ctl)) {
                    const bool on = columns_[col].cb_get
                                        ? columns_[col].cb_get(DataRowAt(static_cast<size_t>(row)))
                                        : false;
                    if (box->Checked() != on) box->Checked(on);
                }
                break;
            case CellKind::Button:
                if (auto* btn = dynamic_cast<Button*>(ctl)) {
                    if (btn->Text() != columns_[col].btn_caption) btn->Text(columns_[col].btn_caption);
                }
                break;
            case CellKind::TextBox:
                if (auto* tb = dynamic_cast<TextBox*>(ctl)) {
                    if (!tb->HasFocus()) {
                        const std::wstring text =
                            columns_[col].tb_get
                                ? columns_[col].tb_get(DataRowAt(static_cast<size_t>(row)))
                                : std::wstring{};
                        if (tb->Text() != text) tb->Text(text);
                    }
                }
                break;
            default:
                break;
            }
        }
    };

    if (groups_.empty()) {
        const ptrdiff_t first = static_cast<ptrdiff_t>(scroll_offset_ / row_h);
        const ptrdiff_t visible = std::max(ptrdiff_t{1}, static_cast<ptrdiff_t>(body_h / row_h) + 2);
        const ptrdiff_t last = std::min(first + visible, static_cast<ptrdiff_t>(row_count_));
        for (ptrdiff_t row = std::max(first, ptrdiff_t{0}); row < last; ++row) {
            const float y_local = BodyTop() + static_cast<float>(row) * row_h - scroll_offset_;
            bind_row(row, y_local);
        }
    } else {
        float cursor = 0.0f;
        const float body_top = BodyTop();
        const float body_bottom = body_top + body_h;
        for (const Group& g : groups_) {
            cursor += GroupBand();
            if (!g.expanded) continue;
            for (size_t i = 0; i < g.count; ++i) {
                const float y_local = body_top + cursor + static_cast<float>(i) * row_h - scroll_offset_;
                if (y_local + row_h < body_top) continue;
                if (y_local > body_bottom) break;
                bind_row(static_cast<ptrdiff_t>(g.start + i), y_local);
            }
            cursor += static_cast<float>(g.count) * row_h;
        }
    }

    while (slot_i < slots_.size()) {
        Slot& slot = slots_[slot_i++];
        slot.row = -1;
        if (!slot.control) continue;
        const size_t idx = slot.child_index;
        if (idx < ChildCount()) SetChildVisibility(idx, false);
    }
}

bool Table::OnKey(uint32_t vk) {
    const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    if (ctrl && (vk == 'C' || vk == 'c')) {
        CopySelection();
        return true;
    }
    if (ctrl && vk == 'V' && cell_edit_enabled_ && selected_ >= 0) {
        PasteRow(DataRowAt(static_cast<size_t>(selected_)), active_col_, clipboard::Text());
        return true;
    }
    if (vk == VK_F2) {
        if (selected_ >= 0) BeginCellEdit(selected_, active_col_);
        return true;
    }
    if (vk == VK_LEFT) {
        MoveActiveColumn(-1);
        return true;
    }
    if (vk == VK_RIGHT) {
        MoveActiveColumn(1);
        return true;
    }
    if (vk == VK_TAB) {
        MoveActiveColumn(shift ? -1 : 1);
        return true;
    }
    if (vk == VK_SPACE || vk == VK_RETURN) {
        if (selected_ < 0 || active_col_ < 0 ||
            active_col_ >= static_cast<int>(columns_.size())) {
            return vk == VK_SPACE;
        }
        const ColumnDef& col = columns_[static_cast<size_t>(active_col_)];
        const size_t data = DataRowAt(static_cast<size_t>(selected_));
        if (col.kind == CellKind::CheckBox && col.cb_get && col.cb_set) {
            col.cb_set(data, !col.cb_get(data));
            SyncSlots();
            Invalidate();
            return true;
        }
        if (vk == VK_RETURN && col.kind == CellKind::Button && col.btn_click) {
            col.btn_click(data);
            return true;
        }
        if (vk == VK_RETURN && col.kind == CellKind::Text && cell_edit_enabled_) {
            BeginCellEdit(selected_, active_col_);
            return true;
        }
    }
    const float row_h = std::max(RowHeight(), 1.0f);
    const ptrdiff_t page = std::max(ptrdiff_t{1}, static_cast<ptrdiff_t>(BodyHeight() / row_h));
    switch (vk) {
    case VK_DOWN: MoveSelection(1); return true;
    case VK_UP: MoveSelection(-1); return true;
    case VK_PRIOR: MoveSelection(-page); return true;
    case VK_NEXT: MoveSelection(page); return true;
    case VK_HOME:
        if (ctrl) {
            for (size_t i = 0; i < columns_.size(); ++i) {
                const size_t c = (visual_.size() == columns_.size()) ? visual_[i] : i;
                if (columns_[c].visible) {
                    ActiveColumn(static_cast<int>(c));
                    break;
                }
            }
        }
        if (row_count_) SelectedIndex(0);
        return true;
    case VK_END:
        if (ctrl) {
            for (size_t i = columns_.size(); i > 0; --i) {
                const size_t c = (visual_.size() == columns_.size()) ? visual_[i - 1] : (i - 1);
                if (columns_[c].visible) {
                    ActiveColumn(static_cast<int>(c));
                    break;
                }
            }
        }
        if (row_count_) SelectedIndex(static_cast<ptrdiff_t>(row_count_) - 1);
        return true;
    default: return false;
    }
}

void Table::MoveActiveColumn(int delta) {
    if (columns_.empty() || delta == 0) return;
    const int n = static_cast<int>(columns_.size());
    int col = active_col_;
    for (int step = 0; step < n; ++step) {
        col += delta > 0 ? 1 : -1;
        if (col < 0) {
            col = n - 1;
            if (selected_ > 0) SelectedIndex(selected_ - 1);
        } else if (col >= n) {
            col = 0;
            if (selected_ >= 0 && selected_ + 1 < static_cast<ptrdiff_t>(row_count_)) {
                SelectedIndex(selected_ + 1);
            }
        }
        if (col >= 0 && col < n && columns_[static_cast<size_t>(col)].visible) {
            ActiveColumn(col);
            return;
        }
    }
}

void Table::EnsureColumnVisible(int col) {
    if (col < 0 || col >= static_cast<int>(columns_.size())) return;
    if (columns_[static_cast<size_t>(col)].frozen) return;
    float xs[kMaxColumns]{};
    float ws[kMaxColumns]{};
    float frozen_width = 0.0f;
    ColumnMetrics(absolute_.w, xs, ws, columns_.size(), &frozen_width, nullptr);
    const float left = xs[static_cast<size_t>(col)];
    const float right = left + ws[static_cast<size_t>(col)];
    const float view_left = frozen_width;
    const float view_right = absolute_.w;
    if (right <= view_left + 1.0f) {
        horizontal_target_ = std::max(0.0f, horizontal_offset_ - (view_left - left + 8.0f));
        Animate();
    } else if (left >= view_right - 1.0f) {
        horizontal_target_ = horizontal_offset_ + (right - view_right + 8.0f);
        ClampScroll();
        Animate();
    }
}

bool Table::CellTextAt(size_t data_row, size_t col, std::wstring& out) const {
    if (col >= columns_.size()) return false;
    const ColumnDef& c = columns_[col];
    if (c.display_get) { c.display_get(data_row, out); return true; }
    switch (c.kind) {
    case CellKind::Text:
        if (c.text_get) { c.text_get(data_row, out); return true; }
        if (c.numeric) {
            if (!c.num_get) return false;
            FormatNumber(c.num_get(data_row), c.precision, out);
            return true;
        }
        if (!cell_text_) return false;
        cell_text_(data_row, col, out);
        return true;
    case CellKind::CheckBox:
        out = (c.cb_get && c.cb_get(data_row)) ? L"1" : L"0";
        return true;
    case CellKind::Button:
        out = c.btn_caption;
        return true;
    case CellKind::TextBox:
        out = c.tb_get ? c.tb_get(data_row) : std::wstring{};
        return true;
    case CellKind::Progress:
        if (!c.prog_get) return false;
        out = std::to_wstring(static_cast<int>(Clamp(c.prog_get(data_row), 0.0f, 1.0f) * 100.0f));
        return true;
    case CellKind::Icon:
        if (!c.icon_get) return false;
        c.icon_get(data_row, out);
        return true;
    }
    return false;
}

bool Table::CopySelection() const {
    if (selected_ < 0) return false;
    const size_t data = DataRowAt(static_cast<size_t>(selected_));
    std::wstring line;
    bool first = true;
    const size_t n = columns_.size();
    auto emit = [&](size_t c) {
        if (!columns_[c].visible) return;
        std::wstring cell;
        CellTextAt(data, c, cell);
        if (!first) line += L'\t';
        first = false;
        line += cell;
    };
    if (visual_.size() == n) {
        for (size_t c : visual_) emit(c);
    } else {
        for (size_t c = 0; c < n; ++c) emit(c);
    }
    return clipboard::Text(line);
}

void Table::MarkFooterRows(size_t index, size_t count) {
    if (!draw_cache_ || !count) return;
    auto& cache = *draw_cache_;
    const size_t end = index + std::min(count, row_count_ > index ? row_count_ - index : size_t{0});
    if (!cache.footer_dirty) { cache.footer_begin = index; cache.footer_end = end; }
    else { cache.footer_begin = std::min(cache.footer_begin, index); cache.footer_end = std::max(cache.footer_end, end); }
    cache.footer_dirty = true;
}

std::wstring Table::FooterText(size_t col) const {
    if (col >= columns_.size() || !draw_cache_) return {};
    const auto& column = columns_[col];
    const auto kind = column.aggregate;
    if (kind == ColumnAggregate::None) return {};
    if (kind == ColumnAggregate::Count) return std::to_wstring(row_count_);
    auto& cache = *draw_cache_;
    cache.aggregates.resize(columns_.size());
    auto& aggregate = cache.aggregates[col];
    const bool rebuild = cache.footer_full || aggregate.kind != kind || aggregate.values.size() != row_count_;
    if (rebuild) {
        aggregate = DrawCache::AggregateState{};
        aggregate.kind = kind;
        aggregate.values.resize(row_count_);
    }
    const bool extrema = kind == ColumnAggregate::Min || kind == ColumnAggregate::Max;
    const size_t first = rebuild ? 0 : std::min(cache.footer_begin, row_count_);
    const size_t last = rebuild ? row_count_ : std::min(cache.footer_end, row_count_);
    for (size_t data = first; data < last; ++data) {
        auto& old = aggregate.values[data];
        if (!rebuild) {
            if (old.numeric) {
                aggregate.sum -= old.number; --aggregate.count;
                if (extrema) { auto it = aggregate.numbers.find(old.number); if (it != aggregate.numbers.end()) aggregate.numbers.erase(it); }
            } else if (extrema && !old.text.empty()) {
                auto it = aggregate.strings.find(old.text); if (it != aggregate.strings.end()) aggregate.strings.erase(it);
            }
        }
        old.numeric = false; old.text.clear();
        if (column.numeric && column.num_get) {
            old.number = column.num_get(data); old.numeric = std::isfinite(old.number);
        } else if (column.kind == CellKind::Progress && column.prog_get) {
            old.number = column.prog_get(data); old.numeric = std::isfinite(old.number);
        } else {
            CellTextAt(data, col, old.text);
            old.numeric = StrictParseNumber(old.text, old.number);
        }
        if (old.numeric) {
            aggregate.sum += old.number; ++aggregate.count;
            if (extrema) aggregate.numbers.insert(old.number);
        } else if (extrema && !old.text.empty()) aggregate.strings.insert(old.text);
    }
    if (!aggregate.count) {
        if (aggregate.strings.empty()) return {};
        return kind == ColumnAggregate::Min ? *aggregate.strings.begin() : *aggregate.strings.rbegin();
    }
    double value = aggregate.sum;
    if (kind == ColumnAggregate::Average) value /= static_cast<double>(aggregate.count);
    else if (kind == ColumnAggregate::Min) value = *aggregate.numbers.begin();
    else if (kind == ColumnAggregate::Max) value = *aggregate.numbers.rbegin();
    std::wstring out;
    if (column.numeric || column.numeric_text) FormatNumber(value, column.precision, out);
    else { wchar_t buf[48]; swprintf_s(buf, L"%.4g", value); out = buf; }
    return out;
}

uint64_t Table::ColumnFingerprint() const noexcept {
    uint64_t cols = columns_.size() ^ (visual_.size() << 8) ^ (static_cast<uint64_t>(group_col_) << 16);
    cols ^= footer_ ? 0x1000000ULL : 0;
    for (size_t i = 0; i < columns_.size(); ++i) {
        const ColumnDef& col = columns_[i];
        const size_t vis = (i < visual_.size()) ? visual_[i] : i;
        cols ^= vis + 1;
        cols = (cols << 5) | (cols >> 59);
        cols ^= static_cast<uint64_t>(col.width * 64.0f + 1.0f);
        for (float value : {col.minimum, col.preferred, col.maximum, col.weight}) {
            cols ^= std::bit_cast<uint32_t>(value);
            cols = (cols << 5) | (cols >> 59);
        }
        cols ^= col.frozen ? 0x9e3779b97f4a7c15ULL : 0;
        cols ^= col.visible ? 0x85ebca77c2b2ae63ULL : 0;
        cols ^= static_cast<uint64_t>(static_cast<int>(col.kind));
        cols ^= static_cast<uint64_t>(static_cast<int>(col.aggregate)) << 8;
        for (const TableSortKey& key : sort_keys_) {
            cols ^= static_cast<uint64_t>(key.col + 1) * 0x9e3779b97f4a7c15ULL;
            cols ^= static_cast<uint64_t>(key.direction + 2);
        }
    }
        for (const Group& g : groups_) {
            cols ^= (g.count + 1) * (g.expanded ? 3ULL : 7ULL);
            cols = (cols << 3) | (cols >> 61);
        }
        return cols;
}

bool Table::ShowContextMenu(Point window_dip) {
    const Point local{window_dip.x - absolute_.x, window_dip.y - absolute_.y};
    if (local.y >= 0.0f && local.y < HeaderHeight() && local.x >= 0.0f && local.x <= absolute_.w) {
        PopupColumnMenu(window_dip);
        return true;
    }
    return Control::ShowContextMenu(window_dip);
}

void Table::PopupColumnMenu(Point window_dip) {
    if (!window_) return;
    size_t shown = 0;
    for (const ColumnDef& c : columns_) {
        if (c.visible) ++shown;
    }
    Menu menu;
    auto add = [&](size_t c) {
        const bool last_on = columns_[c].visible && shown <= 1;
        auto& item = menu.AddItem(columns_[c].title.empty() ? L"(untitled)" : columns_[c].title,
                                  [this, c] { ColumnVisible(static_cast<int>(c), !columns_[c].visible); });
        item.Checked(columns_[c].visible);
        if (last_on) item.Disabled(true);
    };
    if (visual_.size() == columns_.size()) {
        for (size_t c : visual_) add(c);
    } else {
        for (size_t c = 0; c < columns_.size(); ++c) add(c);
    }
    menu.Popup(*window_, window_dip);
}

void Table::OnMouseEnter() {
    Control::OnMouseEnter();
    Animate();
}

void Table::OnMouseDown(Point local, uint32_t buttons) {
    if (!(buttons & 0x0001)) return;
    Focus();
    if (BeginScrollDrag(local)) return;
    if (local.y < HeaderHeight()) {
        const int col = ResizeBoundaryAt(local);
        if (col >= 0) {
            float xs[kMaxColumns]{};
            float ws[kMaxColumns]{};
            ColumnMetrics(absolute_.w, xs, ws, columns_.size());
            SnapFlexToPixels();
            ColumnMetrics(absolute_.w, xs, ws, columns_.size());
            resize_col_ = col;
            resize_start_x_ = local.x;
            resize_start_w_ = ws[static_cast<size_t>(col)];
            columns_[static_cast<size_t>(col)].width = resize_start_w_;
            hover_split_ = col;
            header_press_col_ = -1;
            Invalidate();
            return;
        }
        const int pin = HeaderPinAt(local);
        if (pin >= 0) {
            ColumnFrozen(pin, !ColumnFrozen(pin));
            header_press_col_ = -1;
            return;
        }
        header_press_col_ = ColumnAt(local.x);
        header_press_x_ = local.x;
        reorder_dragging_ = false;
        drop_col_ = -1;
        return;
    }
    const ptrdiff_t group = GroupAt(local);
    if (group >= 0 && (local.y - BodyTop() < GroupBand() || RowAt(local) < 0)) {
        GroupExpanded(static_cast<size_t>(group), !GroupExpanded(static_cast<size_t>(group)));
        return;
    }
    const ptrdiff_t row = RowAt(local);
    if (row >= 0) {
        SelectedIndex(row);
        const int col = ColumnAt(local.x);
        if (col >= 0) ActiveColumn(col);
    }
}

void Table::OnMouseMove(Point local, uint32_t buttons) {
    (void)buttons;
    if (dragging_) {
        const ScrollThumb thumb = Thumb(1.0f);
        const float thumb_h = thumb.visible ? thumb.rect.h : 20.0f;
        const float track = std::max(1.0f, BodyHeight() - thumb_h);
        const float local_body_y = local.y - BodyTop();
        const float t = Clamp((local_body_y - drag_grab_) / track, 0.0f, 1.0f);
        scroll_offset_ = target_offset_ = t * MaxScroll();
        SyncSlots();
        Invalidate();
        return;
    }
    if (horizontal_dragging_) {
        const ScrollThumb thumb = HorizontalThumb(1.0f);
        const float thumb_w = thumb.visible ? thumb.rect.w : 20.0f;
        const Rect track_rect = HorizontalTrack();
        const float track = std::max(1.0f, track_rect.w - thumb_w);
        const float track_x = track_rect.x - absolute_.x;
        const float t = Clamp((local.x - track_x - drag_grab_) / track, 0.0f, 1.0f);
        horizontal_offset_ = horizontal_target_ = t * MaxHorizontalScroll();
        SyncSlots();
        Invalidate();
        return;
    }
    if (resize_col_ >= 0) {
        auto& column = columns_[static_cast<size_t>(resize_col_)];
        column.width = Clamp(resize_start_w_ + (local.x - resize_start_x_),
                             column.minimum > 0.0f ? column.minimum : kMinColWidth, column.maximum);
        ClampScroll();
        SyncSlots();
        Invalidate();
        return;
    }
    if (header_press_col_ >= 0 && local.y < HeaderHeight()) {
        if (!reorder_dragging_ && std::fabs(local.x - header_press_x_) > kReorderSlop) {
            reorder_dragging_ = true;
        }
        if (reorder_dragging_) {
            drop_col_ = ColumnAt(local.x);
            Invalidate();
            return;
        }
    }
    const int split = local.y < HeaderHeight() ? ResizeBoundaryAt(local) : -1;
    if (split != hover_split_) {
        hover_split_ = split;
        Invalidate();
    }
    const ptrdiff_t row = RowAt(local);
    const int col = row >= 0 ? ColumnAt(local.x) : -1;
    const bool cell_changed = row != hover_row_ || (row >= 0 && col != hover_col_);
    if (cell_changed) {
        hover_row_ = row;
        hover_col_ = col;
        Animate();
        Invalidate();
        cell_hover_.Emit(row >= 0 ? static_cast<ptrdiff_t>(DataRowAt(static_cast<size_t>(row))) : -1,
                         col, row >= 0);
    }
    // 换格必须通知窗口层丢弃旧气泡快照，否则同一 Table 内会继续显示旧单元格提示。
    if (cell_tooltip_) {
        std::wstring next;
        if (row >= 0 && col >= 0 && static_cast<size_t>(row) < row_count_)
            cell_tooltip_(DataRowAt(static_cast<size_t>(row)), col, next);
        if (cell_changed || next != tooltip_)
            ToolTip(next);
    }
}

void Table::OnMouseUp(Point, uint32_t) {
    UpdateScope update;
    if (reorder_dragging_ && header_press_col_ >= 0 && drop_col_ >= 0) {
        MoveColumn(header_press_col_, drop_col_);
    } else if (header_press_col_ >= 0 && !reorder_dragging_ && Sortable(header_press_col_)) {
        const int clicked = header_press_col_;
        const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        int dir = 1;
        if (shift) {
            dir = 1;
            for (const TableSortKey& key : sort_keys_) {
                if (key.col != clicked) continue;
                dir = key.direction > 0 ? -1 : 0;
                break;
            }
            if (dir == 0) SortBy(clicked, 0, false);
            else SortBy(clicked, dir, true);
        } else {
            dir = sort_col_ == clicked ? (sort_dir_ > 0 ? -1 : 0) : 1;
            SortBy(clicked, dir, false);
        }
    }
    dragging_ = false;
    horizontal_dragging_ = false;
    if (resize_col_ >= 0) columns_changed_.EmitDeferred();
    resize_col_ = -1;
    header_press_col_ = -1;
    reorder_dragging_ = false;
    drop_col_ = -1;
    Animate();
    Invalidate();
}

void Table::OnMouseLeave() {
    Control::OnMouseLeave();
    const bool had_hover = hover_row_ >= 0;
    hover_row_ = -1;
    hover_col_ = -1;
    if (cell_tooltip_) ToolTip(L"");
    hover_split_ = -1;
    Animate();
    Invalidate();
    if (had_hover) cell_hover_.Emit(-1, -1, false);
}

Rect Table::ToolTipAnchor() const {
    if (hover_row_ < 0 || hover_col_ < 0 || columns_.empty() ||
        static_cast<size_t>(hover_row_) >= row_count_) {
        return absolute_;
    }
    float xs[kMaxColumns];
    float ws[kMaxColumns];
    ColumnMetrics(absolute_.w, xs, ws, columns_.size());
    const size_t col = std::min(static_cast<size_t>(hover_col_), columns_.size() - 1);
    const float top = RowTop(static_cast<size_t>(hover_row_)) - scroll_offset_;
    return {absolute_.x + xs[col], absolute_.y + BodyTop() + top, ws[col], RowHeight()};
}

bool Table::OnWheel(float delta) {
    // 业务方优先（滚轮切换选区等）；未消费才走表格滚动。
    if (wheel_scroll_ && wheel_scroll_(delta)) return true;
    if ((GetKeyState(VK_SHIFT) & 0x8000) != 0 && MaxHorizontalScroll() > 0.0f) {
        if ((delta > 0.0f && horizontal_target_ <= 0.0f) ||
            (delta < 0.0f && horizontal_target_ >= MaxHorizontalScroll())) return false;
        horizontal_target_ = Clamp(horizontal_target_ - delta * kHorizontalWheel, 0.0f,
                                   MaxHorizontalScroll());
        Animate();
        return true;
    }
    if (MaxScroll() <= 0.0f) return false;
    if ((delta > 0.0f && target_offset_ <= 0.0f) ||
        (delta < 0.0f && target_offset_ >= MaxScroll())) return false;
    const float row_h = std::max(RowHeight(), 1.0f);
    target_offset_ = Clamp(target_offset_ - delta * row_h * 2.5f, 0.0f, MaxScroll());
    Animate();
    return true;
}

bool Table::OnHWheel(float delta) {
    const float next = Clamp(horizontal_target_ + delta * kHorizontalWheel, 0.0f, MaxHorizontalScroll());
    if (next == horizontal_target_) return false;
    horizontal_target_ = next;
    Animate();
    return true;
}

Size Table::ScrollbarOccupancy() const {
    return {MaxScroll() > 0.5f ? kBarHit : 0.0f, MaxHorizontalScroll() > 0.5f ? kBarHit : 0.0f};
}

void Table::Prepare(Painter& painter, const Theme& theme) {
    if (!draw_cache_) draw_cache_ = std::make_unique<DrawCache>();
    auto& cache = *draw_cache_;
    uint64_t stamp = 1469598103934665603ULL;
    const auto hash = [&](float v) { stamp = (stamp ^ std::bit_cast<uint32_t>(v)) * 1099511628211ULL; };
    for (Color color : {theme.fill_input, theme.fill_input_hover, theme.stroke_divider, theme.fill_selected,
                        theme.accent, theme.fill_hover, theme.text, theme.text_secondary, theme.text_disabled,
                        theme.control_stroke}) { hash(color.r); hash(color.g); hash(color.b); hash(color.a); }
    hash(theme.radius_control); hash(theme.list_row_height); hash(RowHeight()); hash(HeaderHeight());
    if (cache.theme_stamp != stamp) { cache.list.reset(); cache.theme_stamp = stamp; }
    if (cache.pending_device != painter.DeviceContext()) {
        cache.pending.reset(); cache.list.reset(); cache.pending_device = painter.DeviceContext();
    }
    cache.cell_count = 0;
    flow_active_ = false;
    if (absolute_.IsEmpty() || columns_.empty()) return;
    if (custom_row_height_ <= 0.5f) theme_row_height_ = theme.list_row_height;
    ClampScroll();
    for (Color color : {theme.fill_input, theme.fill_input_hover, theme.stroke_divider, theme.fill_selected,
                        theme.accent, theme.fill_hover, theme.text, theme.text_secondary, theme.text_disabled,
                        theme.control_stroke}) painter.PrepareColor(color);
    const size_t n = columns_.size();
    float xs[kMaxColumns]{}, ws[kMaxColumns]{};
    ColumnMetrics(absolute_.w, xs, ws, n);
    const float row_h = std::max(RowHeight(), 1.0f);
    const float body_y = absolute_.y + std::min(HeaderHeight(), absolute_.h);
    const float header_h = std::min(HeaderHeight(), absolute_.h);
    for (size_t c = 0; c < n; ++c) {
        if (ws[c] < .5f) continue;
        int rank = -1;
        for (size_t k = 0; k < sort_keys_.size(); ++k) if (sort_keys_[k].col == static_cast<int>(c)) rank = static_cast<int>(k);
        painter.PrepareText(columns_[c].title,
            {absolute_.x + xs[c] + kCellPadX, absolute_.y, ws[c] - kCellPadX * 2 - kPinSlot, header_h},
            TextRole::CaptionStrong, rank >= 0 ? theme.text : theme.text_secondary);
        if (rank >= 0 && sort_keys_.size() > 1) {
            const wchar_t text[] = {static_cast<wchar_t>(L'1' + rank), 0};
            painter.PrepareText(text, {absolute_.x + xs[c] + ws[c] - kPinSlot - 22,
                absolute_.y + 4, 12, 12}, TextRole::Overline, theme.text_secondary);
        }
        painter.PrepareIcon(icon::kPin, {absolute_.x + xs[c] + ws[c] - kPinSlot,
            absolute_.y + (header_h - 14) * .5f, 14, 14}, 12, columns_[c].frozen ? theme.text : theme.text_disabled);
    }
    const auto row = [&](ptrdiff_t r, float top) {
        const size_t data = DataRowAt(static_cast<size_t>(r));
        for (size_t c = 0; c < n; ++c) {
            if (ws[c] <= kCellPadX * 2 || xs[c] + ws[c] < -1 || xs[c] > absolute_.w + 1) continue;
            if (cache.cell_count == cache.cells.size()) cache.cells.emplace_back();
            auto& cell = cache.cells[cache.cell_count++];
            cell.row = r; cell.col = c; cell.text.clear(); cell.segments.clear();
            cell.width = 0; cell.progress = 0; cell.flow = false;
            const auto& column = columns_[c];
            if (column.kind == CellKind::Progress && column.prog_get) cell.progress = Clamp(column.prog_get(data), 0.0f, 1.0f);
            if (column.kind == CellKind::Icon && column.icon_get) {
                column.icon_get(data, cell.text);
                painter.PrepareIcon(cell.text, {absolute_.x + xs[c] + kCellPadX,
                    body_y + top - scroll_offset_ + (row_h - 14) * .5f, 14, 14}, 14, theme.text);
            }
            if (column.kind != CellKind::Text) continue;
            CellTextAt(data, c, cell.text);
            cell.flow = !cell.text.empty() && cell_flow_highlight_ && cell_flow_highlight_(data, static_cast<int>(c));
            flow_active_ = flow_active_ || cell.flow;
            const Rect rect{absolute_.x + xs[c] + kCellPadX, body_y + top - scroll_offset_, ws[c] - kCellPadX * 2, row_h};
            if (cell_font_chars_.empty() || cell_font_family_.empty()) {
                cell.width = painter.AdvanceText(cell.text, TextRole::Caption);
                painter.PrepareText(cell.text, rect, TextRole::Caption, theme.text,
                                    (column.numeric || column.numeric_text) ? Align::Trailing : Align::Leading);
            } else {
                for (size_t begin = 0; begin < cell.text.size();) {
                    const bool custom = cell_font_chars_.find(cell.text[begin]) != std::wstring::npos;
                    size_t end = begin + 1;
                    while (end < cell.text.size() && (cell_font_chars_.find(cell.text[end]) != std::wstring::npos) == custom) ++end;
                    const std::wstring_view segment(cell.text.data() + begin, end - begin);
                    const auto measure = [&] {
                        const float width = painter.AdvanceText(segment, TextRole::Caption);
                        if (cell.width < rect.w) painter.PrepareText(segment,
                            {rect.x + cell.width, rect.y, rect.w - cell.width, rect.h}, TextRole::Caption, theme.text);
                        return width;
                    };
                    float advance;
                    if (custom) { FontFamilyScope font(cell_font_family_); advance = measure(); }
                    else advance = measure();
                    cell.segments.push_back({begin, end - begin, advance, custom});
                    cell.width += advance;
                    begin = end;
                }
            }
        }
    };
    const auto range = [&](size_t start, size_t count, float top) {
        const ptrdiff_t first = std::max(ptrdiff_t{0}, static_cast<ptrdiff_t>(std::floor((scroll_offset_ - top) / row_h)));
        const ptrdiff_t last = std::min(static_cast<ptrdiff_t>(count), static_cast<ptrdiff_t>(std::ceil((scroll_offset_ + BodyHeight() - top) / row_h)) + 1);
        for (ptrdiff_t i = first; i < last; ++i) row(static_cast<ptrdiff_t>(start) + i, top + static_cast<float>(i) * row_h);
    };
    if (groups_.empty()) range(0, row_count_, 0);
    else {
        float top = 0;
        const Group* sticky = nullptr;
        float next_header = body_y + BodyHeight();
        for (const auto& group : groups_) {
            const float y = body_y + top - scroll_offset_;
            if (y <= body_y + .5f) sticky = &group;
            else {
                if (sticky && next_header == body_y + BodyHeight()) next_header = y;
                if (y <= body_y + BodyHeight()) painter.PrepareText(group.key,
                    {absolute_.x + 26, y, absolute_.w - 40, GroupBand()}, TextRole::CaptionStrong, theme.text);
            }
            top += GroupBand();
            if (group.expanded) { range(group.start, group.count, top); top += static_cast<float>(group.count) * row_h; }
        }
        if (sticky) painter.PrepareText(sticky->key,
            {absolute_.x + 26, std::min(body_y, next_header - GroupBand()), absolute_.w - 40, GroupBand()},
            TextRole::CaptionStrong, theme.text);
    }
    const bool refresh_footer = cache.footer_dirty || cache.footers.size() != n;
    cache.footers.resize(n);
    for (size_t c = 0; c < n; ++c) {
        if (refresh_footer) cache.footers[c] = footer_ ? FooterText(c) : std::wstring{};
        const Rect footer = FooterRect();
        if (footer_ && ws[c] > kCellPadX * 2) painter.PrepareText(cache.footers[c],
            {footer.x + xs[c] + kCellPadX, footer.y, ws[c] - kCellPadX * 2, footer.h}, TextRole::Caption, theme.text_secondary);
    }
    cache.footer_dirty = cache.footer_full = false;
    cache.footer_begin = cache.footer_end = 0;
    if (flow_active_ && MotionScale() > 0.0f) Animate();
    if (painter.CanRecordCommandList() && painter.DeviceContext() && !cache.pending)
        painter.DeviceContext()->CreateCommandList(&cache.pending);
}

void Table::Draw(Painter& painter, const Theme& theme) {
    if (absolute_.IsEmpty() || columns_.empty()) return;
    if (custom_row_height_ <= 0.5f) theme_row_height_ = theme.list_row_height;
    ClampScroll();

    // ink=false: fills/lines/phosphor (safe to record). ink=true: DrawText/progress/cell
    // icons, always live so model updates cannot replay a stale command list.
    const auto paint = [&](bool ink) {
    if (!ink) painter.FillRoundedRect(absolute_, theme.radius_control, theme.fill_input);

    const size_t n = std::min(columns_.size(), kMaxColumns);
    float xs[kMaxColumns]{};
    float ws[kMaxColumns]{};
    float frozen_width = 0.0f;
    ColumnMetrics(absolute_.w, xs, ws, n, &frozen_width, nullptr);
    frozen_width = std::min(frozen_width, absolute_.w);

    const auto draw_header_column = [&](size_t c, const Rect& header) {
        if (ws[c] < 0.5f) return;
        const float cell_w = std::max(0.0f, ws[c] - kCellPadX * 2.0f - kPinSlot);
        int sort_rank = -1;
        int sort_dir = 1;
        for (size_t k = 0; k < sort_keys_.size(); ++k) {
            if (sort_keys_[k].col != static_cast<int>(c)) continue;
            sort_rank = static_cast<int>(k);
            sort_dir = sort_keys_[k].direction;
            break;
        }
        if (ink) {
            if (cell_w > 0.5f && !columns_[c].title.empty()) {
                painter.DrawText(columns_[c].title,
                                 {absolute_.x + xs[c] + kCellPadX, header.y, cell_w, header.h},
                                 TextRole::CaptionStrong,
                                 sort_rank >= 0 ? theme.text : theme.text_secondary);
            }
            if (sort_rank >= 0 && sort_keys_.size() > 1) {
                const wchar_t rank[] = {static_cast<wchar_t>(L'1' + sort_rank), 0};
                painter.DrawText(rank,
                                 {absolute_.x + xs[c] + ws[c] - kPinSlot - 22.0f,
                                  header.y + 4.0f, 12.0f, 12.0f},
                                 TextRole::Overline, theme.text_secondary);
            }
            return;
        }
        if (sort_rank >= 0) {
            painter.DrawChevron({absolute_.x + xs[c] + ws[c] - kPinSlot - 8.0f,
                                 header.y + header.h * 0.5f},
                                8.0f, sort_dir > 0 ? 180.0f : 0.0f, theme.text_secondary, 1.4f);
        }
        if (ws[c] >= kPinSlot + 4.0f) {
            const bool pinned = columns_[c].frozen;
            const Color pin = pinned ? theme.text : theme.text_disabled;
            painter.DrawIcon(icon::kPin,
                             {absolute_.x + xs[c] + ws[c] - kPinSlot,
                              header.y + (header.h - 14.0f) * 0.5f, 14.0f, 14.0f},
                             12.0f, pin);
        }
        if (ws[c] > 1.0f) {
            const bool hot = static_cast<int>(c) == hover_split_ ||
                             static_cast<int>(c) == resize_col_ ||
                             (reorder_dragging_ && static_cast<int>(c) == drop_col_);
            painter.FillRect({absolute_.x + xs[c] + ws[c], header.y + 8.0f, 1.0f,
                              std::max(0.0f, header.h - 16.0f)},
                             hot ? theme.text_secondary : theme.stroke_divider);
        }
    };

    const float header_h = std::min(HeaderHeight(), absolute_.h);
    const Rect header{absolute_.x, absolute_.y, absolute_.w, header_h};
    if (!header.IsEmpty()) {
        if (!ink) {
            painter.PushClip(header);
            painter.FillRoundedRect(absolute_, theme.radius_control, theme.fill_input_hover);
            painter.PopClip();
        }
        const Rect scrolling_header{absolute_.x + frozen_width, header.y,
                                    std::max(0.0f, absolute_.w - frozen_width), header.h};
        if (!scrolling_header.IsEmpty()) {
            painter.PushClip(scrolling_header);
            for (size_t c = 0; c < n; ++c) {
                if (!columns_[c].frozen) draw_header_column(c, header);
            }
            painter.PopClip();
        }
        if (frozen_width > 0.5f) {
            painter.PushClip({absolute_.x, header.y, frozen_width, header.h});
            for (size_t c = 0; c < n; ++c) {
                if (columns_[c].frozen) draw_header_column(c, header);
            }
            painter.PopClip();
        }
        if (!ink) {
            painter.FillRect({absolute_.x, absolute_.y + header_h - 1.0f, absolute_.w, 1.0f},
                             theme.stroke_divider);
        }
    }

    const float row_h = std::max(RowHeight(), 1.0f);
    const float body_h = BodyHeight();
    if (body_h > 0.5f) {
        const float body_y = absolute_.y + header_h;
        const Rect body_clip{absolute_.x, body_y, absolute_.w, body_h};
        const float body_bottom = body_y + body_h;
        if (!ink) {
            painter.PushClip(body_clip);
            const auto paint_row_bg = [&](ptrdiff_t row, float y) {
                if (y + row_h < body_y || y > body_bottom) return;
                const Rect row_rect{absolute_.x, y, absolute_.w, row_h};
                if (row == selected_) {
                    painter.FillRect(row_rect, theme.fill_selected);
                    painter.FillRoundedRect({row_rect.x, row_rect.y, 3.0f, row_rect.h}, 1.5f,
                                            theme.accent);
                } else if (row == hover_row_ && enabled_) {
                    painter.FillRect(row_rect, theme.fill_hover);
                }
            };
            if (groups_.empty()) {
                const ptrdiff_t first = static_cast<ptrdiff_t>(scroll_offset_ / row_h);
                const ptrdiff_t visible = static_cast<ptrdiff_t>(body_h / row_h) + 2;
                for (ptrdiff_t row = std::max(first, ptrdiff_t{0});
                     row < first + visible && row < static_cast<ptrdiff_t>(row_count_); ++row) {
                    paint_row_bg(row, body_y + static_cast<float>(row) * row_h - scroll_offset_);
                }
            } else {
                float cursor = 0.0f;
                for (size_t g = 0; g < groups_.size(); ++g) {
                    cursor += GroupBand();
                    if (groups_[g].expanded) {
                        for (size_t i = 0; i < groups_[g].count; ++i) {
                            paint_row_bg(static_cast<ptrdiff_t>(groups_[g].start + i),
                                         body_y + cursor + static_cast<float>(i) * row_h -
                                             scroll_offset_);
                        }
                        cursor += static_cast<float>(groups_[g].count) * row_h;
                    }
                }
            }
            painter.PopClip();
        }

        const float cell_clip_y = groups_.empty() ? body_y : body_y + GroupBand();
        const float cell_clip_h = groups_.empty() ? body_h : std::max(0.0f, body_h - GroupBand());
        if (ink) {
        const auto draw_text_columns = [&](bool frozen, const Rect& clip) {
            if (clip.IsEmpty()) return;
            painter.PushClip(clip);
            const auto draw_row_cells = [&](ptrdiff_t row, float y) {
                if (y + row_h < body_y || y > body_bottom) return;
                const Rect row_rect{absolute_.x, y, absolute_.w, row_h};
                for (size_t c = 0; c < n; ++c) {
                    if (columns_[c].frozen != frozen || ws[c] < 0.5f) continue;
                    if (xs[c] + ws[c] < -1.0f || xs[c] > absolute_.w + 1.0f) continue;
                    const float cell_w = std::max(0.0f, ws[c] - kCellPadX * 2.0f);
                    if (cell_w <= 0.5f) continue;
                    const Rect cell{row_rect.x + xs[c] + kCellPadX, row_rect.y, cell_w, row_h};
                    if (!draw_cache_) continue;
                    const auto& cache = *draw_cache_;
                    const auto finish = cache.cells.begin() + static_cast<ptrdiff_t>(cache.cell_count);
                    const auto found = std::lower_bound(cache.cells.begin(), finish, std::pair{row, c},
                        [](const DrawCache::Cell& item, const auto& key) { return std::pair{item.row, item.col} < key; });
                    if (found == finish || found->row != row || found->col != c) continue;
                    const auto& prepared = *found;
                    if (prepared.flow) {
                        const float text_width = prepared.width;
                        constexpr float kFlowPadX = 5.0f;
                        constexpr float kFlowPadY = 3.0f;
                        const float frame_w = std::min(cell_w + kFlowPadX,
                                                       text_width + kFlowPadX * 2.0f);
                        const Rect frame{cell.x - kFlowPadX, cell.y + kFlowPadY, frame_w,
                                         std::max(1.0f, cell.h - kFlowPadY * 2.0f)};
                        Color hot = theme.accent;
                        hot.a = 0.92f * theme.glow_intensity;
                        Color base = theme.stroke_divider;
                        base.a = 0.18f;
                        painter.StrokeRoundedRectSweep(frame, 4.0f, flow_angle_, hot, base, 1.4f);
                    }
                    if (columns_[c].kind == CellKind::Progress) {
                        if (!columns_[c].prog_get) continue;
                        const float t = prepared.progress;
                        const float bar_h = 4.0f;
                        const Rect track{cell.x, cell.y + (cell.h - bar_h) * 0.5f, cell.w, bar_h};
                        painter.FillRoundedRect(track, bar_h * 0.5f, theme.fill_hover);
                        const float fill_w = cell.w * t;
                        if (fill_w > 0.5f) {
                            painter.FillRoundedRect({track.x, track.y, fill_w, track.h},
                                                    bar_h * 0.5f, theme.text);
                        }
                        continue;
                    }
                    if (columns_[c].kind == CellKind::Icon) {
                        if (!columns_[c].icon_get) continue;
                        if (prepared.text.empty()) continue;
                        const float sz = 14.0f;
                        painter.DrawIcon(prepared.text,
                                         {cell.x, cell.y + (cell.h - sz) * 0.5f, sz, sz}, sz,
                                         theme.text);
                        continue;
                    }
                    if (columns_[c].kind != CellKind::Text) continue;
                    if (prepared.text.empty()) continue;
                    if (prepared.segments.empty()) {
                        painter.DrawText(prepared.text, cell, TextRole::Caption, theme.text,
                                         (columns_[c].numeric || columns_[c].numeric_text) ? Align::Trailing : Align::Leading);
                        continue;
                    }
                    float text_x = cell.x;
                    for (const auto& part : prepared.segments) {
                        if (text_x >= cell.Right()) break;
                        const std::wstring_view segment(prepared.text.data() + part.begin, part.length);
                        const Rect rect{text_x, cell.y, cell.Right() - text_x, cell.h};
                        if (part.custom) {
                            FontFamilyScope font(cell_font_family_);
                            painter.DrawText(segment, rect, TextRole::Caption, theme.text);
                        } else painter.DrawText(segment, rect, TextRole::Caption, theme.text);
                        text_x += part.advance;
                    }
                }
            };
            if (groups_.empty()) {
                const ptrdiff_t first = static_cast<ptrdiff_t>(scroll_offset_ / row_h);
                const ptrdiff_t visible = static_cast<ptrdiff_t>(body_h / row_h) + 2;
                for (ptrdiff_t row = std::max(first, ptrdiff_t{0});
                     row < first + visible && row < static_cast<ptrdiff_t>(row_count_); ++row) {
                    draw_row_cells(row, body_y + static_cast<float>(row) * row_h - scroll_offset_);
                }
            } else {
                float cursor = 0.0f;
                for (const Group& g : groups_) {
                    cursor += GroupBand();
                    if (!g.expanded) continue;
                    for (size_t i = 0; i < g.count; ++i) {
                        draw_row_cells(static_cast<ptrdiff_t>(g.start + i),
                                       body_y + cursor + static_cast<float>(i) * row_h -
                                           scroll_offset_);
                    }
                    cursor += static_cast<float>(g.count) * row_h;
                }
            }
            painter.PopClip();
        };

        draw_text_columns(false, {absolute_.x + frozen_width, cell_clip_y,
                                  std::max(0.0f, absolute_.w - frozen_width), cell_clip_h});
        draw_text_columns(true, {absolute_.x, cell_clip_y, frozen_width, cell_clip_h});
        }
        if (!ink && focused_ && selected_ >= 0 && active_col_ >= 0 &&
            active_col_ < static_cast<int>(n) && ws[static_cast<size_t>(active_col_)] > 0.5f) {
            const float y = body_y + RowTop(static_cast<size_t>(selected_)) - scroll_offset_;
            const size_t ac = static_cast<size_t>(active_col_);
            const Rect cell{absolute_.x + xs[ac] + 1.0f, y + 1.0f, std::max(0.0f, ws[ac] - 2.0f),
                            std::max(0.0f, row_h - 2.0f)};
            const Rect ring_clip{absolute_.x, cell_clip_y, absolute_.w, cell_clip_h};
            if (cell.h > 1.0f && y + row_h > cell_clip_y && y < cell_clip_y + cell_clip_h &&
                !ring_clip.IsEmpty()) {
                painter.PushClip(ring_clip);
                painter.StrokeRoundedRect(cell, 3.0f, theme.accent, 1.0f);
                painter.PopClip();
            }
        }
        if (!groups_.empty()) {
            painter.PushClip(body_clip);
            const auto paint_group_header = [&](size_t g, float y) {
                if (y + GroupBand() < body_y || y > body_bottom) return;
                if (!ink) {
                    painter.FillRect({absolute_.x, y, absolute_.w, GroupBand()},
                                     theme.fill_input_hover);
                    painter.FillRect({absolute_.x, y + GroupBand() - 1.0f, absolute_.w, 1.0f},
                                     theme.stroke_divider);
                    painter.DrawChevron({absolute_.x + 14.0f, y + GroupBand() * 0.5f}, 8.0f,
                                        groups_[g].expanded ? 0.0f : -90.0f, theme.text_secondary,
                                        1.4f);
                } else {
                    painter.DrawText(groups_[g].key,
                                     {absolute_.x + 26.0f, y, absolute_.w - 40.0f, GroupBand()},
                                     TextRole::CaptionStrong, theme.text);
                }
            };
            float cursor = 0.0f;
            ptrdiff_t sticky = -1;
            float sticky_next = body_bottom;
            for (size_t g = 0; g < groups_.size(); ++g) {
                const float header_y = body_y + cursor - scroll_offset_;
                if (header_y <= body_y + 0.5f) sticky = static_cast<ptrdiff_t>(g);
                else if (sticky >= 0 && sticky_next == body_bottom) sticky_next = header_y;
                if (header_y > body_y + 0.5f) paint_group_header(g, header_y);
                cursor += GroupBand();
                if (groups_[g].expanded) {
                    cursor += static_cast<float>(groups_[g].count) * row_h;
                }
            }
            if (sticky >= 0) {
                const float y = std::min(body_y, sticky_next - GroupBand());
                paint_group_header(static_cast<size_t>(sticky), y);
            }
            painter.PopClip();
        }
    }

    if (footer_) {
        const Rect footer = FooterRect();
        if (!footer.IsEmpty()) {
            if (!ink) {
                painter.PushClip(footer);
                painter.FillRoundedRect(absolute_, theme.radius_control, theme.fill_input_hover);
                painter.PopClip();
                painter.FillRect({footer.x, footer.y, footer.w, 1.0f}, theme.stroke_divider);
            } else {
                const auto draw_footer_columns = [&](bool frozen, const Rect& clip) {
                    if (clip.IsEmpty()) return;
                    painter.PushClip(clip);
                    for (size_t c = 0; c < n; ++c) {
                        if (columns_[c].frozen != frozen || ws[c] < 0.5f) continue;
                        const float cell_x = footer.x + xs[c];
                        if (cell_x + ws[c] < clip.x + 0.5f || cell_x > clip.Right()) continue;
                        if (!draw_cache_ || c >= draw_cache_->footers.size()) continue;
                        const auto& text = draw_cache_->footers[c];
                        if (text.empty()) continue;
                        painter.DrawText(text,
                                         {cell_x + kCellPadX, footer.y,
                                          std::max(0.0f, ws[c] - kCellPadX * 2.0f), footer.h},
                                         TextRole::Caption, theme.text_secondary);
                    }
                    painter.PopClip();
                };
                draw_footer_columns(false, {footer.x + frozen_width, footer.y,
                                            std::max(0.0f, footer.w - frozen_width), footer.h});
                draw_footer_columns(true, {footer.x, footer.y, frozen_width, footer.h});
            }
        }
    }
    if (!ink && frozen_width > 0.5f) {
        float bottom = absolute_.y + header_h;
        if (body_h > 0.5f) bottom = absolute_.y + header_h + body_h;
        if (footer_) {
            const Rect footer = FooterRect();
            if (!footer.IsEmpty()) bottom = footer.Bottom();
        }
        painter.FillRect({absolute_.x + frozen_width - 1.0f, absolute_.y, 1.0f,
                          std::max(0.0f, bottom - absolute_.y)},
                         theme.control_stroke);
    }
    if (!ink && reorder_dragging_ && drop_col_ >= 0 && drop_col_ < static_cast<int>(n) &&
        ws[static_cast<size_t>(drop_col_)] > 0.5f) {
        const float x = absolute_.x + xs[static_cast<size_t>(drop_col_)];
        painter.FillRect({x, absolute_.y, 2.0f, HeaderHeight()}, theme.accent);
    }
    };

    ID2D1DeviceContext2* dc = painter.DeviceContext();
    if (painter.CanRecordCommandList() && dc && draw_cache_) {
        const uint64_t cols = ColumnFingerprint();
        DrawCache& cache = *draw_cache_;
        // 命令列表使用绝对坐标；仅位置变化的重排同样需要重新录制。
        const bool hit = cache.list && cache.device == dc && cache.scroll == scroll_offset_ &&
                         cache.x == absolute_.x && cache.y == absolute_.y &&
                         cache.hscroll == horizontal_offset_ && cache.w == absolute_.w &&
                         cache.h == absolute_.h && cache.selected == selected_ &&
                         cache.hover == hover_row_ && cache.hover_split == hover_split_ &&
                         cache.sort_col == sort_col_ && cache.sort_dir == sort_dir_ &&
                         cache.active_col == active_col_ && cache.drop_col == drop_col_ &&
                         cache.group_col == group_col_ && cache.focused == focused_ &&
                         cache.footer == footer_ && cache.rows == row_count_ && cache.cols == cols;
        if (!hit) {
            cache.list.reset();
            auto cmd = std::move(cache.pending);
            if (cmd) {
                ComPtr<ID2D1Image> previous;
                dc->GetTarget(&previous);
                D2D1_MATRIX_3X2_F saved{};
                dc->GetTransform(&saved);
                dc->SetTarget(cmd.get());
                dc->SetTransform(saved);
                paint(false);
                cmd->Close();
                dc->SetTarget(previous.get());
                dc->SetTransform(saved);
                cache.list = std::move(cmd);
                cache.device = dc;
                cache.scroll = scroll_offset_;
                cache.hscroll = horizontal_offset_;
                cache.x = absolute_.x;
                cache.y = absolute_.y;
                cache.w = absolute_.w;
                cache.h = absolute_.h;
                cache.selected = selected_;
                cache.hover = hover_row_;
                cache.hover_split = hover_split_;
                cache.sort_col = sort_col_;
                cache.sort_dir = sort_dir_;
                cache.active_col = active_col_;
                cache.drop_col = drop_col_;
                cache.group_col = group_col_;
                cache.focused = focused_;
                cache.footer = footer_;
                cache.rows = row_count_;
                cache.cols = cols;
            }
        }
        if (cache.list) {
            D2D1_MATRIX_3X2_F saved{};
            dc->GetTransform(&saved);
            dc->SetTransform(D2D1::Matrix3x2F::Identity());
            dc->DrawImage(cache.list.get());
            dc->SetTransform(saved);
            paint(true);
            return;
        }
    }
    paint(false);
    paint(true);
}

void Table::DrawOverlay(Painter& painter, const Theme& theme) {
    if (BodyHeight() <= 0.5f) return;
    const bool hot = hovered_ || dragging_ || horizontal_dragging_;
    const Color rest = theme.scrollbar_thumb;
    const Color lit = theme.scrollbar_thumb_hover;
    const Color thumb = hot ? lit : rest;
    const float v_expand =
        MaxScroll() > 0.5f ? std::max(expand_progress_, 0.45f) : expand_progress_;
    const float h_expand =
        MaxHorizontalScroll() > 0.5f ? std::max(expand_progress_, 0.45f) : expand_progress_;
    painter.DrawScrollThumb(Thumb(v_expand), thumb);
    painter.DrawScrollThumb(HorizontalThumb(h_expand), thumb);
}

}  // namespace lumen
