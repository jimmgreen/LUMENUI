// visual — 视觉回归：离屏渲染 LUMEN 控件状态板 → PNG + 像素断言（单暗色主题）。
#include "lumen/lumen.h"
#include "core/offscreen.h"
#include "core/text_service.h"
#include <objbase.h>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <initializer_list>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <span>
#include <windows.h>
#include <oleauto.h>
#include <UIAutomation.h>
#include <UIAutomationClient.h>

using namespace lumen;

namespace {

int g_failures = 0;

// 崩溃诊断：打印异常码与出错地址所在模块（RIP 相对模块基址偏移）。
LONG CALLBACK CrashReport(PEXCEPTION_POINTERS info) {
    if (info->ExceptionRecord->ExceptionCode == 0x406D1388) {
        return EXCEPTION_CONTINUE_SEARCH;   // Visual C++ 调试器线程命名通知，不是崩溃。
    }
    void* addr = info->ExceptionRecord->ExceptionAddress;
    HMODULE mod = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                               GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           static_cast<LPCSTR>(addr), &mod) &&
        GetModuleFileNameA(mod, name, MAX_PATH)) {
        const uintptr_t base = reinterpret_cast<uintptr_t>(mod);
        std::fprintf(stderr, "[crash] code=0x%08lx addr=%p %s+0x%zx\n",
                     info->ExceptionRecord->ExceptionCode, addr, name,
                     reinterpret_cast<uintptr_t>(addr) - base);
    } else {
        std::fprintf(stderr, "[crash] code=0x%08lx addr=%p\n",
                     info->ExceptionRecord->ExceptionCode, addr);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void Check(bool condition, const char* name) {
    std::printf("%s %s\n", condition ? "[PASS]" : "[FAIL]", name);
    if (!condition) ++g_failures;
}

bool CloseTo(Color a, Color b, float tol = 0.06f) {
    return std::fabs(a.r - b.r) <= tol && std::fabs(a.g - b.g) <= tol &&
           std::fabs(a.b - b.b) <= tol && std::fabs(a.a - b.a) <= tol;
}

Color Over(Color top, Color bottom) {
    const float a = top.a;
    return {top.r * a + bottom.r * (1.0f - a), top.g * a + bottom.g * (1.0f - a),
            top.b * a + bottom.b * (1.0f - a), 1.0f};
}

struct TestRoot : StackPanel {
    using StackPanel::Measure;
    using StackPanel::Arrange;
};

struct Scene {
    TestRoot root;
    Button* primary = nullptr;
    CheckBox* checked_box = nullptr;
    Switch* on_switch = nullptr;
    ListView* list = nullptr;
    StackPanel* spot_card = nullptr;

    void Build() {
        root.Padding(16.0f, 12.0f).Spacing(8.0f);
        auto& row1 = root.Add<StackPanel>(StackPanel::Orientation::Horizontal);
        row1.Spacing(8.0f);
        row1.Add<Button>(L"标准");
        row1.Add<Button>(L"主要", ButtonKind::Primary);
        primary = &row1.Add<Button>(L"危险", ButtonKind::Danger);
        row1.Add<Button>(L"透明", ButtonKind::Transparent);
        auto& disabled = row1.Add<Button>(L"禁用");
        disabled.Enabled(false);

        auto& row2 = root.Add<StackPanel>(StackPanel::Orientation::Horizontal);
        row2.Spacing(16.0f);
        row2.Add<CheckBox>(L"未选");
        checked_box = &row2.Add<CheckBox>(L"已选");
        checked_box->Checked(true);
        row2.Add<CheckBox>(L"混合").ThreeState(true).State(CheckState::Indeterminate);
        row2.Add<ToggleButton>(L"切换").Checked(true);
        row2.Add<RadioButton>(L"单选甲").Checked(true);
        row2.Add<RadioButton>(L"单选乙");
        on_switch = &row2.Add<Switch>(L"开关");
        on_switch->Checked(true);
        row2.Add<Switch>(L"关");

        auto& row3 = root.Add<StackPanel>(StackPanel::Orientation::Horizontal);
        row3.Spacing(12.0f);
        row3.Add<TextBox>(L"Emoji \xD83D\xDE00");
        auto& combo = row3.Add<ComboBox>();
        combo.AddItems({L"选项一", L"选项二"}).SelectedIndex(0);

        auto& row4 = root.Add<StackPanel>(StackPanel::Orientation::Horizontal);
        row4.Spacing(12.0f);
        row4.Add<Slider>().Value(0.4f);
        row4.Add<ProgressBar>().Value(0.7f);
        row4.Add<ProgressBar>().Indeterminate(true);

        list = &root.Add<ListView>();
        list->ItemCount(50);
        list->ItemText([](size_t i, std::wstring& s) { s = L"项目 " + std::to_wstring(i); });
        list->SelectedIndex(1);

        auto& tabs = root.Add<TabControl>();
        tabs.AddTab(L"标签一").Add<Label>(L"内容一");
        tabs.AddTab(L"标签二").Add<Label>(L"内容二");

        root.Add<Label>(L"静态文本 Body / 二级文本", TextRole::Body).Secondary(true);

        spot_card = &root.Add<StackPanel>();
        spot_card->Card(StackPanel::CardStyle::Lumen, 14.0f);
        spot_card->Padding(16.0f, 12.0f);
        spot_card->Add<Label>(L"BENTO 聚光卡 — 径向反射与边缘折射", TextRole::BodyStrong);
        spot_card->Add<Label>(L"鼠标跟随光斑 600px · 边缘折射光环 400px", TextRole::Caption)
            .Secondary(true);

        root.Add<Separator>();
        root.Add<HyperlinkButton>(L"文档链接");
        auto& bar = root.Add<InfoBar>(L"信息条");
        bar.Message(L"单色提示").Closable(false);
        root.Add<PasswordBox>(L"secret");
    }
};

bool Near(float a, float b, float eps = 0.05f) { return std::fabs(a - b) <= eps; }

void ClickDip(Window& window, Point dip) {
    window.DispatchMouseMove(dip);
    window.DispatchMouseDown(dip);
    window.DispatchMouseUp(dip);
}

void WheelDip(HWND hwnd, Point dip, int delta) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const float sx = rc.right > 0 ? static_cast<float>(rc.right) / 320.0f : 1.0f;
    const float sy = rc.bottom > 0 ? static_cast<float>(rc.bottom) / 360.0f : sx;
    POINT client{static_cast<int>(dip.x * sx + 0.5f), static_cast<int>(dip.y * sy + 0.5f)};
    ClientToScreen(hwnd, &client);
    SendMessageW(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, delta), MAKELPARAM(client.x, client.y));
}

// —— 交互逻辑：直接驱动控件输入入口（不经过窗口路由；修饰键走 buttons 标志）——
struct TestList : ListView {
    using ListView::OnMouseDown;
    using ListView::OnMouseMove;
    using ListView::OnMouseUp;
    using ListView::OnKey;
    using ListView::OnAnimate;
    using ListView::AutomationItemName;
};
struct TestDirtyControl : Button {
    using Button::Button;
    using Control::DirtyBounds;
    using Control::Invalidate;
};
struct TestSlider : Slider {
    using Slider::Measure;
    using Slider::OnKey;
};
struct TestRangeSlider : RangeSlider {
    using RangeSlider::Measure;
    using RangeSlider::OnKey;
};
struct TestTabs : TabControl {
    using TabControl::OnKey;
    using TabControl::OnMouseDown;
    using TabControl::OnMouseMove;
    using TabControl::OnMouseUp;
    using TabControl::OnWheel;
    using TabControl::Arrange;
    float Scroll() const { return scroll_x_; }
    bool Overflows() const { return StripOverflows(); }
};
struct TestComboBox : ComboBox {
    using ComboBox::OnChar;
    using ComboBox::OnKey;
    using ComboBox::OnAnimate;
    using ComboBox::Measure;
    void Focus() { ComboBox::Focus(); }
};

struct TestTextBox : TextBox {
    using TextBox::OnChar;
    using TextBox::OnKey;
    using TextBox::OnImeCompose;
    using TextBox::OnImeCommit;
    using TextBox::OnImeEnd;
    using TextBox::ImeComposing;
    using TextBox::OnMouseDown;
    using TextBox::OnMouseDoubleClick;
    using TextBox::OnMouseUp;
    using TextBox::OnAnimate;
    using TextBox::Measure;
    using TextBox::Undo;
    using TextBox::SelectWordAt;
    using TextBox::SelectLineAt;
    using TextBox::WordLeft;
    using TextBox::EnsureEditMenu;
    using TextBox::CaretX;
    using Control::MeasureText;
    size_t CaretIndex() const noexcept { return caret_; }
    size_t AnchorIndex() const noexcept { return anchor_; }
};

struct TestAutoSuggestBox : AutoSuggestBox {
    using AutoSuggestBox::OnChar;
    using AutoSuggestBox::OnKey;
};

struct TestDatePicker : DatePicker {
    using DatePicker::OnKey;
};

struct TestCheckBox : CheckBox {
    using CheckBox::CheckBox;
    using CheckBox::OnMouseUp;
    using CheckBox::OnKey;
};
struct TestToggleButton : ToggleButton {
    using ToggleButton::ToggleButton;
    using ToggleButton::OnMouseUp;
    using ToggleButton::OnKey;
};


struct TestTimePicker : TimePicker {
    using TimePicker::OnKey;
};

struct TestCommandBar : CommandBar {
    using CommandBar::OnKey;
    using CommandBar::Measure;
    using CommandBar::Arrange;
};

struct TestNavigationView : NavigationView {
    using NavigationView::OnKey;
    using NavigationView::OnMouseDown;
    using NavigationView::Measure;
    using NavigationView::Arrange;
    using NavigationView::CursorAt;
};

struct TestScrollViewer : ScrollViewer {
    using ScrollViewer::OnWheel;
    using ScrollViewer::OnHWheel;
    using ScrollViewer::OnAnimate;
    using ScrollViewer::Measure;
    using ScrollViewer::Arrange;
};

struct TestBreadcrumb : Breadcrumb {
    using Breadcrumb::OnMouseDown;
    using Breadcrumb::Measure;
    using Breadcrumb::Arrange;
};

struct TestTable : Table {
    using Table::AutomationItemName;
    using Table::OnMouseDown;
    using Table::OnMouseMove;
    using Table::OnMouseUp;
    using Table::OnMouseDoubleClick;
    using Table::CommitCellEdit;
    using Table::CancelCellEdit;
    using Table::ColumnAt;
    using Table::MaxHorizontalScroll;
    using Table::OnWheel;
    using Table::OnKey;
    using Table::Arrange;
    using Table::RowTop;
    using Table::ShowContextMenu;
    size_t SlotCount() const { return slots_.size(); }
    size_t SlotColumn(size_t i) const { return slots_[i].col; }
    Control* SlotControl(size_t i) { return i < slots_.size() ? slots_[i].control : nullptr; }
    void Commit() { CommitCellEdit(); }
    void Cancel() { CancelCellEdit(); }
    Table::CellEditor* Editor() { return cell_editor_; }
};
struct TestSplitView : SplitView {
    using SplitView::OnAnimate;
};
struct TestDialog : Dialog {
    using Dialog::Measure;
    using Dialog::Arrange;
    using Dialog::OnKey;
};
struct TestColorPicker : ColorPicker {
    using ColorPicker::Measure;
    using ColorPicker::Arrange;
    using ColorPicker::OnKey;
    using ColorPicker::OnMouseDown;
};
struct TestCalendarView : CalendarView {
    using CalendarView::Measure;
    using CalendarView::Arrange;
    using CalendarView::OnKey;
    using CalendarView::OnMouseDown;
};
struct TestChip : Chip {
    using Chip::Chip;
    using Chip::Measure;
    using Chip::Arrange;
    using Chip::OnKey;
    using Chip::OnMouseDown;
    using Chip::OnMouseUp;
};
struct TestGridView : GridView {
    using GridView::Measure;
    using GridView::Arrange;
    using GridView::OnKey;
    using GridView::OnMouseDown;
};
struct TestPagination : Pagination {
    using Pagination::OnKey;
    using Pagination::OnMouseDown;
};
struct TestMenuBar : MenuBar {
    using MenuBar::OnMouseDown;
    using MenuBar::Measure;
    using MenuBar::Arrange;
};
struct TestCarousel : Carousel {
    using Carousel::OnKey;
    using Carousel::OnMouseDown;
    using Carousel::Measure;
    using Carousel::Arrange;
};
struct TestStepper : Stepper {
    using Stepper::OnMouseDown;
    using Stepper::Measure;
    using Stepper::Arrange;
};
struct TestSpotlightCard : StackPanel {
    void ForceSpotlight() {
        spotlight_t_ = 1.0f;
        spotlight_inside_ = true;
    }
};
struct TestButton : Button {
    using Button::OnMouseUp;
    using Button::OnKey;
};
struct TestPanel : Panel {
    using Panel::Measure;
};
struct TestSparkline : Sparkline {
    using Sparkline::Measure;
    using Sparkline::Arrange;
};
struct TestGauge : Gauge {
    using Gauge::Measure;
    using Gauge::Arrange;
};
struct TestChart : Chart {
    using Chart::Measure;
    using Chart::Arrange;
    using Chart::OnMouseMove;
    using Chart::OnMouseDown;
    using Chart::OnMouseUp;
    using Chart::OnMouseDoubleClick;
    using Chart::OnWheel;
    using Chart::OnAnimate;
};
struct TestLog : LogView {
    using LogView::Measure;
    using LogView::Arrange;
    using LogView::OnWheel;
};
struct TestRich : RichLabel {
    using RichLabel::Measure;
    using RichLabel::Arrange;
};

constexpr uint32_t kBtnL = 0x0001;
constexpr uint32_t kBtnShift = 0x0004;
constexpr uint32_t kBtnCtrl = 0x0008;
constexpr uint32_t kBtnM = 0x0010;

bool SameIndices(const std::vector<size_t>& got, std::initializer_list<size_t> want) {
    if (got.size() != want.size()) return false;
    size_t i = 0;
    for (size_t w : want) {
        if (got[i++] != w) return false;
    }
    return true;
}

void TestInteraction() {
    const Theme theme = MakeTheme();

    {
        TestSlider slider;
        int changed = 0;
        slider.Range(0.0f, 10.0f)
            .Step(2.0f)
            .Value(4.0f)
            .Orientation(SliderOrientation::Vertical)
            .OnValueChanged([&](float) { ++changed; });
        Check(slider.Measure({100.0f, 300.0f}, theme).h == 180.0f,
              "vertical slider measures tall");
        slider.OnKey(VK_UP);
        Check(slider.Value() == 6.0f && changed == 1, "vertical slider up increments");
        slider.OnKey(VK_DOWN);
        Check(slider.Value() == 4.0f && changed == 2, "vertical slider down decrements");
    }
    {
        TestRangeSlider slider;
        int changed = 0;
        slider.Range(0.0f, 100.0f)
            .Values(25.0f, 75.0f)
            .Step(5.0f)
            .OnValueChanged([&](float, float) { ++changed; });
        slider.OnKey(VK_END);
        Check(slider.LowerValue() == 75.0f && slider.UpperValue() == 75.0f,
              "range slider lower cannot cross upper");
        Check(slider.OnKey(VK_TAB), "range slider tab selects upper thumb");
        slider.OnKey(VK_RIGHT);
        Check(slider.UpperValue() == 80.0f && changed == 2,
              "range slider keyboard adjusts active thumb");
        slider.Values(90.0f, 10.0f);
        Check(slider.LowerValue() == 10.0f && slider.UpperValue() == 90.0f,
              "range slider programmatic values normalize");
    }
    {
        TestTabs tabs;
        int closed = 0;
        int changed = 0;
        tabs.AddTab({L"home", L"Home", icon::kHome, false});
        tabs.AddTab({L"file", L"main.cpp", icon::kCode, true});
        tabs.AddTab({L"log", L"Build log", icon::kLayers, true});
        tabs.SelectedId(L"file")
            .OnSelectionChanged([&](ptrdiff_t, ptrdiff_t) { ++changed; })
            .OnTabClosing([](std::wstring_view id) { return id != L"log"; })
            .OnTabClosed([&](std::wstring_view) { ++closed; });
        Check(tabs.SelectedId() == L"file", "tabs select stable id");
        Check(!tabs.CloseTab(L"log") && closed == 0, "tabs close can be vetoed");
        Check(tabs.CloseTab(L"file") && closed == 1, "tabs close removes page");
        Check(tabs.SelectedId() == L"log" && changed == 1,
              "tabs close selects right neighbor");
        Check(!tabs.CloseTab(L"home"), "tabs nonclosable item stays open");
        tabs.OnKey(VK_HOME);
        Check(tabs.SelectedId() == L"home", "tabs home selects first");
        tabs.OnKey(VK_END);
        Check(tabs.SelectedId() == L"log", "tabs end selects last");
    }
    {
        TestTabs tabs;
        int moved = 0;
        tabs.AddTab({L"a", L"Alpha", L"", false});
        tabs.AddTab({L"b", L"Beta", L"", false});
        tabs.AddTab({L"c", L"Gamma", L"", false});
        tabs.OnReordered([&](size_t, size_t) { ++moved; });
        tabs.MoveTab(0, 2);
        Check(tabs.Tab(0).id == L"b" && tabs.Tab(2).id == L"a" && tabs.SelectedId() == L"a",
              "tabs MoveTab permutes and keeps selection");
        Check(moved == 1, "tabs MoveTab notifies");
        tabs.Arrange({0.0f, 0.0f, 480.0f, 180.0f});
        tabs.SelectedIndex(0);
        tabs.OnMouseDown({12.0f, 20.0f}, kBtnL);
        tabs.OnMouseMove({280.0f, 20.0f}, kBtnL);
        tabs.OnMouseUp({280.0f, 20.0f}, kBtnL);
        Check(tabs.Tab(0).id == L"c" && tabs.Tab(2).id == L"b" && moved == 2,
              "tabs drag reorder drops at slot");
    }
    {
        TestTabs tabs;
        int closed = 0;
        tabs.AddTab({L"pin", L"Pinned", L"", false});
        tabs.OnTabClosed([&](std::wstring_view) { ++closed; });
        tabs.Arrange({0.0f, 0.0f, 400.0f, 180.0f});
        tabs.OnMouseDown({20.0f, 20.0f}, kBtnM);
        Check(tabs.TabCount() == 1 && closed == 0, "tabs middle click skips nonclosable");
        TestTabs scratch;
        scratch.AddTab({L"tmp", L"Scratch", L"", true});
        scratch.OnTabClosed([&](std::wstring_view) { ++closed; });
        scratch.Arrange({0.0f, 0.0f, 400.0f, 180.0f});
        scratch.OnMouseDown({20.0f, 20.0f}, kBtnM);
        Check(closed == 1 && scratch.TabCount() == 0, "tabs middle click closes");
    }
    {
        TestTabs tabs;
        for (int i = 0; i < 8; ++i) {
            tabs.AddTab(L"Document " + std::to_wstring(i) + L" · long title");
        }
        tabs.Arrange({0.0f, 0.0f, 220.0f, 180.0f});
        Check(tabs.Overflows(), "tabs overflow when strip is narrow");
        tabs.OnMouseMove({40.0f, 16.0f}, 0);
        Check(tabs.OnWheel(-1.0f), "tabs wheel scrolls strip");
        Check(tabs.Scroll() > 0.0f, "tabs scroll offset increases");
        tabs.SelectedIndex(7);
        Check(tabs.Scroll() > 0.0f, "tabs selecting last keeps overflow scroll");
    }
    {
        TestList list;
        int expanded = 0;
        list.Groups({{L"today", L"Today", 3, true},
                        {L"older", L"Older", 100000, true}})
            .OnGroupExpandedChanged([&](std::wstring_view id, bool on) {
                if (id == L"older" && !on) ++expanded;
            });
        Check(list.ItemCount() == 100003, "grouped list exposes global item count");
        list.MultiSelect(true).SelectedIndices({1, 100002});
        list.GroupExpanded(L"older", false);
        Check(!list.GroupExpanded(L"older") && expanded == 1,
              "grouped list collapses section");
        Check(list.SelectionCount() == 2 && list.IsSelected(100002),
              "grouped list preserves hidden selection");
        list.ItemCount(12);
        Check(list.Groups().empty() && list.ItemCount() == 12,
              "plain item count exits grouped mode");
        list.Groups({{L"a", L"A", 3, true}, {L"b", L"B", 3, true}});
        Check(list.ItemCount() == 6, "grouped list rebuilds count");
        list.MoveItem(0, 2);
        Check(list.DataIndex(0) == 1 && list.DataIndex(1) == 2 && list.DataIndex(2) == 0,
              "grouped list reorders inside section");
        list.MoveItem(2, 4);
        Check(list.DataIndex(2) == 0 && list.DataIndex(4) == 4,
              "grouped list rejects cross-section move");
    }
    {
        TestRoot root;
        auto& list = root.Add<TestList>();
        list.ItemCount(4);
        list.MoveItem(3, 0);
        Check(list.DataIndex(0) == 3 && list.DataIndex(1) == 0 && list.DataIndex(3) == 2,
              "plain list MoveItem permutes view");
        list.SelectedIndex(0);
        Check(list.SelectedDataIndex() == 3, "selection follows moved row");
        size_t swiped = 99;
        list.SwipeTrailing({L"Del", L"", [&](size_t view) { swiped = view; }});
        root.Measure({300.0f, 200.0f}, theme);
        root.Arrange({0.0f, 0.0f, 300.0f, 200.0f});
        const float rh = theme.list_row_height;
        list.OnMouseDown({80.0f, 0.5f * rh}, kBtnL);
        list.OnMouseMove({20.0f, 0.5f * rh}, kBtnL);
        list.OnMouseUp({20.0f, 0.5f * rh}, 0);
        Check(swiped == 0, "trailing swipe invokes view row");
    }
    {
        TestRoot root;
        auto& list = root.Add<TestList>();
        list.EmptyTitle(L"None").EmptyHint(L"Hint");
        list.ItemCount(0);
        root.Measure({300.0f, 200.0f}, theme);
        root.Arrange({0.0f, 0.0f, 300.0f, 200.0f});
        Check(list.ChildCount() == 1 && list.Child(0).Visible(), "empty list hosts EmptyState");
        Check(list.Child(0).AbsoluteBounds().h > 8.0f, "empty state fills list");
        list.ItemCount(3);
        root.Measure({300.0f, 200.0f}, theme);
        root.Arrange({0.0f, 0.0f, 300.0f, 200.0f});
        Check(!list.Child(0).Visible(), "empty state hides when rows exist");
        int removed = 0;
        list.AnimateRemoved(1, [&] {
            ++removed;
            list.ItemCount(2);
        });
        list.OnAnimate(1.0f);
        Check(removed == 1 && list.ItemCount() == 2, "remove animation completes");
        list.ItemCount(3);
        list.AnimateInserted(0);
        list.OnAnimate(1.0f);
        Check(list.ItemCount() == 3, "insert animation leaves count");
    }
    {
        TestList list;
        VectorModel<std::wstring> model;
        model.Reset({L"Alpha", L"Bravo"});
        list.Bind(model);
        Check(list.ItemCount() == 2, "bind list takes model count");
        model.Insert(0, L"New");
        Check(list.ItemCount() == 3, "bind list insert updates count");
        Check(list.AutomationItemName(0) == L"New", "bind list insert at 0");
        Check(list.AutomationItemName(1) == L"Alpha", "bind list keeps following rows");
        model.RemoveAt(0);
        Check(list.ItemCount() == 2 && list.AutomationItemName(0) == L"Alpha",
              "bind list remove syncs count");

        VectorModel<std::wstring> names;
        names.Reset({L"Cedar", L"Aspen", L"Birch"});
        FilteredModel filtered(names, [](size_t, const ItemRow& row) {
            return row.text.find(L'e') != std::wstring::npos;
        });
        TestList flist;
        flist.Bind(filtered);
        Check(flist.ItemCount() == 2, "filtered model drops non-matching");
        Check(flist.AutomationItemName(0) == L"Cedar", "filtered keeps source order");
        filtered.Where([](size_t, const ItemRow& row) { return row.text == L"Birch"; });
        Check(flist.ItemCount() == 1 && flist.AutomationItemName(0) == L"Birch",
              "filtered Where rebuilds view");

        SortedModel sorted(names);
        sorted.OrderBy([&](size_t a, size_t b) {
            ItemRow ra, rb;
            names.Get(a, ra);
            names.Get(b, rb);
            return ra.text < rb.text;
        });
        TestList slist;
        slist.Bind(sorted);
        Check(slist.ItemCount() == 3 && slist.AutomationItemName(0) == L"Aspen",
              "sorted model orders A-Z");

        VectorModel<ItemData> table_model;
        table_model.Map([](const ItemData& d, ItemRow& row) {
            row.text = d.text;
            row.cells = {d.text, d.glyph};
        });
        table_model.Reset({{L"N", L"x"}, {L"Q", L"y"}});
        TestTable table;
        table.AddColumn(L"Name", 80.0f);
        table.AddColumn(L"G", 40.0f);
        table.Bind(table_model);
        Check(table.RowCount() == 2, "table bind follows model count");
        table_model.Push({L"R", L"z"});
        Check(table.RowCount() == 3 && table.AutomationItemName(2) == L"R",
              "table bind insert updates count");
    }
    {
        std::vector<std::wstring> items(10000);
        for (size_t i = 0; i < items.size(); ++i) items[i] = L"Option " + std::to_wstring(i);
        Window window(L"combo-virtual", {360.0f, 360.0f});
        auto& combo = window.Root().Add<TestComboBox>();
        combo.Items(std::move(items)).MaxDropDownRows(6);
        window.Show();
        HWND hwnd = static_cast<HWND>(window.NativeHandle());
        UpdateWindow(hwnd);
        combo.Focus();
        combo.OnKey(VK_RETURN);
        SendMessageW(hwnd, WM_KEYDOWN, VK_END, 0);
        SendMessageW(hwnd, WM_KEYDOWN, VK_RETURN, 0);
        Check(combo.SelectedIndex() == 9999, "combo virtual popup selects last of 10000");
        Check(combo.HasFocus(), "combo popup restores anchor focus");
        combo.Editable(false).SelectedIndex(-1).Editable(true);
        combo.OnChar(L'9');
        SendMessageW(hwnd, WM_CHAR, L'9', 0);
        SendMessageW(hwnd, WM_KEYDOWN, VK_RETURN, 0);
        Check(combo.SelectedIndex() == 99, "combo editable filter maps original index");
        window.Close();
    }
    {
        std::vector<std::wstring> items(2000);
        for (size_t i = 0; i < items.size(); ++i) items[i] = L"Option " + std::to_wstring(i);
        Window window(L"combo-scroll-drag", {360.0f, 420.0f});
        window.Root().Padding(12.0f, 8.0f);
        auto& combo = window.Root().Add<TestComboBox>();
        combo.Items(std::move(items)).MaxDropDownRows(8);
        window.Show();
        HWND hwnd = static_cast<HWND>(window.NativeHandle());
        UpdateWindow(hwnd);
        combo.Focus();
        combo.OnKey(VK_RETURN);
        UpdateWindow(hwnd);
        const Rect bounds = combo.AbsoluteBounds();
        const float scale = static_cast<float>(GetDpiForWindow(hwnd)) / 96.0f;
        auto px = [scale](float dip) { return static_cast<int>(dip * scale + 0.5f); };
        // ShowTransient 把弹层 x 夹到 ≥8，与 ComboBox 左缘对齐（根已 pad 12）。
        const int bar_x = px(bounds.Right() - 3.0f);
        const int popup_y = px(bounds.Bottom() + 6.0f);
        const int thumb_y = popup_y + px(12.0f);
        const int drag_y = popup_y + px(240.0f);
        SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(bar_x, thumb_y));
        SendMessageW(hwnd, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(bar_x, drag_y));
        SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(bar_x, drag_y));
        const int item_x = px(bounds.x + 40.0f);
        const int item_y = popup_y + px(20.0f);
        SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(item_x, item_y));
        SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(item_x, item_y));
        Check(combo.SelectedIndex() > 1000, "combo popup scrollbar drag scrolls list");
        window.Close();
    }
    {
        TestComboBox jump;
        jump.AddItems({L"Apple", L"Banana", L"Apricot", L"Cherry"});
        jump.OnChar(L'B');
        Check(jump.SelectedIndex() == 1, "combo typeahead jumps to Banana");
        jump.OnChar(L'a');
        Check(jump.SelectedIndex() == 1, "combo typeahead prefix stays Banana");
        jump.OnAnimate(1.5f);
        jump.OnChar(L'A');
        Check(jump.SelectedIndex() == 0, "combo typeahead Apple after timeout");
        jump.OnChar(L'A');
        Check(jump.SelectedIndex() == 2, "combo typeahead repeated letter cycles Apricot");
    }
    {
        TestComboBox grouped;
        grouped.Items({L"Apple", L"Pear", L"Carrot", L"Kale"});
        grouped.Groups({{L"f", L"Fruit", 2}, {L"v", L"Veg", 2}});
        Check(grouped.Groups().size() == 2, "combo groups stored");
        Window window(L"combo-grouped", {360.0f, 360.0f});
        auto& combo = window.Root().Add<TestComboBox>();
        combo.Items({L"Apple", L"Pear", L"Carrot", L"Kale"})
            .Groups({{L"f", L"Fruit", 2}, {L"v", L"Veg", 2}});
        window.Show();
        HWND hwnd = static_cast<HWND>(window.NativeHandle());
        UpdateWindow(hwnd);
        combo.Focus();
        combo.OnKey(VK_RETURN);
        SendMessageW(hwnd, WM_KEYDOWN, VK_END, 0);
        SendMessageW(hwnd, WM_KEYDOWN, VK_RETURN, 0);
        Check(combo.SelectedIndex() == 3, "combo grouped popup END selects last item");
        window.Close();
    }
    {
        TestComboBox multi;
        multi.AddItems({L"Glow", L"Spotlight", L"Specular", L"Ambient"});
        multi.MultiSelect(true);
        Check(!multi.Editable(), "combo multi disables editable");
        multi.Editable(true);
        Check(!multi.Editable(), "combo multi rejects editable");
        multi.SelectedIndices({0, 2});
        Check(multi.SelectionCount() == 2, "combo multi selection count");
        Check(multi.IsSelected(0) && multi.IsSelected(2) && !multi.IsSelected(1),
              "combo multi IsSelected");
        Check(multi.ChildCount() == 2, "combo multi hosts two chips");
        Check(multi.SelectedText() == L"Glow, Specular", "combo multi SelectedText joins");
        multi.ClearSelection();
        Check(multi.SelectionCount() == 0 && multi.ChildCount() == 0, "combo multi clear chips");
        Window window(L"combo-multi", {360.0f, 360.0f});
        auto& combo = window.Root().Add<TestComboBox>();
        combo.AddItems({L"Glow", L"Spotlight", L"Specular"}).MultiSelect(true);
        window.Show();
        HWND hwnd = static_cast<HWND>(window.NativeHandle());
        UpdateWindow(hwnd);
        combo.Focus();
        combo.OnKey(VK_RETURN);
        SendMessageW(hwnd, WM_KEYDOWN, VK_SPACE, 0);
        SendMessageW(hwnd, WM_KEYDOWN, VK_DOWN, 0);
        SendMessageW(hwnd, WM_KEYDOWN, VK_SPACE, 0);
        Check(combo.SelectionCount() == 2, "combo multi popup space toggles without closing");
        window.Close();
    }
    {
        Window window(L"combo-multi-open", {520.0f, 360.0f});
        window.Root().Padding(12.0f, 8.0f);
        auto& combo = window.Root().Add<TestComboBox>();
        combo.AddItems({L"Glow", L"Spotlight", L"Specular", L"Ambient", L"Carbon", L"Void"})
            .MultiSelect(true)
            .SelectedIndices({0, 2, 4});
        window.Show();
        HWND hwnd = static_cast<HWND>(window.NativeHandle());
        UpdateWindow(hwnd);
        combo.Focus();
        combo.OnKey(VK_RETURN);
        UpdateWindow(hwnd);
        const Rect bounds = combo.AbsoluteBounds();
        const float scale = static_cast<float>(GetDpiForWindow(hwnd)) / 96.0f;
        auto px = [scale](float dip) { return static_cast<int>(dip * scale + 0.5f); };
        const int item_x = px(bounds.x + 40.0f);
        const int item_y = px(bounds.Bottom() + 6.0f + 20.0f);
        SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(item_x, item_y));
        SendMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(item_x, item_y));
        Check(combo.SelectionCount() == 2 && !combo.IsSelected(0),
              "combo multi popup opens on first item not scrolled to last");
        window.Close();
    }
    {
        TestComboBox chips;
        chips.AddItems({L"Glow", L"Spotlight", L"Specular", L"Ambient", L"Carbon", L"Void"})
            .MultiSelect(true)
            .SelectedIndices({0, 1, 2, 3, 4, 5});
        const Size unconstrained = chips.Measure({1.0e5f, 1.0e5f}, theme);
        Check(unconstrained.h <= theme.input_height + 0.5f,
              "combo unconstrained measure stays one chip row");
        TestRoot root;
        auto& row = root.Add<Row>();
        auto& combo = row.Add<TestComboBox>();
        combo.AddItems({L"Glow", L"Spotlight", L"Specular", L"Ambient", L"Carbon", L"Void"})
            .MultiSelect(true)
            .SelectedIndices({0, 1, 2, 3, 4, 5})
            .Grow();
        root.Measure({640.0f, 400.0f}, theme);
        root.Arrange({0.0f, 0.0f, 640.0f, 400.0f});
        Check(combo.Bounds().h <= theme.input_height + 0.5f,
              "combo multi chips one row when grow row is wide");
        Check(combo.Bounds().w > 400.0f, "combo multi grow fills wide row");
    }
    {
        TestCheckBox box(L"x");
        box.ThreeState(true);
        box.OnMouseUp({2.0f, 2.0f}, 0x0001);
        Check(box.State() == CheckState::Checked, "tri-state click checks");
        box.OnMouseUp({2.0f, 2.0f}, 0x0001);
        Check(box.State() == CheckState::Indeterminate, "tri-state click mixed");
        box.OnMouseUp({2.0f, 2.0f}, 0x0001);
        Check(box.State() == CheckState::Unchecked, "tri-state click clears");
        box.OnKey(VK_SPACE);
        Check(box.State() == CheckState::Checked, "tri-state space checks");
        TestCheckBox bin(L"y");
        bin.State(CheckState::Indeterminate);
        bin.OnMouseUp({2.0f, 2.0f}, 0x0001);
        Check(bin.State() == CheckState::Checked, "binary mixed click checks");
        int fires = 0;
        bin.OnToggled([&](bool) { ++fires; });
        bin.Checked(false);
        Check(!bin.Checked() && fires == 0, "checkbox Checked silent");
    }
    {
        TestToggleButton btn(L"Bold");
        int fires = 0;
        btn.OnToggled([&](bool) { ++fires; });
        btn.OnMouseUp({0.0f, 0.0f}, 0x0001);
        Check(btn.Checked() && fires == 1, "toggle button clicks on");
        btn.OnKey(VK_SPACE);
        Check(!btn.Checked() && fires == 2, "toggle button space off");
        btn.Checked(true);
        Check(btn.Checked() && fires == 2, "toggle Checked silent");
    }

    {
        using namespace std::chrono;
        TestDatePicker picker;
        picker.Range(year{2024} / January / day{10}, year{2024} / January / day{20});
        picker.Value(year{2024} / January / day{1});
        Check(picker.Value() && *picker.Value() == year{2024} / January / day{10},
              "date picker clamps minimum");
        picker.Value(year{2024} / February / day{29});
        Check(picker.Value() && *picker.Value() == year{2024} / January / day{20},
              "date picker clamps maximum");
        picker.Value(std::nullopt);
        Check(!picker.Value(), "date picker nullable value");
    }
    {
        using namespace std::chrono;
        TestTimePicker picker;
        picker.Range(hours{9}, hours{17}).MinuteIncrement(15);
        picker.Value(hours{8});
        Check(picker.Value() && picker.Value()->count() == 9 * 60,
              "time picker clamps minimum");
        picker.Value(hours{18});
        Check(picker.Value() && picker.Value()->count() == 17 * 60,
              "time picker clamps maximum");
        picker.MinuteIncrement(7);
        Check(picker.MinuteIncrement() == 7, "time picker preserves minute increment");
        picker.DisplayMode(TimeDisplayMode::TwelveHour);
        Check(picker.DisplayMode() == TimeDisplayMode::TwelveHour,
              "time picker supports explicit twelve-hour display");
        picker.DisplayMode(TimeDisplayMode::TwentyFourHour);
        Check(picker.DisplayMode() == TimeDisplayMode::TwentyFourHour,
              "time picker supports explicit twenty-four-hour display");
        TestTimePicker precise;
        Check(precise.MinuteIncrement() == 1, "time picker defaults to one-minute precision");
    }
    {
        using namespace std::chrono;
        Window window(L"time-picker-wheel", {320.0f, 360.0f});
        auto& picker = window.Root().Add<TimePicker>();
        picker.Value(hours{9} + minutes{45}).MinuteIncrement(15);
        window.Show();
        HWND hwnd = static_cast<HWND>(window.NativeHandle());
        UpdateWindow(hwnd);

        const auto click_picker = [&] {
            const Rect bounds = picker.AbsoluteBounds();
            ClickDip(window, {bounds.x + 24.0f, bounds.y + bounds.h * 0.5f});
        };

        click_picker();
        window.DispatchMouseMove({190.0f, 130.0f});
        WheelDip(hwnd, {190.0f, 130.0f}, -WHEEL_DELTA);
        window.DispatchKey(VK_RETURN);
        Check(picker.Value() && picker.Value()->count() == 9 * 60,
              "time minute wheel does not change hour");

        picker.Value(hours{23});
        click_picker();
        window.DispatchMouseMove({60.0f, 130.0f});
        WheelDip(hwnd, {60.0f, 130.0f}, -WHEEL_DELTA);
        window.DispatchKey(VK_RETURN);
        Check(picker.Value() && picker.Value()->count() == 0,
              "time hour wheel wraps 23 to 0");
        window.Close();
    }
    {
        using namespace std::chrono;
        Window window(L"time-picker-capture", {320.0f, 360.0f});
        auto& picker = window.Root().Add<TimePicker>();
        picker.Value(hours{9} + minutes{30});
        window.Show();
        UpdateWindow(static_cast<HWND>(window.NativeHandle()));

        const Rect bounds = picker.AbsoluteBounds();
        ClickDip(window, {bounds.x + 24.0f, bounds.y + bounds.h * 0.5f});
        ClickDip(window, {148.0f, bounds.Bottom() + 6.0f + 256.0f});
        Check(GetCapture() == nullptr, "time picker selection releases mouse capture");
        window.Close();
    }
    {
        using namespace std::chrono;
        Window window(L"date-picker-years", {340.0f, 380.0f});
        auto& picker = window.Root().Add<DatePicker>();
        picker.Value(year{2026} / August / day{31});
        window.Show();
        UpdateWindow(static_cast<HWND>(window.NativeHandle()));

        const Rect bounds = picker.AbsoluteBounds();
        ClickDip(window, {bounds.x + 24.0f, bounds.y + bounds.h * 0.5f});
        const float popup_y = bounds.Bottom() + 6.0f;
        ClickDip(window, {154.0f, popup_y + 28.0f});
        ClickDip(window, {154.0f, popup_y + 28.0f});
        ClickDip(window, {292.0f, popup_y + 28.0f});
        ClickDip(window, {52.0f, popup_y + 82.0f});
        window.DispatchKey(VK_RETURN);
        window.DispatchKey(VK_RETURN);
        Check(picker.Value() && picker.Value()->year() == year{2033},
              "date picker pages and selects later year");
        window.Close();
    }
    {
        using namespace std::chrono;
        Window window(L"date-picker-capture", {340.0f, 380.0f});
        auto& picker = window.Root().Add<DatePicker>();
        picker.Value(year{2026} / August / day{31});
        window.Show();
        UpdateWindow(static_cast<HWND>(window.NativeHandle()));

        const Rect bounds = picker.AbsoluteBounds();
        ClickDip(window, {bounds.x + 24.0f, bounds.y + bounds.h * 0.5f});
        // 2026-08-02 位于第二行第一列；点击日期格会在 OnMouseDown 内关闭弹层。
        ClickDip(window, {34.0f, bounds.Bottom() + 6.0f + 129.0f});
        Check(GetCapture() == nullptr, "date picker selection releases mouse capture");
        window.Close();
    }
    {
        TestCommandBar bar;
        int invoked = 0;
        bool checked = false;
        bar.Items({{L"run", L"Run", icon::kPlay, CommandBarItemType::Toggle},
                   {L"sep", L"", L"", CommandBarItemType::Separator},
                   {L"copy", L"Copy", icon::kCopy, CommandBarItemType::Action, true, false, true}})
            .OnInvoked([&](std::wstring_view id, bool value) {
                if (id == L"run") { ++invoked; checked = value; }
            });
        bar.Measure({160.0f, 40.0f}, theme);
        bar.Arrange({0.0f, 0.0f, 160.0f, 40.0f});
        bar.OnKey(VK_RETURN);
        Check(invoked == 1 && checked, "command bar toggle invokes");
        Check(bar.Items()[2].overflow_only, "command bar overflow-only retained");
        Check(bar.Measure({800.0f, 40.0f}, theme).w == 800.0f, "command bar fills available width");
    }
    {
        TestNavigationView nav;
        int changed = 0;
        nav.Items({{L"home", L"Home", icon::kLayers},
                   {L"disabled", L"Disabled", icon::kWarning, NavigationItemType::Item, false},
                   {L"files", L"Files", icon::kFolder}});
        nav.SelectedId(L"home")
            .OnSelectionChanged([&](std::wstring_view) { ++changed; });
        nav.Measure({680.0f, 300.0f}, theme);
        nav.Arrange({0.0f, 0.0f, 680.0f, 300.0f});
        Check(nav.SelectedId() == L"home", "navigation selected id");
        nav.OnKey(VK_DOWN);
        nav.OnKey(VK_RETURN);
        Check(nav.SelectedId() == L"files" && changed == 1,
              "navigation keyboard skips disabled item");
        nav.DisplayMode(NavigationDisplayMode::Compact);
        Check(nav.DisplayMode() == NavigationDisplayMode::Compact,
              "navigation compact mode");
        nav.Height(300.0f);
        Check(nav.Measure({620.0f, 1.0e5f}, theme).h == 300.0f,
              "navigation constrained height in scroll content");
        Check(nav.Measure({740.0f, 1.0e5f}, theme).w == 740.0f,
              "navigation width follows parent layout");
    }
    {
        TestNavigationView nav;
        nav.DisplayMode(NavigationDisplayMode::Expanded)
            .Items({{L"", L"Basics", L"", NavigationItemType::Header},
                    {L"home", L"Home", icon::kLayers},
                    {L"off", L"Off", icon::kWarning, NavigationItemType::Item, false},
                    {L"files", L"Files", icon::kFolder}});
        nav.Measure({400.0f, 360.0f}, theme);
        nav.Arrange({0.0f, 0.0f, 400.0f, 360.0f});
        Check(nav.CursorAt({20.0f, 28.0f}) == CursorShape::Hand, "navigation toggle cursor");
        Check(nav.CursorAt({24.0f, 62.0f}) == CursorShape::Arrow, "navigation header cursor");
        Check(nav.CursorAt({24.0f, 96.0f}) == CursorShape::Hand, "navigation item cursor");
        Check(nav.CursorAt({24.0f, 136.0f}) == CursorShape::Arrow, "navigation disabled cursor");
        Check(nav.CursorAt({24.0f, 280.0f}) == CursorShape::Arrow, "navigation empty pane cursor");
        Check(nav.CursorAt({300.0f, 96.0f}) == CursorShape::Arrow, "navigation content cursor");
    }
    {
        TestNavigationView nav;
        int expanded = 0;
        nav.Items({{L"root", L"Root", icon::kFolder, NavigationItemType::Item, true, L"", true},
                   {L"child", L"Child", icon::kFolder, NavigationItemType::Item, true, L"root", false},
                   {L"leaf", L"Leaf", icon::kCode, NavigationItemType::Item, true, L"child", false},
                   {L"orphan", L"Orphan", icon::kWarning, NavigationItemType::Item, true, L"missing", false}})
            .OnExpandedChanged([&](std::wstring_view id, bool on) {
                if (id == L"child" && on) ++expanded;
            });
        nav.Measure({800.0f, 360.0f}, theme);
        nav.Arrange({0.0f, 0.0f, 800.0f, 360.0f});
        Check(nav.ItemExpanded(L"root"), "navigation honors initial expanded state");
        nav.ItemExpanded(L"child", true);
        Check(nav.ItemExpanded(L"child") && expanded == 1, "navigation expands nested item");
        nav.RevealId(L"leaf");
        Check(nav.SelectedId() == L"leaf" && nav.ItemExpanded(L"root") && nav.ItemExpanded(L"child"),
              "navigation reveal expands ancestors");
        nav.SelectedId(L"orphan");
        Check(nav.SelectedId() == L"orphan", "navigation invalid parent becomes root");
    }
    {
        TestRoot root;
        auto& nav = root.Add<TestNavigationView>();
        nav.Items({{L"root", L"Root", icon::kFolder, NavigationItemType::Item, true, L"", true},
                   {L"one", L"One", icon::kCode, NavigationItemType::Item, true, L"root"},
                   {L"two", L"Two", icon::kCode, NavigationItemType::Item, true, L"root"},
                   {L"three", L"Three", icon::kCode, NavigationItemType::Item, true, L"root"},
                   {L"four", L"Four", icon::kCode, NavigationItemType::Item, true, L"root"}})
            .FooterItems({{L"settings", L"Settings", icon::kSettings}})
            .DisplayMode(NavigationDisplayMode::Expanded)
            .Height(236.0f);
        root.Measure({520.0f, 236.0f}, theme);
        root.Arrange({0.0f, 0.0f, 520.0f, 236.0f});
        nav.OnMouseDown({24.0f, 208.0f}, kBtnL);
        Check(nav.SelectedId() == L"settings", "navigation footer stays above scrolling tree");
    }
    {
        TestNavigationView nav;
        int searches = 0;
        nav.Items({{L"root", L"Root", icon::kFolder, NavigationItemType::Item, true, L"", true},
                   {L"leaf", L"UniqueLeaf", icon::kCode, NavigationItemType::Item, true, L"root"},
                   {L"other", L"Zzz", icon::kClock}})
            .OnSearch([&](std::wstring_view) { ++searches; })
            .SearchEnabled(true)
            .SelectedId(L"root");
        nav.RevealId(L"leaf");
        Check(nav.PathIds().size() == 2 && nav.PathIds()[0] == L"root" && nav.PathIds()[1] == L"leaf",
              "navigation path ids follow ancestors");
        Check(nav.PathTitles().size() == 2 && nav.PathTitles()[1] == L"UniqueLeaf",
              "navigation path titles follow ancestors");
        nav.SelectedId(L"root");
        nav.SearchQuery(L"unique");
        Check(searches == 1 && nav.SearchQuery() == L"unique", "navigation search query applied");
        nav.Measure({800.0f, 360.0f}, theme);
        nav.Arrange({0.0f, 0.0f, 800.0f, 360.0f});
        nav.OnKey(VK_DOWN);
        nav.OnKey(VK_RETURN);
        Check(nav.SelectedId() == L"leaf", "navigation search keeps matching lineage selectable");
        TestBreadcrumb crumb;
        nav.BindBreadcrumb(crumb);
        Check(crumb.Count() == 2, "navigation bind breadcrumb copies path");
        crumb.Measure({400.0f, 40.0f}, theme);
        crumb.Arrange({0.0f, 0.0f, 400.0f, 40.0f});
        crumb.OnMouseDown({12.0f, 20.0f}, kBtnL);
        Check(nav.SelectedId() == L"root", "navigation breadcrumb click selects ancestor");
        nav.SelectedId(L"other");
        Check(nav.PathIds().size() == 1 && nav.PathIds()[0] == L"other",
              "navigation root item path is one segment");
        nav.ShowBreadcrumb(true);
        Check(nav.BreadcrumbVisible(), "navigation owned breadcrumb enabled");
        nav.DisplayMode(NavigationDisplayMode::Compact);
        nav.Measure({800.0f, 360.0f}, theme);
        nav.Arrange({0.0f, 0.0f, 800.0f, 360.0f});
        Check(nav.SearchBox() && !nav.SearchBox()->Visible(),
              "navigation compact hides search box");
        nav.OnMouseDown({24.0f, 60.0f}, kBtnL);
        nav.Measure({800.0f, 360.0f}, theme);
        nav.Arrange({0.0f, 0.0f, 800.0f, 360.0f});
        Check(nav.SearchBox() && nav.SearchBox()->Visible(),
              "navigation compact search click expands pane");
    }
    {
        ImageView image;
        const std::array<std::byte, 4> invalid{};
        Check(image.Status() == ImageStatus::Empty, "image empty status");
        Check(!image.LoadMemory(invalid) && !image.HasImage() &&
                  image.Status() == ImageStatus::Failed,
              "image invalid memory rejected");
        image.CornerRadius(8.0f).Stretch(ImageStretch::UniformToFill);
        Check(image.NaturalPixelSize().w == 0.0f, "image empty natural size");
    }

    {
        TestTextBox box;
        Check(box.OnChar(L'\xD83D') && box.Text().empty(), "emoji high surrogate waits");
        Check(box.OnChar(L'\xDE00') && box.Text() == L"\xD83D\xDE00",
              "emoji surrogate pair inserts together");
        box.OnKey(VK_BACK);
        Check(box.Text().empty(), "emoji backspace removes surrogate pair");

        box.Text(L"A\xD83D\xDE00" L"B");
        box.OnKey(VK_LEFT);
        Check(box.CaretIndex() == 3, "emoji caret moves before trailing text");
        box.OnKey(VK_LEFT);
        Check(box.CaretIndex() == 1, "emoji caret skips surrogate pair");

        box.Text(L"\xD83D\xDC69\u200D\xD83D\xDCBB");
        box.OnKey(VK_BACK);
        Check(box.Text().empty(), "emoji backspace removes ZWJ sequence");

        box.Text(L"\xD83C\xDDE8\xD83C\xDDF3");
        box.OnKey(VK_BACK);
        Check(box.Text().empty(), "emoji backspace removes flag pair");

        box.OnChar(L'\xD83D');
        box.OnKey(VK_LEFT);
        box.OnChar(L'\xDE00');
        Check(box.Text().empty(), "interrupted surrogate input is discarded");
    }

    {
        TestTextBox box;
        box.OnImeCompose(L"ceshi", 5, {});
        Check(box.Text().empty(), "ime compose does not commit latin");
        Check(box.ImeComposing(), "ime compose marks composing");
        box.OnKey(VK_BACK);
        Check(box.Text().empty(), "ime compose swallows backspace");
        box.OnImeEnd();
        Check(!box.ImeComposing() && box.Text().empty(), "ime cancel leaves text empty");

        box.Text(L"ab");
        box.OnImeCompose(L"ce", 2, {});
        box.OnImeCommit(L"\u6d4b\u8bd5");
        Check(box.Text() == L"ab\u6d4b\u8bd5", "ime commit inserts at caret");
        Check(!box.ImeComposing(), "ime commit ends composing");
        Check(box.CaretIndex() == 4, "ime commit moves caret after result");
    }

    {
        TestTextBox box;
        box.Text(L"hello world");
        box.SelectWordAt(1);
        Check(box.AnchorIndex() == 0 && box.CaretIndex() == 5, "double-click selects word");
        box.SelectLineAt(0);
        Check(box.AnchorIndex() == 0 && box.CaretIndex() == box.Text().size(),
              "triple-click selects line");
        box.Text(L"hello world");
        box.OnKey(VK_END);
        const size_t left = box.WordLeft(box.CaretIndex());
        Check(left == 6, "word left lands on world");
        // Ctrl 由 GetKeyState 读取，测试直接裁词：End 后删到词首。
        box.Text(L"hello world");
        box.OnKey(VK_END);
        const size_t start = box.WordLeft(box.Text().size());
        Check(box.Text().substr(start) == L"world", "word left range is last word");
    }

    {
        TestTextBox box;
        box.MaxLength(3);
        box.OnChar(L'a');
        box.OnChar(L'b');
        box.OnChar(L'c');
        box.OnChar(L'd');
        Check(box.Text() == L"abc", "maxlength truncates insert");
    }

    {
        TestTextBox box;
        box.Mask(L"000-0000");
        for (wchar_t ch : std::wstring(L"1234567")) box.OnChar(ch);
        Check(box.Text() == L"123-4567", "mask inserts literals");
        box.OnChar(L'8');
        Check(box.Text() == L"123-4567", "mask rejects overflow");
    }

    {
        TestTextBox box;
        for (wchar_t ch : std::wstring(L"hello")) box.OnChar(ch);
        Check(box.Text() == L"hello", "typed word");
        Check(box.Undo() && box.Text().empty(), "undo groups a typed word");
        box.OnChar(L'a');
        box.OnChar(L' ');
        box.OnChar(L'b');
        Check(box.Undo() && box.Text() == L"a ", "space starts a new undo group");
        Check(box.Undo() && box.Text() == L"a", "space is its own group");
    }

    {
        TestTextBox box;
        box.Placeholder(L"Name").FloatingLabel(true);
        const Theme th = MakeTheme();
        const Size sz = box.Measure({200.0f, 80.0f}, th);
        Check(sz.h > th.input_height + 1.0f, "floating label grows height");
        box.EnsureEditMenu();
        Check(box.HasContextMenu(), "textbox installs edit menu");
    }

    {
        Window window(L"cjk-caret", {560.0f, 140.0f});
        auto& box = window.Root().Add<TestTextBox>();
        const std::wstring cjk = L"输入中文，拼音应出现在框内";
        box.Text(cjk);
        window.Show();
        window.LayoutNow();
        const float caret = box.CaretX(cjk.size());
        const Size measured = box.MeasureText(cjk, TextRole::Body);
        Check(caret > 40.0f, "cjk caret has advance");
        Check(caret + 48.0f < box.AbsoluteBounds().w, "cjk caret is not at field edge");
        Check(caret < measured.w + 8.0f, "cjk caret not past measure");
        Check(caret > measured.w * 0.45f, "cjk caret tracks text width");
        window.Close();
    }

    {
        Window window(L"mixed-caret", {560.0f, 140.0f});
        auto& box = window.Root().Add<TestTextBox>();
        const std::wstring mixed = L"on this line.的";
        box.Text(mixed);
        window.Show();
        window.LayoutNow();
        const float caret = box.CaretX(mixed.size());
        const Size measured = box.MeasureText(mixed, TextRole::Body);
        Check(caret > 40.0f, "mixed caret has advance");
        Check(caret + 48.0f < box.AbsoluteBounds().w, "mixed caret is not at field edge");
        Check(caret < measured.w + 8.0f, "mixed caret not past measure");
        Check(caret > measured.w * 0.70f, "mixed caret sits after last glyph");
        window.Close();
    }

    {
        TestRoot root;
        auto& list = root.Add<TestList>();
        list.ItemCount(10);
        list.MultiSelect(true);
        root.Measure({300.0f, 400.0f}, theme);
        root.Arrange({0.0f, 0.0f, 300.0f, 400.0f});
        const float rh = theme.list_row_height;
        auto click = [&](ptrdiff_t row, uint32_t mods) {
            list.OnMouseDown({10.0f, (static_cast<float>(row) + 0.5f) * rh}, kBtnL | mods);
        };
        int changed = 0;
        list.OnSelectionChanged([&](ptrdiff_t, ptrdiff_t) { ++changed; });

        click(2, 0);
        Check(list.SelectedIndex() == 2 && SameIndices(list.SelectedIndices(), {2}),
              "multi plain click selects");
        click(5, kBtnCtrl);
        Check(SameIndices(list.SelectedIndices(), {2, 5}), "multi ctrl click adds");
        click(5, kBtnCtrl);
        Check(SameIndices(list.SelectedIndices(), {2}), "multi ctrl click toggles off");
        click(5, kBtnCtrl);
        click(7, kBtnShift);
        Check(SameIndices(list.SelectedIndices(), {2, 3, 4, 5, 6, 7}),
              "multi shift click range from anchor");
        click(1, 0);
        Check(SameIndices(list.SelectedIndices(), {1}), "multi plain click replaces");
        list.OnKey(VK_DOWN);
        Check(SameIndices(list.SelectedIndices(), {2}), "multi arrow moves selection");
        list.SelectedIndices({3, 1, 3, 99, -1, 4});
        Check(SameIndices(list.SelectedIndices(), {1, 3, 4}), "multi set indices sorted unique");
        list.OnKey(VK_ESCAPE);
        Check(list.SelectionCount() == 0 && list.SelectedIndex() == -1, "multi escape clears");
        Check(changed >= 8, "multi selection changed notified");
        list.SelectedIndices({8, 9});
        list.ItemCount(9);
        Check(SameIndices(list.SelectedIndices(), {8}), "multi shrink filters out-of-range");
        Check(list.SelectedIndex() >= 0 && list.SelectedIndex() < 9,
              "multi shrink clamps focus row");
    }
    {
        TestRoot root;
        auto& list = root.Add<TestList>();
        list.ItemCount(10);
        root.Measure({300.0f, 400.0f}, theme);
        root.Arrange({0.0f, 0.0f, 300.0f, 400.0f});
        list.SelectedIndex(4);
        Check(SameIndices(list.SelectedIndices(), {4}), "single SelectedIndices");
        list.SelectedIndices({6, 2});
        Check(SameIndices(list.SelectedIndices(), {6}), "single set indices takes last");
        const float rh = theme.list_row_height;
        list.OnMouseDown({10.0f, 0.5f * rh}, kBtnL);
        Check(SameIndices(list.SelectedIndices(), {0}), "single click selects row");
        list.SelectedIndex(9);
        list.ItemCount(10);
        list.SelectedIndex(9);
        list.ItemCount(5);
        Check(list.SelectedIndex() == 4, "single shrink clamps selected");
    }
    {
        TestRoot root;
        auto& table = root.Add<TestTable>();
        table.AddColumn(L"A", 100.0f);
        table.AddColumn(L"B");
        static const std::vector<int> kValues{9, 5, 7, 1};
        table.CellText([](size_t row, size_t col, std::wstring& out) {
            if (col == 0) out = std::to_wstring(kValues[row]);
            else out = L"b" + std::to_wstring(row);
        });
        table.Sortable(0, true);
        table.Sortable(1, true);
        table.RowCount(4);
        root.Measure({300.0f, 400.0f}, theme);
        root.Arrange({0.0f, 0.0f, 300.0f, 400.0f});

        // 升序 1,5,7,9 → 视图到数据的置换 3,1,2,0
        table.SortBy(0, 1);
        Check(table.SortedColumn() == 0 && table.SortDirection() == 1, "sort state asc");
        Check(table.DataRowAt(0) == 3 && table.DataRowAt(1) == 1 && table.DataRowAt(2) == 2 &&
                  table.DataRowAt(3) == 0,
              "sort asc permutation");
        table.SortBy(0, -1);
        Check(table.DataRowAt(0) == 0 && table.DataRowAt(3) == 3, "sort desc permutation");
        table.SortBy(0, 0);
        Check(table.SortedColumn() == -1 && table.DataRowAt(2) == 2, "sort clear identity");

        // 表头点击循环 无 → 升 → 降 → 无；换列直接升序
        auto header_click = [&](float x) {
            table.OnMouseDown({x, 16.0f}, kBtnL);
            table.OnMouseUp({x, 16.0f}, 0);
        };
        header_click(50.0f);
        Check(table.SortDirection() == 1, "header click asc");
        header_click(50.0f);
        Check(table.SortDirection() == -1, "header click desc");
        header_click(50.0f);
        Check(table.SortDirection() == 0, "header click clears");
        header_click(200.0f);
        Check(table.SortedColumn() == 1 && table.SortDirection() == 1, "header other column asc");
        table.SortBy(1, 0);

        // 选中跟随数据行：数据 0（"9"）升序后从视图 0 落到视图 3
        table.SelectedIndex(0);
        table.SortBy(0, 1);
        Check(table.SelectedIndex() == 3 && table.DataRowAt(3) == 0, "selection follows sort");
        table.SortBy(0, 0);

        // 列宽拖动：边界 ±4 DIP 命中，起点把弹性列固化为实宽
        auto drag = [&](float from, float to) {
            table.OnMouseDown({from, 16.0f}, kBtnL);
            table.OnMouseMove({to, 16.0f}, kBtnL);
            table.OnMouseUp({to, 16.0f}, 0);
        };
        drag(100.0f, 160.0f);
        Check(Near(table.ColumnWidth(0), 160.0f), "column drag widens");
        drag(160.0f, 10.0f);
        Check(Near(table.ColumnWidth(0), 40.0f), "column drag min clamp");
        drag(40.0f, 500.0f);
        Check(Near(table.ColumnWidth(0), 500.0f), "column drag creates horizontal overflow");
        Check(table.MaxHorizontalScroll() > 0.0f, "column drag exposes horizontal scroll");
    }
    {
        TestRoot root;
        auto& table = root.Add<TestTable>();
        table.AddColumn(L"A", 80.0f);
        table.AddColumn(L"B");
        table.AddColumn(L"C", 80.0f);
        table.RowCount(1);
        root.Measure({400.0f, 220.0f}, theme);
        root.Arrange({0.0f, 0.0f, 400.0f, 220.0f});
        table.OnMouseDown({80.0f, 16.0f}, kBtnL);
        const float snapped_b = table.ColumnWidth(1);
        Check(snapped_b > 90.0f, "resize snaps flex neighbor to pixels");
        table.OnMouseMove({140.0f, 16.0f}, kBtnL);
        table.OnMouseUp({140.0f, 16.0f}, 0);
        Check(Near(table.ColumnWidth(0), 140.0f), "resize only widens dragged column");
        Check(Near(table.ColumnWidth(1), snapped_b), "resize leaves neighbor width");
        Check(Near(table.ColumnWidth(2), 80.0f), "resize leaves trailing column");
    }
    {
        TestRoot root;
        auto& table = root.Add<TestTable>();
        table.AddColumn(L"Name", 90.0f).Frozen();
        table.AddColumn(L"Status", 100.0f);
        table.AddColumn(L"Owner", 80.0f).Frozen();
        table.AddColumn(L"Updated", 140.0f);
        table.RowCount(4);
        root.Measure({260.0f, 220.0f}, theme);
        root.Arrange({0.0f, 0.0f, 260.0f, 220.0f});

        Check(table.ColumnFrozen(0) && !table.ColumnFrozen(1) && table.ColumnFrozen(2),
              "table retains arbitrary frozen flags");
        Check(table.ColumnAt(20.0f) == 0 && table.ColumnAt(110.0f) == 2 &&
                  table.ColumnAt(180.0f) == 1,
              "frozen columns group left in declaration order");
        Check(table.MaxHorizontalScroll() > 0.0f, "table nonfrozen columns overflow");
        table.ScrollToX(10000.0f);
        Check(Near(table.HorizontalOffset(), table.MaxHorizontalScroll()),
              "table horizontal offset clamps");
        Check(table.ColumnAt(180.0f) == 3, "scrolled visual column maps to original index");

        table.CellText([](size_t row, size_t col, std::wstring& out) {
            out = std::to_wstring(row) + L":" + std::to_wstring(col);
        }).CellEditEnabled(true);
        table.OnMouseDoubleClick({180.0f, 46.0f});
        Check(table.Editor() != nullptr && table.Editor()->AbsoluteBounds().x >= 170.0f,
              "cell editor follows scrolled visual column");
    }
    {
        TestRoot root;
        auto& table = root.Add<TestTable>();
        table.AddColumn(L"Name", 90.0f);
        table.AddColumn(L"Status", 100.0f);
        table.RowCount(2);
        root.Measure({260.0f, 200.0f}, theme);
        root.Arrange({0.0f, 0.0f, 260.0f, 200.0f});
        Check(!table.ColumnFrozen(0), "header pin starts unfrozen");
        table.OnMouseDown({78.0f, 16.0f}, kBtnL);
        Check(table.ColumnFrozen(0) && !table.ColumnFrozen(1), "header pin freezes column");
        table.OnMouseDown({78.0f, 16.0f}, kBtnL);
        Check(!table.ColumnFrozen(0), "header pin unfreezes column");
    }
    {
        TestRoot root;
        auto& table = root.Add<TestTable>();
        table.AddColumn(L"A", 200.0f);
        table.AddColumn(L"B");
        table.RowCount(1);
        root.Measure({120.0f, 200.0f}, theme);
        root.Arrange({0.0f, 0.0f, 120.0f, 200.0f});
        table.Arrange({0.0f, 0.0f, 120.0f, 200.0f});
        Check(table.MaxHorizontalScroll() > 0.0f, "flex column keeps min width when overflow");
    }
    {
        TestRoot root;
        auto& table = root.Add<TestTable>();
        static std::vector<uint8_t> on(4, 0);
        table.AddColumn(L"#", 64.0f).Frozen();
        table.AddColumn(L"On", 56.0f).CheckBox(
            [](size_t row) { return row < on.size() && on[row] != 0; },
            [](size_t row, bool value) {
                if (row < on.size()) on[row] = value ? 1 : 0;
            });
        table.AddColumn(L"Note", 80.0f);
        table.AddColumn(L"Action", 108.0f).Button(L"Run", [](size_t) {});
        table.RowCount(4);
        root.Measure({400.0f, 220.0f}, theme);
        root.Arrange({0.0f, 0.0f, 400.0f, 220.0f});
        table.ColumnFrozen(1, true);
        Check(table.ColumnFrozen(0) && table.ColumnFrozen(1), "freeze on keeps hash frozen");
        bool kinds_ok = true;
        bool frozen_checkbox = false;
        bool frozen_button = false;
        for (size_t i = 0; i < table.SlotCount(); ++i) {
            Control* ctl = table.SlotControl(i);
            if (!ctl || !ctl->Visible()) continue;
            const size_t col = table.SlotColumn(i);
            if (col == 1 && dynamic_cast<CheckBox*>(ctl) == nullptr) kinds_ok = false;
            if (col == 3 && dynamic_cast<Button*>(ctl) == nullptr) kinds_ok = false;
            const float x = ctl->AbsoluteBounds().x;
            if (col == 1 && dynamic_cast<CheckBox*>(ctl) && x < 130.0f) frozen_checkbox = true;
            if (col == 3 && dynamic_cast<Button*>(ctl) && x < 130.0f) frozen_button = true;
        }
        Check(kinds_ok, "freeze on keeps checkbox/button on their columns");
        Check(frozen_checkbox && !frozen_button, "frozen band has on checkbox not action button");
    }
    {
        TestRoot root;
        auto& table = root.Add<TestTable>();
        table.AddColumn(L"#", 64.0f).Frozen();
        table.AddColumn(L"On", 56.0f).CheckBox([](size_t) { return false; }, [](size_t, bool) {});
        table.AddColumn(L"Note", 80.0f).TextBox([](size_t row) { return L"Note " + std::to_wstring(row); },
                                               [](size_t, std::wstring) {});
        table.AddColumn(L"Action", 108.0f).Button(L"Run", [](size_t) {});
        table.RowCount(8);
        root.Measure({420.0f, 280.0f}, theme);
        root.Arrange({0.0f, 0.0f, 420.0f, 280.0f});
        table.ColumnFrozen(2, true);
        table.ColumnFrozen(3, true);
        table.MoveColumn(3, 2);
        bool kinds_ok = true;
        float action_x = 1.0e9f;
        float note_x = 1.0e9f;
        for (size_t i = 0; i < table.SlotCount(); ++i) {
            Control* ctl = table.SlotControl(i);
            if (!ctl || !ctl->Visible()) continue;
            const size_t col = table.SlotColumn(i);
            if (col == 1 && dynamic_cast<CheckBox*>(ctl) == nullptr) kinds_ok = false;
            if (col == 2 && dynamic_cast<TextBox*>(ctl) == nullptr) kinds_ok = false;
            if (col == 3 && dynamic_cast<Button*>(ctl) == nullptr) kinds_ok = false;
            if (col == 2 && ctl->AbsoluteBounds().x < note_x) note_x = ctl->AbsoluteBounds().x;
            if (col == 3 && ctl->AbsoluteBounds().x < action_x) action_x = ctl->AbsoluteBounds().x;
        }
        Check(kinds_ok, "frozen reorder keeps widget kinds on data columns");
        Check(action_x < note_x, "frozen reorder puts action widgets left of note");
    }

    {
        TestRoot root;
        auto& table = root.Add<TestTable>();
        table.AddColumn(L"Name", 80.0f);
        table.AddColumn(L"Env", 80.0f);
        table.AddColumn(L"Load", 60.0f).Progress([](size_t row) {
            return row == 0 ? 0.5f : 1.0f;
        });
        table.CellText([](size_t row, size_t col, std::wstring& out) {
            if (col == 0) out = (row < 2) ? L"a" : L"b";
            else if (col == 1) out = (row % 2 == 0) ? L"Prod" : L"Stage";
            else out.clear();
        });
        table.RowCount(4);
        table.Sortable(0, true);
        table.Sortable(1, true);
        root.Measure({240.0f, 220.0f}, theme);
        root.Arrange({0.0f, 0.0f, 240.0f, 220.0f});

        table.ColumnVisible(1, false);
        table.ColumnVisible(2, false);
        Check(table.ColumnVisible(0) && !table.ColumnVisible(1) && !table.ColumnVisible(2),
              "column hide");
        table.ColumnVisible(0, false);
        Check(table.ColumnVisible(0), "cannot hide last remaining column");
        table.ColumnVisible(1, true);
        table.ColumnVisible(2, true);

        table.MoveColumn(1, 0);
        Check(table.ColumnAt(20.0f) == 1, "move column visual order");
        table.MoveColumn(1, 0);

        table.SortBy(0, 1);
        table.SortBy(1, -1, true);
        Check(table.SortKeys().size() == 2 && table.SortKeys()[1].col == 1,
              "multi-column sort keys");
        table.SortBy(0, 0);

        table.GroupBy(1);
        Check(table.GroupCount() == 2, "group by environment");
        Check(table.GroupExpanded(0), "group starts expanded");
        Check(table.RowTop(0) > 20.0f, "first grouped row sits below group header");
        table.GroupExpanded(0, false);
        Check(!table.GroupExpanded(0), "group collapse");
        table.GroupBy(-1);

        table.Footer(true).Aggregate(0, ColumnAggregate::Count);
        table.Aggregate(2, ColumnAggregate::Average);
        Check(table.Footer(), "footer enabled");
        Check(table.ColumnAggregateKind(0) == ColumnAggregate::Count, "footer aggregate");

        table.SelectedIndex(0);
        table.ActiveColumn(0);
        table.OnKey(VK_RIGHT);
        Check(table.ActiveColumn() == 1, "keyboard cell right");
        table.OnKey(VK_LEFT);
        Check(table.ActiveColumn() == 0, "keyboard cell left");
        Check(table.ShowContextMenu({12.0f, 10.0f}), "header context menu path");
        Check(!table.ShowContextMenu({12.0f, 80.0f}), "body has no column menu");
    }
}

void TestImageViewRendering() {
    OffscreenRenderer source;
    if (!source.Init(4, 2)) {
        Check(false, "image source renderer init");
        return;
    }
    ID2D1DeviceContext2* source_dc = source.BeginDraw();
    Painter source_painter;
    source_painter.BeginFrame(source_dc, &UiText(), 1.0f);
    source_painter.FillRect({0.0f, 0.0f, 2.0f, 2.0f}, Color{1.0f, 0.0f, 0.0f, 1.0f});
    source_painter.FillRect({2.0f, 0.0f, 2.0f, 2.0f}, Color{0.0f, 1.0f, 0.0f, 1.0f});
    source_painter.EndFrame();
    Check(source.EndDraw(), "image source enddraw");
    const wchar_t* path = L"lumen_image_view_source.png";
    Check(source.SavePNG(path), "image source save");
    source.Shutdown();

    ImageView image;
    image.SetBounds({0.0f, 0.0f, 80.0f, 80.0f});
    image.CornerRadius(10.0f).Stretch(ImageStretch::UniformToFill);
    Check(image.LoadFile(path) && image.HasImage(), "image file loads");
    Check(image.NaturalPixelSize().w == 4.0f && image.NaturalPixelSize().h == 2.0f,
          "image natural pixel size");
    const std::array<std::byte, 4> invalid{};
    Check(!image.LoadMemory(invalid) && image.HasImage() && image.Status() == ImageStatus::Ready,
          "image failure keeps previous source");

    TestRoot root;
    auto& shown = root.Add<ImageView>();
    Check(shown.LoadFile(path), "image render source loads");
    shown.SetBounds({0.0f, 0.0f, 80.0f, 80.0f});
    shown.Stretch(ImageStretch::UniformToFill).CornerRadius(10.0f);
    root.Measure({80.0f, 80.0f}, MakeTheme());
    root.Arrange({0.0f, 0.0f, 80.0f, 80.0f});
    OffscreenRenderer target;
    if (!target.Init(80, 80)) {
        Check(false, "image target renderer init");
        DeleteFileW(path);
        return;
    }
    ID2D1DeviceContext2* dc = target.BeginDraw();
    Painter painter;
    painter.BeginFrame(dc, &UiText(), 1.0f);
    painter.FillRect({0.0f, 0.0f, 80.0f, 80.0f}, Color{0.0f, 0.0f, 0.0f, 1.0f});
    DrawControlTree(painter, MakeTheme(), &root);
    painter.EndFrame();
    Check(target.EndDraw(), "image target enddraw");
    Color left{}, right{}, corner{};
    target.ReadPixel(20, 40, left);
    target.ReadPixel(60, 40, right);
    target.ReadPixel(1, 1, corner);
    Check(left.r > left.g + 0.25f && right.g > right.r + 0.25f,
          "image colors preserved");
    Check(corner.r < 0.05f && corner.g < 0.05f, "image rounded clip");
    target.Shutdown();
    DeleteFileW(path);
}

// —— 新控件（弹层/导航/轻量展示）交互逻辑断言 ——
struct TestNumberBox : NumberBox {
    using NumberBox::OnChar;
    using NumberBox::OnKey;
    using NumberBox::OnFocusChanged;
    using NumberBox::CursorAt;
    using NumberBox::OnMouseDown;
    using NumberBox::OnMouseUp;
    using NumberBox::OnAnimate;
};
struct TestRepeatButton : RepeatButton {
    using RepeatButton::RepeatButton;
    using RepeatButton::OnMouseDown;
    using RepeatButton::OnMouseUp;
    using RepeatButton::OnMouseMove;
    using RepeatButton::OnKey;
    using RepeatButton::OnAnimate;
};
struct TestRating : Rating {
    using Rating::OnMouseDown;
};
struct TestSkeleton : Skeleton {
    using Skeleton::OnAnimate;
};
struct TestTreeView : TreeView {
    using TreeView::OnKey;
    using TreeView::OnMouseDown;
};
struct TestTreeTable : TreeTable {
    using TreeTable::OnKey;
    using TreeTable::OnMouseDown;
    using TreeTable::Measure;
    using TreeTable::Arrange;
};
struct TestSplitButton : SplitButton {
    using SplitButton::SplitButton;
    using SplitButton::OnMouseUp;
    using SplitButton::Measure;
};
struct TestTitleBar : TitleBar {
    using TitleBar::Measure;
    using TitleBar::Arrange;
};
struct TestDropDownButton : DropDownButton {
    using DropDownButton::DropDownButton;
    using DropDownButton::OnMouseDown;
    using DropDownButton::OnMouseUp;
    using DropDownButton::OnKey;
};
struct TestTeachingTip : TeachingTip {
    using TeachingTip::Measure;
    using TeachingTip::Arrange;
    using TeachingTip::OnMouseDown;
    using TeachingTip::OnMouseUp;
};
struct TestInfoBadge : InfoBadge {
    using InfoBadge::Measure;
};
struct TestFileDropZone : FileDropZone {
    using FileDropZone::Measure;
    using FileDropZone::OnFileDrag;
    using FileDropZone::OnFileDrop;
};
struct TestFormField : FormField {
    using FormField::FormField;
    using FormField::Measure;
    using FormField::Arrange;
};
struct TestStatusBar : StatusBar {
    using StatusBar::Measure;
    using StatusBar::Arrange;
    using StatusBar::OnMouseDown;
};
struct TestHotkeyBox : HotkeyBox {
    using HotkeyBox::HotkeyBox;
    using HotkeyBox::Measure;
    using HotkeyBox::OnKey;
    using HotkeyBox::OnChar;
    using HotkeyBox::OnMouseDown;
};
struct TestGroupBox : GroupBox {
    using GroupBox::GroupBox;
    using GroupBox::Measure;
    using GroupBox::Arrange;
};
struct TestViewbox : Viewbox {
    using Viewbox::Viewbox;
    using Viewbox::Measure;
    using Viewbox::Arrange;
    using Viewbox::MapToChildren;
};
struct TestPasswordBox : PasswordBox {
    using PasswordBox::PasswordBox;
    using PasswordBox::OnMouseDown;
    using PasswordBox::CursorAt;
};

bool Near2(double a, double b) { return std::fabs(a - b) <= 1e-6; }

template <typename T>
void root_measure_arrange2(T& control, const Theme& theme) {
    const Size desired = control.Measure({2000.0f, 2000.0f}, theme);
    control.Measure({desired.w + 40.0f, desired.h + 40.0f}, theme);
    control.Arrange({0.0f, 0.0f, desired.w + 40.0f, desired.h + 40.0f});
}

void TestExtras() {
    const Theme theme = MakeTheme();
    {
        Check(ToastKindGlyph(ToastKind::Default) == nullptr, "toast default uses dot");
        Check(std::wstring(ToastKindGlyph(ToastKind::Success)) == icon::kCheckMark,
              "toast success glyph");
        Check(std::wstring(ToastKindGlyph(ToastKind::Warning)) == icon::kWarning,
              "toast warning glyph");
        Check(std::wstring(ToastKindGlyph(ToastKind::Error)) == icon::kShield, "toast error glyph");
        Check(std::wstring(ToastKindGlyph(ToastKind::Info)) == icon::kInfo, "toast info glyph");
        ToastData data;
        Check(data.duration > 2.0f && data.kind == ToastKind::Default, "toast data defaults");
        data.text = L"saved";
        data.action = L"undo";
        data.duration = 0.0f;
        data.kind = ToastKind::Success;
        Check(data.action == L"undo" && data.duration <= 0.0f, "toast data action and persist");
    }
    {
        auto tip = std::make_unique<ToolTip>();
        Check(tip->MaxWidth() > 200.0f, "tooltip default max width");
        Check(tip->Closable(), "tooltip closable by default");
        tip->Add<Label>(L"Title", TextRole::CaptionStrong);
        tip->Add<Label>(L"Body text that wraps").Wrap(true);
        Check(tip->ChildCount() == 2, "tooltip children");
        TestRoot host;
        auto& measured = host.Add<ToolTip>();
        measured.Add<Label>(L"Title", TextRole::CaptionStrong);
        measured.Add<Label>(L"Body text that wraps").Wrap(true);
        host.Measure({280.0f, 2000.0f}, theme);
        Check(measured.DesiredSize().w > 8.0f && measured.DesiredSize().h > 16.0f,
              "tooltip measure");
        Button btn(L"Host");
        btn.ToolTip(std::move(tip));
        Check(btn.ToolTipContent() != nullptr, "tooltip content owned");
        Check(btn.ToolTip().empty(), "string cleared by custom");
        Check(btn.HasToolTip(), "has custom tooltip");
        btn.ToolTip(L"plain");
        Check(btn.ToolTipContent() == nullptr, "string clears custom");
        Check(btn.ToolTip() == L"plain", "string tooltip");
        Check(btn.HasToolTip(), "has string tooltip");
        btn.ToolTip(L"");
        Check(!btn.HasToolTip(), "empty clears tooltip");
    }
    {
        TestNumberBox box;
        box.Range(0.0, 10.0);
        box.OnChar(L'1');
        box.OnChar(L'2');
        Check(box.Text() == L"12", "number accepts digits");
        box.OnChar(L'a');
        Check(box.Text() == L"12", "number rejects letter");
        box.OnFocusChanged(false);
        Check(Near2(box.Value(), 10.0) && box.Text() == L"10", "number clamp+format on blur");
        box.Value(7.0);
        box.OnKey(VK_UP);
        Check(Near2(box.Value(), 8.0), "number step up");
        box.OnKey(VK_DOWN);
        Check(Near2(box.Value(), 7.0), "number step down");
        box.Value(99.0);
        Check(Near2(box.Value(), 10.0), "number value clamps to range");
    }
    {
        // spin 区光标为箭头，文本区仍为 IBeam（需要 Arrange 后的绝对坐标）。
        TestRoot root;
        auto& box = root.Add<TestNumberBox>();
        box.Range(0.0, 100.0).Value(42.0);
        root.Measure({300.0f, 60.0f}, theme);
        root.Arrange({0.0f, 0.0f, 300.0f, 60.0f});
        const Rect b = box.AbsoluteBounds();
        Check(box.CursorAt({b.Right() - 4.0f, b.h * 0.5f}) == CursorShape::Arrow,
              "number spin zone cursor");
        Check(box.CursorAt({b.w * 0.4f, b.h * 0.5f}) == CursorShape::IBeam,
              "number text zone cursor");
        box.Value(10.0);
        box.OnMouseDown({b.w - 4.0f, 4.0f}, 0x0001);
        Check(Near2(box.Value(), 11.0), "number spin press steps once");
        box.OnAnimate(0.10f);
        Check(Near2(box.Value(), 11.0), "number spin waits delay");
        box.OnAnimate(0.35f);
        Check(box.Value() > 11.0 + 0.5, "number spin repeats after delay");
        box.OnMouseUp({b.w - 4.0f, 4.0f}, 0);
        const double held = box.Value();
        box.OnAnimate(1.0f);
        Check(Near2(box.Value(), held), "number spin stops on release");
    }
    {
        RepeatHold hold;
        hold.delay = 0.20f;
        hold.interval = 0.05f;
        hold.Press();
        Check(hold.Tick(0.10f, true) == 0, "repeat hold delay");
        Check(hold.Tick(0.11f, true) == 1, "repeat hold first extra");
        Check(hold.Tick(0.05f, true) == 1, "repeat hold interval");
        Check(hold.Tick(0.20f, false) == 0, "repeat hold pauses when inactive");
        hold.Release();
        Check(hold.Tick(1.0f, true) == 0, "repeat hold released");
    }
    {
        TestRoot root;
        auto& btn = root.Add<TestRepeatButton>(L"+");
        btn.Delay(0.20f).Interval(0.05f);
        int clicks = 0;
        btn.OnClick([&] { ++clicks; });
        root.Measure({200.0f, 80.0f}, theme);
        root.Arrange({0.0f, 0.0f, 200.0f, 80.0f});
        btn.OnMouseDown({8.0f, 8.0f}, 0x0001);
        Check(clicks == 1, "repeat button fires on press");
        btn.OnAnimate(0.10f);
        Check(clicks == 1, "repeat button waits delay");
        btn.OnAnimate(0.15f);
        Check(clicks >= 2, "repeat button starts after delay");
        btn.OnMouseMove({-10.0f, 8.0f}, 0x0001);
        const int paused = clicks;
        btn.OnAnimate(0.50f);
        Check(clicks == paused, "repeat button pauses outside");
        btn.OnMouseMove({8.0f, 8.0f}, 0x0001);
        btn.OnAnimate(0.20f);
        Check(clicks > paused, "repeat button resumes inside");
        btn.OnMouseUp({8.0f, 8.0f}, 0);
        const int released = clicks;
        btn.OnAnimate(1.0f);
        Check(clicks == released, "repeat button stops on release");
        btn.OnKey(VK_SPACE);
        Check(clicks == released + 1, "repeat button space fires");
    }
    {
        TestRoot root;
        auto& rating = root.Add<TestRating>();
        int rated = -1;
        rating.OnRated([&](int v) { rated = v; });
        root.Measure({400.0f, 60.0f}, theme);
        root.Arrange({0.0f, 0.0f, 400.0f, 60.0f});
        rating.Value(2.0);
        rating.OnMouseDown({3 * 24 + 10.0f, 10.0f}, 0x0001);
        Check(Near2(rating.Value(), 4.0) && rated == 4, "rating click sets star");
        rating.Value(2.0);
        rating.OnMouseDown({1 * 24 + 10.0f, 10.0f}, 0x0001);
        Check(Near2(rating.Value(), 1.0), "rating click same star cancels");
        rating.ReadOnly(true);
        rating.OnMouseDown({3 * 24 + 10.0f, 10.0f}, 0x0001);
        Check(Near2(rating.Value(), 1.0), "rating readonly ignores clicks");
    }
    {
        TestRoot root;
        auto& crumb = root.Add<TestBreadcrumb>();
        crumb.AddItem(L"库").AddItem(L"项目").AddItem(L"设置");
        size_t nav = 999;
        crumb.OnNavigate([&](size_t index) { nav = index; });
        root.Measure({400.0f, 40.0f}, theme);
        root.Arrange({0.0f, 0.0f, 400.0f, 40.0f});
        crumb.OnMouseDown({crumb.DesiredSize().w - 10.0f, 20.0f}, 0x0001);
        Check(crumb.SelectedIndex() == 2 && nav == 2, "breadcrumb click navigates");
        crumb.SelectedIndex(0);
        Check(crumb.SelectedIndex() == 0 && nav == 2, "breadcrumb programmatic silent");
    }
    {
        TestSkeleton skeleton;
        skeleton.Lines(3);
        Check(skeleton.OnAnimate(0.1f), "skeleton animates while active");
        skeleton.Active(false);
        Check(!skeleton.OnAnimate(0.1f), "skeleton stops when inactive");
    }
    {
        AutoSuggestBox box;
        box.Suggestions([](std::wstring_view query) {
            std::vector<std::wstring> all{L"Button", L"Badge", L"Breadcrumb", L"Slider"};
            std::vector<std::wstring> out;
            for (const std::wstring& item : all) {
                if (std::wstring_view(item).find(query) == 0) out.push_back(item);
            }
            return out;
        });
        box.MaxSuggestions(8);
        Check(box.QuerySuggestions().size() == 4, "suggest empty query all");
        box.Text(L"B");
        Check(box.QuerySuggestions().size() == 3, "suggest prefix filter");
        box.MaxSuggestions(2);
        Check(box.QuerySuggestions().size() == 2, "suggest limit");
        TestAutoSuggestBox typed;
        typed.OnChar(L'a');
        typed.OnChar(L'b');
        typed.OnChar(L'c');
        Check(typed.Text() == L"abc", "suggest types characters");
        typed.OnKey(VK_BACK);
        typed.OnKey(VK_BACK);
        Check(typed.Text() == L"a", "suggest backspace repeats without refocus");
    }
    {
        TestTreeView tree;
        tree.Roots(2);
        tree.ChildCount([](size_t id) { return id == 0 ? 2 : 0; });
        tree.ChildAt([](size_t id, size_t index) { (void)id; return 100 + index; });
        tree.ItemText([](size_t id, std::wstring& s) { s = L"n" + std::to_wstring(id); });
        Check(tree.VisibleCount() == 2, "tree roots flattened");
        tree.Expand(0);
        Check(tree.VisibleCount() == 4, "tree expand flattens children");
        Check(tree.VisibleIdAt(1) == 100 && tree.VisibleIdAt(2) == 101, "tree child order");
        tree.SelectedId(101);
        Check(tree.SelectedId() == 101, "tree select by id");
        tree.SelectedId(100);
        tree.OnKey(VK_DOWN);
        Check(tree.SelectedId() == 101, "tree arrow moves down");
        tree.Expand(0, false);
        Check(tree.VisibleCount() == 2 && tree.SelectedId() == 0,
              "tree collapse clamps selection to parent");
        tree.Expand(0);
        tree.SelectedId(1);
        tree.Expand(0, false);
        Check(tree.VisibleCount() == 2 && tree.SelectedId() == 1,
              "tree collapse keeps visible sibling root");
        tree.RevealId(101);
        Check(tree.VisibleCount() == 4, "tree reveal expands ancestors");
        int activated = -1;
        tree.OnActivate([&](size_t id) { activated = static_cast<int>(id); });
        tree.SelectedId(0);
        tree.OnKey(VK_RETURN);
        Check(activated == 0, "tree enter activates");
        // 点击命中：chevron 区折叠/展开（局部坐标），文本区仅选中。
        TestRoot click_root;
        auto& click_tree = click_root.Add<TestTreeView>();
        click_tree.Roots(2);
        click_tree.ChildCount([](size_t id) { return id == 0 ? 2 : 0; });
        click_tree.ChildAt([](size_t id, size_t index) { (void)id; return 100 + index; });
        click_tree.Expand(0);
        click_root.Measure({300.0f, 200.0f}, theme);
        click_root.Arrange({0.0f, 0.0f, 300.0f, 200.0f});
        click_tree.OnMouseDown({8.0f, 14.0f}, 0x0001);    // 行 0 chevron 区 x∈[4,20)
        Check(!click_tree.Expanded(0), "tree chevron click collapses");
        click_tree.OnMouseDown({8.0f, 14.0f}, 0x0001);
        Check(click_tree.Expanded(0), "tree chevron click expands");
        click_tree.OnMouseDown({60.0f, 14.0f}, 0x0001);   // 行 0 文本区
        Check(click_tree.SelectedId() == 0, "tree row click selects");
        click_tree.OnMouseDown({28.0f, 14.0f + 28.0f}, 0x0001);   // 行 1（子节点，无子 → 仅选中）
        Check(click_tree.SelectedId() == 100, "tree child row click selects");
    }
    {
        // 平铺数据入口与回调式等价；ExpandAll 全展开。
        TestTreeView flat;
        flat.SetFlatData({TreeView::kNone, TreeView::kNone, 0, 0, 1, 3, 3});
        flat.ItemText([](size_t id, std::wstring& s) { s = L"n" + std::to_wstring(id); });
        Check(flat.Roots() == 2 && flat.VisibleCount() == 2, "flat data roots");
        flat.ExpandAll();
        Check(flat.VisibleCount() == 7, "flat expand all");
        Check(flat.VisibleIdAt(1) == 2 && flat.VisibleIdAt(2) == 3, "flat child order");
        TestTreeView mirror;
        mirror.Roots(2);
        mirror.ChildCount([](size_t id) {
            return id == 0 ? 2 : (id == 1 ? 1 : (id == 3 ? 2 : 0));
        });
        mirror.ChildAt([](size_t id, size_t index) {
            return id == 0 ? (index == 0 ? 2 : 3) : (id == 1 ? 4 : (index == 0 ? 5 : 6));
        });
        mirror.ExpandAll();
        Check(mirror.VisibleCount() == 7 && mirror.VisibleIdAt(4) == 6,
              "provider model matches flat data");
        TestTreeView sparse;
        sparse.SetFlatData({TreeView::kNone, 0, TreeView::kNone});
        Check(sparse.Roots() == 2 && sparse.VisibleCount() == 2, "flat sparse roots");
        Check(sparse.VisibleIdAt(0) == 0 && sparse.VisibleIdAt(1) == 2, "flat sparse root ids");
        sparse.ExpandAll();
        Check(sparse.VisibleCount() == 3 && sparse.VisibleIdAt(1) == 1, "flat sparse expand");
    }
    {
        TestTreeTable table;
        table.AddColumn(L"Name", 180.0f).AddColumn(L"Kind", 80.0f);
        table.SetFlatData({TreeTable::kNone, TreeTable::kNone, 0, 0});
        table.ItemText([](size_t id, std::wstring& s) { s = L"n" + std::to_wstring(id); });
        Check(table.ColumnCount() == 2, "tree table columns");
        Check(table.Roots() == 2 && table.VisibleCount() == 2, "tree table roots");
        table.Expand(0);
        Check(table.VisibleCount() == 4, "tree table expand flattens children");
        Check(table.VisibleIdAt(1) == 2 && table.VisibleIdAt(2) == 3, "tree table child order");
        table.SelectedId(3);
        Check(table.SelectedId() == 3, "tree table select by id");
        table.OnKey(VK_UP);
        Check(table.SelectedId() == 2, "tree table arrow moves up");
        table.Expand(0, false);
        Check(table.VisibleCount() == 2 && table.SelectedId() == 0,
              "tree table collapse clamps selection to parent");
        table.RevealId(3);
        Check(table.VisibleCount() == 4, "tree table reveal expands");
        int activated = -1;
        table.OnActivate([&](size_t id) { activated = static_cast<int>(id); });
        table.SelectedId(1);
        table.OnKey(VK_RETURN);
        Check(activated == 1, "tree table enter activates");
        table.CollapseAll();
        table.ExpandAll();
        Check(table.VisibleCount() == 4 && table.VisibleIdAt(1) == 2,
              "tree table expand all");
        TestRoot click_root;
        auto& click = click_root.Add<TestTreeTable>();
        click.AddColumn(L"Name");
        click.SetFlatData({TreeTable::kNone, 0});
        click.Expand(0);
        click_root.Measure({400.0f, 200.0f}, theme);
        click_root.Arrange({0.0f, 0.0f, 400.0f, 200.0f});
        click.OnMouseDown({8.0f, 32.0f + 14.0f}, 0x0001);
        Check(!click.Expanded(0), "tree table chevron click collapses");
        click.OnMouseDown({8.0f, 32.0f + 14.0f}, 0x0001);
        Check(click.Expanded(0), "tree table chevron click expands");
        click.OnMouseDown({80.0f, 32.0f + 14.0f}, 0x0001);
        Check(click.SelectedId() == 0, "tree table row click selects");
    }
    {
        // 密码框揭示开关：切换掩码态不改真实文本。
        TestRoot root;
        auto& pwd = root.Add<TestPasswordBox>(L"secret");
        pwd.Revealable(true);
        root.Measure({220.0f, 40.0f}, theme);
        root.Arrange({0.0f, 0.0f, 220.0f, 40.0f});
        Check(pwd.Password() && !pwd.Revealed(), "password starts masked");
        pwd.OnMouseDown({pwd.AbsoluteBounds().w - 12.0f, 20.0f}, 0x0001);
        Check(pwd.Revealed() && !pwd.Password() && pwd.Text() == L"secret",
              "password reveal keeps text");
        pwd.OnMouseDown({pwd.AbsoluteBounds().w - 12.0f, 20.0f}, 0x0001);
        Check(!pwd.Revealed() && pwd.Password(), "password hide restores mask");
        Check(pwd.CursorAt({pwd.AbsoluteBounds().w - 8.0f, 20.0f}) == CursorShape::Hand,
              "password reveal zone cursor");
    }
    {
        // Table 单元格编辑：双击进入 → 提交回调（数据行换算）→ Esc 取消不回调。
        TestRoot root;
        auto& table = root.Add<TestTable>();
        table.AddColumn(L"A", 100.0f);
        table.AddColumn(L"B");
        static const std::vector<int> kCellValues{9, 5, 7, 1};
        table.CellText([](size_t row, size_t col, std::wstring& out) {
            if (col == 0) out = std::to_wstring(kCellValues[row]);
            else out = L"b" + std::to_wstring(row);
        });
        table.RowCount(4);
        table.CellEditEnabled(true);
        int edited_row = -1;
        std::wstring edited_text;
        table.OnCellEdited([&](size_t r, int c, std::wstring t) {
            edited_row = static_cast<int>(r);
            edited_text = t;
            (void)c;
        });
        root.Measure({300.0f, 400.0f}, theme);
        root.Arrange({0.0f, 0.0f, 300.0f, 400.0f});
        table.OnMouseDoubleClick({50.0f, 32.0f + 14.0f});
        Check(table.Editor() != nullptr && table.Editor()->Text() == L"9",
              "cell edit begins with cell text");
        table.Editor()->Text(L"99");
        table.Commit();
        Check(edited_row == 0 && edited_text == L"99", "cell edit commits data row");
        table.OnMouseDoubleClick({50.0f, 32.0f + 14.0f});
        table.Cancel();
        Check(edited_row == 0 && edited_text == L"99", "cell edit cancel skips callback");
    }
    {
        struct OriginPanel : Panel {
            using Panel::Measure;
            using Panel::Arrange;
        };
        OriginPanel root;
        auto& table = root.Add<TestTable>();
        table.AddColumn(L"A", 100.0f);
        table.AddColumn(L"B");
        static const std::vector<int> kOff{1, 2};
        table.CellText([](size_t row, size_t col, std::wstring& out) {
            if (col == 0) out = std::to_wstring(kOff[row]);
            else out = L"x";
        });
        table.RowCount(2);
        table.CellEditEnabled(true);
        table.SetBounds({80.0f, 0.0f, 220.0f, 200.0f});
        root.Measure({400.0f, 200.0f}, theme);
        root.Arrange({0.0f, 0.0f, 400.0f, 200.0f});
        table.OnMouseDoubleClick({50.0f, 32.0f + 14.0f});
        Check(table.Editor() != nullptr, "offset table editor exists");
        if (table.Editor()) {
            const Rect ed = table.Editor()->AbsoluteBounds();
            Check(ed.x >= 80.0f && ed.x < 160.0f, "cell editor parent-relative at x=80");
        }
    }
    {
        // SplitView：展开 220 / 折叠 Compact 48；侧栏子级不得溢到内容区。
        TestRoot root;
        auto& shell = root.Add<TestSplitView>();
        auto& nav = shell.Pane().Add<Button>(L"总览", ButtonKind::Transparent);
        nav.Glyph(icon::kLayers).SizeClass(ButtonSize::Small);
        shell.Content().Add<Label>(L"总览", TextRole::BodyStrong);
        root.Measure({600.0f, 200.0f}, theme);
        root.Arrange({0.0f, 0.0f, 600.0f, 200.0f});
        Check(Near(shell.Pane().AbsoluteBounds().w, 220.0f), "splitview pane length");
        shell.Collapse(true);
        for (int i = 0; i < 40; ++i) shell.OnAnimate(0.2f);   // 缓动收敛
        Check(Near(shell.Pane().AbsoluteBounds().w, 48.0f, 0.5f), "splitview collapse compact");
        const Rect pane = shell.Pane().AbsoluteBounds();
        Check(nav.AbsoluteBounds().Right() <= pane.Right() + 0.5f,
              "splitview compact pane children stay inside");
        Check(shell.Content().AbsoluteBounds().x + 0.5f >= pane.Right(),
              "splitview compact content starts after pane");
    }
    {
        TestRoot root;
        auto& wrap = root.Add<WrapPanel>().Gap(8.0f, 8.0f);
        auto& a = wrap.Add<Panel>().SetBounds({0.0f, 0.0f, 80.0f, 20.0f});
        auto& b = wrap.Add<Panel>().SetBounds({0.0f, 0.0f, 80.0f, 20.0f});
        auto& c = wrap.Add<Panel>().SetBounds({0.0f, 0.0f, 80.0f, 20.0f});
        root.Measure({200.0f, 80.0f}, theme);
        root.Arrange({0.0f, 0.0f, 200.0f, 80.0f});
        Check(Near(a.Bounds().x, 0.0f) && Near(b.Bounds().x, 88.0f), "wrap first row");
        Check(c.Bounds().y >= 20.0f, "wrap third item to next row");
    }
    {
        TestColorPicker picker;
        picker.Color(Color::Hex(0xFF0000));
        const Color red = picker.Color();
        Check(red.r > 0.9f && red.g < 0.15f && red.b < 0.15f, "colorpicker set red");
        picker.Measure({280.0f, 240.0f}, theme);
        picker.Arrange({0.0f, 0.0f, 280.0f, 240.0f});
        Check(picker.Measure({280.0f, 240.0f}, theme).w == 280.0f, "colorpicker fills available width");
        Check(picker.ChildCount() == 1, "colorpicker hosts text box");
        const auto& field = static_cast<const TextBox&>(picker.Child(0));
        Check(field.Text() == L"#FF0000", "colorpicker hex field tracks rgb");
        picker.OnKey(VK_DOWN);
        Check(picker.Color().r + picker.Color().g + picker.Color().b < red.r + red.g + red.b + 0.001f,
              "colorpicker value down");
        Check(picker.Hex(L"#00AA00") && picker.Hex() == L"#00AA00", "colorpicker set hex");
        Check(field.Text() == L"#00AA00", "colorpicker hex field after SetHex");
        picker.Color(Color::Hex(0xFF0000));
        Check(picker.Hex() == L"#FF0000", "colorpicker hex from rgb");
    }
    {
        using namespace std::chrono;
        TestCalendarView calendar;
        calendar.Value(year{2026} / August / day{31});
        calendar.Measure({308.0f, 320.0f}, theme);
        calendar.Arrange({0.0f, 0.0f, 308.0f, 320.0f});
        calendar.OnKey(VK_LEFT);
        calendar.OnKey(VK_RETURN);
        Check(calendar.Value() && calendar.Value()->day() == day{30}, "calendar key previous day");
    }
    {
        using namespace std::chrono;
        TestCalendarView calendar;
        calendar.Value(year{2026} / August / day{31});
        calendar.Measure({308.0f, 320.0f}, theme);
        calendar.Arrange({0.0f, 0.0f, 308.0f, 320.0f});
        calendar.OnMouseDown({154.0f, 28.0f}, 0x0001);
        calendar.OnMouseDown({52.0f, 82.0f}, 0x0001);
        calendar.OnKey(VK_RETURN);
        Check(calendar.Value() && calendar.Value()->month() == January,
              "calendar title opens month grid");
    }
    {
        using namespace std::chrono;
        TestCalendarView calendar;
        calendar.Value(year{2026} / August / day{31});
        calendar.Measure({308.0f, 320.0f}, theme);
        calendar.Arrange({0.0f, 0.0f, 308.0f, 320.0f});
        calendar.OnMouseDown({154.0f, 28.0f}, 0x0001);
        calendar.OnMouseDown({154.0f, 28.0f}, 0x0001);
        calendar.OnMouseDown({52.0f, 82.0f}, 0x0001);
        calendar.OnKey(VK_RETURN);
        calendar.OnKey(VK_RETURN);
        Check(calendar.Value() && calendar.Value()->year() == year{2021},
              "calendar year grid then month");
    }
    {
        TestChip chip(L"Filter");
        chip.Selectable(true);
        chip.Measure({80.0f, 28.0f}, theme);
        chip.Arrange({0.0f, 0.0f, 80.0f, 28.0f});
        chip.OnKey(VK_SPACE);
        Check(chip.Selected(), "chip space toggles");
        int closed = 0;
        TestChip tag(L"Draft");
        tag.Closable(true).OnClosed([&] { ++closed; });
        tag.Measure({80.0f, 28.0f}, theme);
        tag.Arrange({0.0f, 0.0f, 80.0f, 28.0f});
        tag.OnMouseDown({70.0f, 14.0f}, 0x0001);
        Check(closed == 1, "chip close dismisses");
    }
    {
        TestRoot root;
        auto& grid = root.Add<TestGridView>();
        grid.ItemCount(9).ItemSize({80.0f, 80.0f}).ItemGap(0.0f);
        root.Measure({250.0f, 260.0f}, theme);
        root.Arrange({0.0f, 0.0f, 250.0f, 260.0f});
        grid.OnMouseDown({40.0f, 40.0f}, 0x0001);
        Check(grid.SelectedIndex() == 0, "gridview click first tile");
        grid.OnKey(VK_RIGHT);
        Check(grid.SelectedIndex() == 1, "gridview key next tile");
    }
    {
        Button btn(L"ctx");
        Menu menu;
        menu.AddItem(L"Copy", nullptr);
        btn.ContextMenu(std::move(menu));
        Check(btn.HasContextMenu(), "control context menu stored");
        Check(!btn.ShowContextMenu({0.0f, 0.0f}), "context menu without window no-ops");
    }
    {
        // Pagination：键盘/点击翻页。
        TestRoot root;
        auto& pager = root.Add<TestPagination>();
        pager.PageCount(12).Current(5);
        root.Measure({500.0f, 40.0f}, theme);
        root.Arrange({0.0f, 0.0f, 500.0f, 40.0f});
        pager.OnKey(VK_RIGHT);
        Check(pager.Current() == 6, "pagination key next");
        pager.OnMouseDown({20.0f, 14.0f}, 0x0001);   // 上一页箭头
        Check(pager.Current() == 5, "pagination prev click");
    }
    {
        // MenuBar：装配与无窗口静默。
        TestMenuBar bar;
        Menu a;
        a.AddItem(L"x", nullptr);
        bar.AddMenu(L"File", std::move(a)).AddMenu(L"Edit", Menu{});
        Check(bar.Count() == 2, "menubar add menus");
        root_measure_arrange2(bar, theme);
        bar.OnMouseDown({20.0f, 16.0f}, 0x0001);   // 无窗口：静默
        Check(true, "menubar click headless safe");
    }
    {
        // Carousel：交互翻页回调；编程切换不回调。
        TestRoot root;
        auto& car = root.Add<TestCarousel>();
        car.AddPage<Label>(L"a");
        car.AddPage<Label>(L"b");
        car.AddPage<Label>(L"c");
        Check(car.PageCount() == 3, "carousel pages");
        size_t seen = 999;
        car.OnPageChanged([&](size_t page) { seen = page; });
        car.OnKey(VK_RIGHT);
        Check(car.Current() == 1 && seen == 1, "carousel key page + callback");
        car.Current(2);
        Check(car.Current() == 2 && seen == 1, "carousel programmatic silent");
        root_measure_arrange2(car, theme);
        // 圆点条按局部坐标命中：点第 0 格回第 0 页并回调。
        const Rect cb = car.AbsoluteBounds();
        car.OnMouseDown({cb.w * 0.5f - 14.0f, cb.h - 10.0f}, 0x0001);
        Check(car.Current() == 0 && seen == 0, "carousel dot click navigates");
        car.OnMouseDown({cb.w * 0.5f + 40.0f, cb.h - 10.0f}, 0x0001);   // 圆点条外：不翻页
        Check(car.Current() == 0, "carousel outside strip ignored");
    }
    {
        // Stepper：仅允许回跳，前进不触发。
        TestStepper steps;
        steps.AddStep(L"a").AddStep(L"b").AddStep(L"c");
        steps.Current(2);
        size_t stepped = 999;
        steps.OnStepChanged([&](size_t x) { stepped = x; });
        root_measure_arrange2(steps, theme);
        steps.OnMouseDown({10.0f, 13.0f}, 0x0001);
        Check(steps.Current() == 0 && stepped == 0, "stepper back navigate");
        steps.OnMouseDown({260.0f, 13.0f}, 0x0001);
        Check(steps.Current() == 0 && stepped == 0, "stepper forward blocked");
    }
    {
        // 弹出一行式：未入窗口树时静默（不弹、不崩），DropdownMenu 装配无副作用。
        TestRoot root;
        auto& split = root.Add<TestSplitButton>(L"demo");
        Menu menu;
        menu.AddItem(L"item", nullptr);
        split.DropdownMenu(std::move(menu));
        Check(Menu().PopupTo(split) == -1, "menu popup without window no-ops");
        Check(MenuLabel(L"&Copy") == L"Copy", "menu label strips mnemonic");
        Check(MenuAccessKey(L"&Copy") == L'C', "menu access key");
        Check(MenuLabel(L"Look && Feel") == L"Look & Feel", "menu escaped ampersand");
        Check(MenuAccessKey(L"Look && Feel") == 0, "menu escaped has no access key");
        Check(MenuAccessKey(L"C&omfortable") == L'O', "menu access key not first letter");
        const float p_adv = AdvanceUiText(L"P", TextRole::Body, nullptr);
        const float paste_adv = AdvanceUiText(L"Paste", TextRole::Body, nullptr);
        Check(p_adv > 2.0f && p_adv * 1.6f < paste_adv, "mnemonic letter narrower than word");
        Check(p_adv <= MeasureUiText(L"P", TextRole::Body).w, "advance excludes ink pad");
        Menu grouped;
        grouped.AddHeader(L"View");
        grouped.AddItem(L"Compact", nullptr).Radio().RadioGroup(L"density").Checked(true);
        grouped.AddItem(L"Comfortable", nullptr).Radio().RadioGroup(L"density");
        grouped.AddItem(L"Word wrap", nullptr).Checked(true);
        Check(grouped.Items()[0].header && grouped.Items()[0].disabled, "menu header not interactive");
        Check(grouped.Items()[1].radio && grouped.Items()[1].checked &&
                  grouped.Items()[1].radio_group == L"density",
              "menu radio group compact");
        Check(grouped.Items()[2].radio && !grouped.Items()[2].checked, "menu radio sibling off");
        Check(grouped.Items()[3].checkable && grouped.Items()[3].checked,
              "menu Checked() is checkable");
        root.Measure({200.0f, 44.0f}, theme);
        root.Arrange({0.0f, 0.0f, 200.0f, 44.0f});
        split.OnMouseUp({split.AbsoluteBounds().w - 6.0f, 22.0f}, 0x0001);   // 箭头区：无窗口静默
        Check(split.Text() == L"demo", "split dropdown headless safe");
        TestSplitButton packed(L"Auto deploy");
        packed.Toggle(true).Checked(true);
        const Size pack = packed.Measure({2000.0f, 2000.0f}, theme);
        const float text_w = UiText().MeasureText(L"Auto deploy", TextRole::Body).w;
        Check(pack.w + 0.5f >= 12.0f + 20.0f + text_w + 12.0f + 32.0f,
              "split button checked width includes check and text gap");
    }
    {
        TestTitleBar bar;
        bar.Title(L"LUMEN Gallery").Glyph(L"*").Status(L"86.6 FPS · 33.02 ms");
        bar.Measure({1280.0f, 40.0f}, theme);
        bar.Arrange({0.0f, 0.0f, 1280.0f, 40.0f});
        const float text_w = UiText().MeasureText(L"LUMEN Gallery", TextRole::CaptionStrong).w;
        Check(bar.Content().Bounds().x + 0.5f >= 16.0f + 24.0f + text_w + 12.0f,
              "title bar caption width includes full title");
    }
    {
        // 1x1 opaque PNG — layout must reserve the glyph slot once a bitmap icon loads.
        static constexpr unsigned char kPng1x1[] = {
            0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
            0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01,
            0x08, 0x02, 0x00, 0x00, 0x00, 0x90, 0x77, 0x53, 0xDE, 0x00, 0x00, 0x00,
            0x0C, 0x49, 0x44, 0x41, 0x54, 0x08, 0xD7, 0x63, 0xF8, 0xCF, 0xC0, 0x00,
            0x00, 0x00, 0x03, 0x00, 0x01, 0x00, 0x05, 0xFE, 0xD4, 0xEF, 0x00, 0x00,
            0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82};
        TestTitleBar bar;
        bar.Title(L"LUMEN Gallery");
        Check(bar.LoadIconMemory({reinterpret_cast<const std::byte*>(kPng1x1), sizeof(kPng1x1)}),
              "title bar LoadIconMemory accepts png");
        Check(bar.HasIcon(), "title bar reports bitmap icon");
        bar.Measure({1280.0f, 40.0f}, theme);
        bar.Arrange({0.0f, 0.0f, 1280.0f, 40.0f});
        const float text_w = UiText().MeasureText(L"LUMEN Gallery", TextRole::CaptionStrong).w;
        Check(bar.Content().Bounds().x + 0.5f >= 16.0f + 24.0f + text_w + 12.0f,
              "title bar bitmap icon reserves caption slot");
    }
    {
        TestRoot root;
        auto& dd = root.Add<TestDropDownButton>(L"Export");
        int opened = 0;
        dd.OnDropdown([&] { ++opened; });
        root.Measure({200.0f, 44.0f}, theme);
        root.Arrange({0.0f, 0.0f, 200.0f, 44.0f});
        dd.OnKey(VK_SPACE);
        Check(opened == 1, "dropdown space opens");
        dd.OnKey(VK_DOWN);
        Check(opened == 2, "dropdown down opens");
        dd.OnMouseDown({12.0f, 12.0f}, 0x0001);
        dd.OnMouseUp({12.0f, 12.0f}, 0x0001);
        Check(opened == 3, "dropdown click opens");
        dd.Enabled(false);
        dd.OnKey(VK_SPACE);
        dd.OnMouseDown({12.0f, 12.0f}, 0x0001);
        dd.OnMouseUp({12.0f, 12.0f}, 0x0001);
        Check(opened == 3, "dropdown disabled ignores");
        Menu menu;
        menu.AddItem(L"item", nullptr);
        dd.Enabled(true).DropdownMenu(std::move(menu));
        dd.Open();
        Check(dd.Text() == L"Export", "dropdown menu headless safe");
    }
    {
        TestRoot root;
        auto& box = root.Add<TokenBox>();
        Check(box.AddToken(L"  alpha  ") && box.Tokens().size() == 1 && box.Tokens()[0] == L"alpha",
              "token trim on add");
        Check(!box.AddToken(L"alpha"), "token rejects duplicate");
        box.AllowDuplicates(true);
        Check(box.AddToken(L"alpha") && box.Tokens().size() == 2, "token allows duplicate");
        box.AllowDuplicates(false);
        box.Tokens({L"Source", L"Glow"});
        Check(box.Tokens().size() == 2 && box.Tokens()[1] == L"Glow", "token replace set");
        box.Draft(L"beta");
        Check(box.CommitDraft() && box.Tokens().size() == 3 && box.Tokens()[2] == L"beta",
              "token commit draft");
        box.Draft(L"one, two；three");
        box.CommitDraft();
        Check(box.Tokens().size() == 6 && box.Tokens()[5] == L"three", "token split delimiters");
        Check(box.RemoveLast() && box.Tokens().size() == 5, "token remove last");
        box.MaxTokens(5);
        Check(!box.AddToken(L"overflow"), "token honors max");
        box.ClearTokens();
        Check(box.Tokens().empty(), "token clear");
        box.Tokens({L"Alpha", L"Beta", L"Gamma", L"Delta", L"Epsilon", L"Zeta"});
        root.Measure({220.0f, 400.0f}, theme);
        Check(box.DesiredSize().h > theme.input_height + 8.0f, "token wraps to extra row");
    }
    {
        TestTeachingTip tip;
        tip.Title(L"DropDownButton")
            .Message(L"整颗点击弹出菜单。")
            .Glyph(icon::kSparkle);
        const Size size = tip.Measure({280.0f, 400.0f}, theme);
        Check(size.w >= 160.0f && size.h >= 56.0f, "teaching tip measures card");
        tip.Arrange({20.0f, 40.0f, size.w, size.h});
        int closed = 0;
        tip.OnClosed([&] { ++closed; });
        const float close_x = size.w - 14.0f - 14.0f;
        tip.OnMouseDown({close_x, 16.0f}, 0x0001);
        tip.OnMouseUp({close_x, 16.0f}, 0x0001);
        Check(closed == 1 && !tip.Visible(), "teaching tip close dismisses");
    }
    {
        TestInfoBadge badge;
        Check(badge.Measure({100.0f, 100.0f}, theme).w == 0.0f, "info badge empty measures zero");
        badge.Dot();
        const Size dot = badge.Measure({100.0f, 100.0f}, theme);
        Check(dot.w == 8.0f && dot.h == 8.0f, "info badge dot size");
        badge.Count(7);
        const Size n = badge.Measure({100.0f, 100.0f}, theme);
        Check(n.h == 16.0f && n.w >= n.h, "info badge count pill");
        badge.Count(150);
        const Size overflow = badge.Measure({100.0f, 100.0f}, theme);
        Check(overflow.w > n.w, "info badge overflow wider than 7");
        TestTabs tabs;
        tabs.AddTab({L"inbox", L"Inbox", icon::kMail, false, InfoBadgeData::Count(12)});
        Check(tabs.TabBadge(L"inbox").kind == InfoBadgeData::Kind::Count &&
                  tabs.TabBadge(L"inbox").count == 12,
              "tab item badge retained");
        tabs.TabBadge(L"inbox", InfoBadgeData::Dot());
        Check(tabs.TabBadge(L"inbox").kind == InfoBadgeData::Kind::Dot, "tab set badge");
        TestNavigationView nav;
        nav.Items({{L"home", L"Home", icon::kLayers},
                   {L"files", L"Files", icon::kFolder}});
        nav.ItemBadge(L"files", InfoBadgeData::Count(3));
        Check(nav.ItemBadge(L"files").count == 3, "navigation set item badge");
        TestCommandBar bar;
        bar.Items({{L"run", L"Run", icon::kPlay}});
        bar.ItemBadge(L"run", InfoBadgeData::Dot());
        Check(bar.Items()[0].badge.kind == InfoBadgeData::Kind::Dot, "command bar item badge");
        IconView icon(icon::kBell);
        icon.Badge(InfoBadgeData::Count(8));
        Check(icon.Badge().count == 8, "icon view overlay badge");
    }
    {
        TestFileDropZone zone;
        const Size size = zone.Measure({400.0f, 200.0f}, theme);
        Check(size.w == 400.0f && size.h >= 72.0f, "file drop zone fills width");
        zone.Accept(L".png;.jpg");
        auto kept = zone.Filter({L"C:\\a.jpg", L"C:\\b.txt", L"C:\\c.PNG"});
        Check(kept.size() == 2 && kept[1] == L"C:\\c.PNG", "file drop filters extensions");
        zone.Multiple(false);
        kept = zone.Filter({L"C:\\a.png", L"C:\\b.png"});
        Check(kept.size() == 1 && kept[0] == L"C:\\a.png", "file drop single keeps first");
        int dropped = 0;
        zone.OnDrop([&](const std::vector<std::wstring>& paths) {
            dropped = static_cast<int>(paths.size());
        });
        zone.OnFileDrop({L"C:\\shot.png", L"C:\\notes.txt"});
        Check(dropped == 1 && zone.LastPaths().size() == 1 && !zone.Armed(),
              "file drop delivers filtered paths");
        zone.OnFileDrag(true);
        Check(zone.Armed(), "file drop arms on drag");
        zone.OnFileDrag(false);
        Check(!zone.Armed(), "file drop disarms on leave");
    }
    {
        TestRoot root;
        auto& field = root.Add<TestFormField>(L"Name");
        field.Required(true);
        auto& box = field.Add<TextBox>();
        box.Placeholder(L"Project");
        root.Measure({400.0f, 2000.0f}, theme);
        root.Arrange({0.0f, 0.0f, 400.0f, 200.0f});
        Check(field.Required() && field.Label() == L"Name", "form field required label");
        Check(box.AbsoluteBounds().y > field.AbsoluteBounds().y + 12.0f,
              "form field child sits below label");
        const Size idle = field.Measure({400.0f, 2000.0f}, theme);
        field.Error(L"Name is required");
        Check(field.HasError(), "form field reports error");
        const Size broken = field.Measure({400.0f, 2000.0f}, theme);
        Check(broken.h > idle.h, "form field error grows height");
        field.Error({});
        Check(!field.HasError() && field.Measure({400.0f, 2000.0f}, theme).h == idle.h,
              "form field clears error");
        field.Description(L"Shown on the build card.");
        Check(field.Measure({400.0f, 2000.0f}, theme).h > idle.h,
              "form field description grows height");
    }
    {
        TestRoot root;
        Button* ok = nullptr;
        root.Children(
            Column().Comfortable().Children(
                FormField(L"Name").Child(TextBox().Placeholder(L"Project")),
                Row().Children(Button(L"Cancel"), Button(L"OK", ButtonKind::Primary).Ref(ok))));
        Check(ok != nullptr && ok->Text() == L"OK", "children ref binds heap button");
        Check(root.ChildCount() == 1, "children added one column");
        root.Measure({400.0f, 2000.0f}, theme);
        root.Arrange({0.0f, 0.0f, 400.0f, 200.0f});
        Check(ok->DesiredSize().h == 44.0f, "children button medium height");
        Check(ok->AbsoluteBounds().y > 0.0f, "children nested row arranged");
    }
    {
        TestRoot root;
        Command save{L"Save", [] {}};
        bool dirty = false;
        save.CanExecute([&] { return dirty; });
        auto& btn = root.Add<Button>();
        btn.Bind(save);
        Check(!btn.Enabled() && btn.Text() == L"Save", "command canexecute disables button");
        dirty = true;
        save.RaiseCanExecuteChanged();
        Check(btn.Enabled(), "command raise enables button");
        int ran = 0;
        Command counted{L"Run", [&] { ++ran; }};
        counted.CanExecute([] { return false; });
        counted.Execute();
        Check(ran == 0, "command execute no-ops when disabled");
        Menu menu;
        menu.Add(save);
        Check(menu.Items().size() == 1 && menu.Items()[0].command == &save,
              "menu add command stores pointer");
    }
    {
        TestRoot root;
        auto& compact = root.Add<Column>();
        compact.Density(Density::Compact);
        auto& short_btn = compact.Add<Button>(L"Go");
        auto& normal = root.Add<Button>(L"Go");
        root.Measure({400.0f, 2000.0f}, theme);
        Check(short_btn.DesiredSize().h < normal.DesiredSize().h - 4.0f,
              "compact density shortens button");
        Check(normal.DesiredSize().h == 44.0f, "default density keeps medium height");
        const Theme compact_theme = short_btn.EffectiveTheme(theme);
        Check(compact_theme.button_height < theme.button_height, "effective theme scales button_height");
    }
    {
        TestRoot root;
        auto& bar = root.Add<InfoBar>(L"Update available");
        bar.Message(L"Install the artifact.").Glyph(icon::kPackage);
        int clicks = 0;
        bar.Action(L"Install", [&] { ++clicks; });
        root.Measure({480.0f, 200.0f}, theme);
        root.Arrange({0.0f, 0.0f, 480.0f, 200.0f});
        Check(bar.ChildCount() == 1, "info bar hosts action");
        Check(bar.Glyph() == icon::kPackage, "info bar custom glyph");
        const Rect bar_r = bar.AbsoluteBounds();
        const Rect btn = bar.Child(0).AbsoluteBounds();
        Check(btn.x > bar_r.x + 40.0f, "info bar action right of glyph");
        Check(btn.Right() <= bar_r.Right() - 14.0f - 28.0f + 0.5f,
              "info bar action left of close");
        Check(btn.y >= bar_r.y && btn.Bottom() <= bar_r.Bottom() + 0.5f,
              "info bar action inside bar");
        bar.Action({}, nullptr);
        Check(!bar.Child(0).Visible(), "info bar empty action hides button");
        bar.Action(L"Retry", [&] { ++clicks; });
        Check(bar.Child(0).Visible(), "info bar action restore shows button");
        (void)clicks;
    }
    {
        TestStatusBar bar;
        bar.Path(L"C:\\src\\lumen").CountText(L"3 files").Zoom(L"100%");
        Check(bar.ItemCount() == 3 && bar.Path() == L"C:\\src\\lumen" && bar.Zoom() == L"100%",
              "status bar named slots");
        const Size size = bar.Measure({400.0f, 80.0f}, theme);
        Check(size.w == 400.0f && size.h == StatusBar::kHeight, "status bar fills width");
        bar.ItemText(L"zoom", L"150%");
        Check(bar.Zoom() == L"150%", "status bar set text by id");
        Check(bar.RemoveItem(L"count") && bar.ItemCount() == 2 && bar.CountText().empty(),
              "status bar removes tally");
        int invoked = 0;
        std::wstring last;
        bar.OnInvoked([&](std::wstring_view id) {
            ++invoked;
            last = id;
        });
        bar.Arrange({0.0f, 0.0f, 400.0f, StatusBar::kHeight});
        bar.OnMouseDown({20.0f, 14.0f}, 0x0001);
        Check(invoked == 1 && last == L"path", "status bar click path");
        bar.OnMouseDown({380.0f, 14.0f}, 0x0001);
        Check(invoked == 2 && last == L"zoom", "status bar click zoom");
    }
    {
        TestHotkeyBox box(L"ctrl+k");
        Check(box.Chord() == L"Ctrl+K" && box.Vk() == 'K' && box.HasCtrl() && !box.HasShift(),
              "hotkey parses ctrl+k");
        box.Chord(L"Ctrl+Shift+F12");
        Check(box.Chord() == L"Ctrl+Shift+F12" && box.Vk() == VK_F12 && box.HasShift(),
              "hotkey parses function key");
        int changed = 0;
        box.OnChanged([&] { ++changed; });
        box.OnKey('K');
        Check(box.Chord() == L"Ctrl+Shift+F12" && changed == 0, "hotkey ignores bare letter");
        box.OnKey(VK_F5);
        Check(box.Chord() == L"F5" && box.Vk() == VK_F5 && changed == 1, "hotkey captures F5");
        box.OnKey(VK_BACK);
        Check(box.Empty() && changed == 2, "hotkey backspace clears");
        box.Chord(L"Alt+Enter");
        Check(box.Chord() == L"Alt+Enter" && box.HasAlt(), "hotkey parses alt+enter");
        const Size size = box.Measure({220.0f, 80.0f}, theme);
        Check(size.w == 220.0f && size.h == theme.input_height, "hotkey measures like input");
        box.OnChar(L'x');
        Check(box.Chord() == L"Alt+Enter", "hotkey swallows char");
        box.Clear();
        Check(box.Empty() && changed == 3, "hotkey clear notifies");
    }
    {
        TestRoot root;
        auto& group = root.Add<TestGroupBox>(L"Network");
        auto& inside = group.Add<Label>(L"inside");
        root.Measure({400.0f, 2000.0f}, theme);
        root.Arrange({0.0f, 0.0f, 400.0f, 200.0f});
        Check(group.Title() == L"Network", "group box title");
        Check(inside.AbsoluteBounds().y > group.AbsoluteBounds().y + 10.0f,
              "group box child sits below title");
        Check(group.DesiredSize().h > inside.DesiredSize().h + 16.0f,
              "group box taller than child");
        const float with_title = group.DesiredSize().h;
        group.Title({});
        root.Measure({400.0f, 2000.0f}, theme);
        Check(group.DesiredSize().h < with_title, "group box untitled is shorter");
    }
    {
        TestViewbox box;
        auto& inner = box.Add<Panel>();
        inner.SetBounds({0.0f, 0.0f, 80.0f, 40.0f});
        const Size uniform = box.Measure({160.0f, 80.0f}, theme);
        Check(uniform.w == 160.0f && uniform.h == 80.0f, "viewbox uniform scales to fit");
        box.Arrange({0.0f, 0.0f, 160.0f, 80.0f});
        Check(inner.AbsoluteBounds().w == 80.0f && inner.AbsoluteBounds().h == 40.0f,
              "viewbox child keeps natural size");
        const Point mapped = box.MapToChildren({40.0f, 20.0f});
        Check(mapped.x == 20.0f && mapped.y == 10.0f, "viewbox maps pointer into layout space");
        inner.SetBounds({0.0f, 0.0f, 80.0f, 20.0f});
        box.Stretch(ViewboxStretch::Fill);
        const Size fill = box.Measure({160.0f, 80.0f}, theme);
        Check(fill.w == 160.0f && fill.h == 80.0f, "viewbox fill eats available");
        box.Arrange({0.0f, 0.0f, 160.0f, 80.0f});
        const Point fill_mapped = box.MapToChildren({80.0f, 40.0f});
        Check(fill_mapped.x == 40.0f && fill_mapped.y == 10.0f, "viewbox fill maps non-uniform");
        inner.SetBounds({0.0f, 0.0f, 80.0f, 40.0f});
        box.Stretch(ViewboxStretch::None);
        const Size none = box.Measure({160.0f, 80.0f}, theme);
        Check(none.w == 80.0f && none.h == 40.0f, "viewbox none keeps natural");
        box.Arrange({0.0f, 0.0f, 160.0f, 80.0f});
        Check(inner.AbsoluteBounds().x == 40.0f && inner.AbsoluteBounds().y == 20.0f,
              "viewbox none centers natural child");
    }
    {
        OffscreenRenderer renderer;
        if (!renderer.Init(200, 140)) {
            Check(false, "viewbox fill clip renderer");
        } else {
            TestViewbox box;
            box.Stretch(ViewboxStretch::Fill);
            auto& inner = box.Add<Panel>();
            inner.Background(Color{1.0f, 1.0f, 1.0f, 1.0f});
            inner.SetBounds({0.0f, 0.0f, 40.0f, 80.0f});
            box.Measure({120.0f, 80.0f}, theme);
            box.Arrange({16.0f, 16.0f, 120.0f, 80.0f});
            ID2D1DeviceContext2* dc = renderer.BeginDraw();
            Painter painter;
            painter.BeginFrame(dc, &UiText(), 1.0f);
            painter.FillRect({0.0f, 0.0f, 200.0f, 140.0f}, theme.bg);
            DrawControlTree(painter, theme, &box, box.AbsoluteBounds());
            painter.EndFrame();
            Check(renderer.EndDraw(), "viewbox fill clip enddraw");
            Color inside{};
            Color outside{};
            Check(renderer.ReadPixel(76, 56, inside) && inside.r > 0.85f,
                  "viewbox fill paints stretched child");
            Check(renderer.ReadPixel(140, 56, outside) && CloseTo(outside, theme.bg),
                  "viewbox fill clips non-uniform overflow");
            renderer.Shutdown();
        }
    }
    {
        OffscreenRenderer renderer;
        if (!renderer.Init(200, 120)) {
            Check(false, "viewbox uniformtofill clip renderer");
        } else {
            TestViewbox box;
            box.Stretch(ViewboxStretch::UniformToFill);
            auto& inner = box.Add<Panel>();
            inner.Background(Color{1.0f, 1.0f, 1.0f, 1.0f});
            inner.SetBounds({0.0f, 0.0f, 40.0f, 80.0f});
            box.Measure({120.0f, 40.0f}, theme);
            box.Arrange({16.0f, 16.0f, 120.0f, 40.0f});
            ID2D1DeviceContext2* dc = renderer.BeginDraw();
            Painter painter;
            painter.BeginFrame(dc, &UiText(), 1.0f);
            painter.FillRect({0.0f, 0.0f, 200.0f, 120.0f}, theme.bg);
            DrawControlTree(painter, theme, &box, box.AbsoluteBounds());
            painter.EndFrame();
            Check(renderer.EndDraw(), "viewbox uniformtofill clip enddraw");
            Color inside{};
            Color outside{};
            Check(renderer.ReadPixel(76, 36, inside) && inside.r > 0.85f,
                  "viewbox uniformtofill paints cropped child");
            Check(renderer.ReadPixel(76, 64, outside) && CloseTo(outside, theme.bg),
                  "viewbox uniformtofill clips overflow");
            renderer.Shutdown();
        }
    }
    {
        // Dialog：额外子级排在换行正文之下、页脚按钮之上，不得与说明文字重叠。
        TestDialog dialog;
        const std::wstring message =
            L"A new luminescent primitive on the void. Spotlight, glow tokens "
            L"and monochrome contrast stay in lockstep.\n\nName this component:";
        dialog.Title(L"Add Component")
            .Message(message)
            .SecondaryButton(L"Cancel", [] {})
            .PrimaryButton(L"Add", [] {});
        auto& box = dialog.Add<TextBox>();
        box.Placeholder(L"Component name");
        const Size size = dialog.Measure({600.0f, 800.0f}, theme);
        dialog.Arrange({0.0f, 0.0f, size.w, size.h});
        const float inner = size.w - 48.0f;
        const float msg_h = MeasureWrappedHeight(message, TextRole::Body, inner);
        const float msg_bottom = 20.0f + 28.0f + 8.0f + msg_h;
        Check(box.AbsoluteBounds().y + 0.5f >= msg_bottom,
              "dialog extra child below message");
        Check(box.AbsoluteBounds().Bottom() + 0.5f <= size.h - 24.0f - 40.0f,
              "dialog extra child above footer");
        TestDialog compact;
        compact.Title(L"Delete").Message(L"Sure?").CardSize(DialogSize::Compact)
            .SecondaryButton(L"Cancel")
            .PrimaryButton(L"Delete");
        const Size compact_size = compact.Measure({800.0f, 800.0f}, theme);
        Check(std::fabs(compact_size.w - 320.0f) < 0.5f, "dialog compact width");
        TestDialog wide;
        wide.Title(L"Export").CardSize(DialogSize::Wide).PrimaryButton(L"Save");
        const Size wide_size = wide.Measure({800.0f, 800.0f}, theme);
        Check(std::fabs(wide_size.w - 560.0f) < 0.5f, "dialog wide width");
        int got = -1;
        TestDialog keyed;
        keyed.Title(L"x")
            .PrimaryButton(L"OK")
            .SecondaryButton(L"No")
            .DefaultButton(DialogCommand::Primary)
            .CancelButton(DialogCommand::Secondary)
            .OnResult([&](DialogResult r) { got = static_cast<int>(r); });
        Check(keyed.OnKey(VK_RETURN), "dialog enter default");
        Check(got == static_cast<int>(DialogResult::Primary), "dialog result primary");
        DialogResult esc_got = DialogResult::None;
        TestDialog esc;
        esc.Title(L"x")
            .CloseButton(L"Not now")
            .PrimaryButton(L"Save")
            .CancelButton(DialogCommand::Close)
            .OnResult([&](DialogResult r) { esc_got = r; });
        Check(esc.OnKey(VK_ESCAPE), "dialog esc cancel");
        Check(esc_got == DialogResult::Close, "dialog esc close command");
    }
    {
        Window window(L"overlay-lifetime", {240.0f, 160.0f});
        {
            Dialog dialog;
            dialog.Title(L"x").Message(L"y").DefaultClose();
            window.ShowDialog(dialog);
            Check(window.DialogActive(), "dialog shown");
        }
        Check(!window.DialogActive(), "dialog dtor unregisters");
        {
            Flyout flyout;
            flyout.Add<Label>(L"hi");
            window.ShowFlyout(flyout, nullptr);
            Check(window.FlyoutActive(), "flyout shown");
        }
        Check(!window.FlyoutActive(), "flyout dtor unregisters");
        {
            TeachingTip tip;
            tip.Title(L"Tip").Message(L"Body");
            window.ShowTeachingTip(tip, nullptr);
            Check(window.FlyoutActive(), "teaching tip shown");
        }
        Check(!window.FlyoutActive(), "teaching tip dtor unregisters");
    }
    {
        Check(clipboard::Text(L"lumen-clip") && clipboard::Text() == L"lumen-clip",
              "clipboard roundtrip");
        Button named(L"Save");
        named.AccessibleName(L"save document");
        Check(named.AccessibleName() == L"save document", "accessible name stored");
    }
    {
        TestLog log;
        log.Follow(true);
        const Size sz = log.Measure({240.0f, 80.0f}, theme);
        log.Arrange({0.0f, 0.0f, sz.w, sz.h});
        log.ItemCount(200);
        Check(log.Following(), "logview follows tail");
        log.OnWheel(4.0f);
        Check(!log.Following(), "logview pauses follow on scroll up");
    }
    {
        TestRich rich;
        rich.Add(L"used ").Strong(L"85%").Secondary(L" of space");
        const Size sz = rich.Measure({200.0f, 400.0f}, theme);
        Check(sz.h >= 16.0f && sz.w == 200.0f, "richlabel measures wrap width");
    }
    {
        Window window(L"batch2-overlay", {240.0f, 160.0f});
        Check(window.IsUiThread(), "window ui thread");
        int posted = 0;
        window.Post([&posted] { posted = 1; });
        MSG msg{};
        while (PeekMessageW(&msg, static_cast<HWND>(window.NativeHandle()), 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Check(posted == 1, "window post drains on ui thread");
        window.ShowBusy(L"wait");
        Check(window.BusyActive(), "busy shown");
        window.CloseBusy();
        Check(!window.BusyActive(), "busy closed");
        {
            Drawer drawer;
            window.ShowDrawer(drawer, Edge::Right);
            Check(window.DrawerActive(), "drawer shown");
        }
        Check(!window.DrawerActive(), "drawer dtor unregisters");
    }
    {
        OffscreenRenderer renderer;
        if (!renderer.Init(220, 80)) {
            Check(false, "sparkline renderer");
        } else {
            TestSparkline spark;
            spark.Count(8).Values([](size_t i) { return static_cast<float>(i); });
            spark.Measure({200.0f, 36.0f}, theme);
            spark.Arrange({8.0f, 16.0f, 200.0f, 36.0f});
            ID2D1DeviceContext2* dc = renderer.BeginDraw();
            Painter painter;
            painter.BeginFrame(dc, &UiText(), 1.0f);
            painter.FillRect({0.0f, 0.0f, 220.0f, 80.0f}, theme.bg);
            DrawControlTree(painter, theme, &spark);
            painter.EndFrame();
            Check(renderer.EndDraw(), "sparkline enddraw");
            Color ink{};
            int hits = 0;
            for (int x = 16; x < 200; ++x) {
                for (int y = 18; y < 50; ++y) {
                    renderer.ReadPixel(x, y, ink);
                    if (ink.r > 0.12f) ++hits;
                }
            }
            Check(hits > 8, "sparkline paints ink");
            renderer.Shutdown();
        }
    }
    {
        OffscreenRenderer renderer;
        if (!renderer.Init(140, 120)) {
            Check(false, "gauge renderer");
        } else {
            TestGauge gauge;
            gauge.Range(0.0f, 100.0f).Value(80.0f).Threshold(60.0f);
            gauge.Measure({120.0f, 110.0f}, theme);
            gauge.Arrange({10.0f, 4.0f, 120.0f, 110.0f});
            ID2D1DeviceContext2* dc = renderer.BeginDraw();
            Painter painter;
            painter.BeginFrame(dc, &UiText(), 1.0f);
            painter.FillRect({0.0f, 0.0f, 140.0f, 120.0f}, theme.bg);
            DrawControlTree(painter, theme, &gauge);
            painter.EndFrame();
            Check(renderer.EndDraw(), "gauge enddraw");
            Color ink{};
            int hits = 0;
            for (int y = 20; y < 100; ++y) {
                for (int x = 20; x < 120; ++x) {
                    renderer.ReadPixel(x, y, ink);
                    if (ink.r > 0.12f) ++hits;
                }
            }
            Check(hits > 20, "gauge paints arc ink");
            renderer.Shutdown();
        }
    }
    {
        OffscreenRenderer renderer;
        if (!renderer.Init(240, 180)) {
            Check(false, "chart area renderer");
        } else {
            TestChart chart;
            chart.Kind(ChartKind::Area)
                .Values({4.f, 8.f, 6.f, 12.f, 10.f, 16.f, 14.f, 18.f})
                .PreferredSize({220.0f, 140.0f});
            chart.Measure({220.0f, 140.0f}, theme);
            chart.Arrange({8.0f, 16.0f, 220.0f, 140.0f});
            ID2D1DeviceContext2* dc = renderer.BeginDraw();
            Painter painter;
            painter.BeginFrame(dc, &UiText(), 1.0f);
            painter.FillRect({0.0f, 0.0f, 240.0f, 180.0f}, theme.bg);
            DrawControlTree(painter, theme, &chart);
            painter.EndFrame();
            Check(renderer.EndDraw(), "chart area enddraw");
            Color ink{};
            int hits = 0;
            for (int x = 20; x < 210; ++x) {
                for (int y = 40; y < 150; ++y) {
                    renderer.ReadPixel(x, y, ink);
                    if (ink.r > theme.bg.r + 0.04f) ++hits;
                }
            }
            Check(hits > 40, "chart area fill brighter than backdrop");
            renderer.Shutdown();
        }
    }
    {
        OffscreenRenderer renderer;
        if (!renderer.Init(240, 180)) {
            Check(false, "chart donut renderer");
        } else {
            TestChart chart;
            chart.Kind(ChartKind::Donut)
                .Slices({{L"A", 0.5f}, {L"B", 0.3f}, {L"C", 0.2f}})
                .PreferredSize({220.0f, 160.0f});
            chart.Measure({220.0f, 160.0f}, theme);
            chart.Arrange({8.0f, 8.0f, 220.0f, 160.0f});
            ID2D1DeviceContext2* dc = renderer.BeginDraw();
            Painter painter;
            painter.BeginFrame(dc, &UiText(), 1.0f);
            painter.FillRect({0.0f, 0.0f, 240.0f, 180.0f}, theme.bg);
            DrawControlTree(painter, theme, &chart);
            painter.EndFrame();
            Check(renderer.EndDraw(), "chart donut enddraw");
            Color ink{};
            int hits = 0;
            for (int x = 20; x < 140; ++x) {
                for (int y = 20; y < 160; ++y) {
                    renderer.ReadPixel(x, y, ink);
                    if (ink.r > 0.12f) ++hits;
                }
            }
            Check(hits > 30, "chart donut paints ring");
            renderer.Shutdown();
        }
    }
    {
        OffscreenRenderer renderer;
        if (!renderer.Init(200, 80)) {
            Check(false, "chart heatmap renderer");
        } else {
            TestChart chart;
            chart.Kind(ChartKind::Heatmap).Grid(8, 4).Cell([](size_t x, size_t y) {
                return (x == 0 && y == 0) ? 1.0f : 0.0f;
            });
            chart.PreferredSize({180.0f, 64.0f});
            chart.Measure({180.0f, 64.0f}, theme);
            chart.Arrange({8.0f, 8.0f, 180.0f, 64.0f});
            ID2D1DeviceContext2* dc = renderer.BeginDraw();
            Painter painter;
            painter.BeginFrame(dc, &UiText(), 1.0f);
            painter.FillRect({0.0f, 0.0f, 200.0f, 80.0f}, theme.bg);
            DrawControlTree(painter, theme, &chart);
            painter.EndFrame();
            Check(renderer.EndDraw(), "chart heatmap enddraw");
            Color hi{}, lo{};
            renderer.ReadPixel(24, 22, hi);
            renderer.ReadPixel(52, 22, lo);
            Check(hi.r > lo.r + 0.05f, "chart heatmap high cell brighter than low");
            renderer.Shutdown();
        }
    }
    {
        OffscreenRenderer renderer;
        if (!renderer.Init(220, 80)) {
            Check(false, "chart sample-cap renderer");
        } else {
            TestChart chart;
            chart.Kind(ChartKind::Line).Count(10000).Values([](size_t i) {
                return static_cast<float>(i % 17);
            });
            chart.Measure({200.0f, 60.0f}, theme);
            chart.Arrange({8.0f, 8.0f, 200.0f, 60.0f});
            ID2D1DeviceContext2* dc = renderer.BeginDraw();
            Painter painter;
            painter.BeginFrame(dc, &UiText(), 1.0f);
            painter.FillRect({0.0f, 0.0f, 220.0f, 80.0f}, theme.bg);
            DrawControlTree(painter, theme, &chart);
            painter.EndFrame();
            Check(renderer.EndDraw(), "chart 10000-point sample cap");
            renderer.Shutdown();
        }
    }
    {
        TestChart chart;
        chart.Values({1.f, 2.f, 3.f});
        Check(std::fabs(chart.DisplayValue(0) - 1.f) < 0.01f, "chart first values snap");
        chart.Values({10.f, 20.f, 30.f});
        Check(chart.DisplayValue(0) < 4.f, "chart tween starts at previous");
        chart.OnAnimate(0.08f);
        Check(chart.DisplayValue(0) > 1.f && chart.DisplayValue(0) < 10.f, "chart tween mid");
        chart.OnAnimate(1.0f);
        Check(std::fabs(chart.DisplayValue(0) - 10.f) < 0.05f, "chart tween settles");
    }
    {
        TestChart chart;
        chart.Kind(ChartKind::Line)
            .Header(L"T", L"1")
            .Values({20.f, 32.f, 38.f, 45.f})
            .Baseline({18.f, 24.f, 29.f, 40.f})
            .SeriesName(L"Active")
            .BaselineName(L"Baseline");
        chart.Measure({400.0f, 200.0f}, theme);
        chart.Arrange({0.0f, 0.0f, 400.0f, 200.0f});
        Check(chart.LegendCount() == 2, "chart line legend has two series");
        Check(chart.SeriesVisible(1), "chart baseline visible by default");
        const Rect lb = chart.LegendBounds(1);
        Check(lb.w > 8.0f && lb.h > 8.0f, "chart legend bounds");
        chart.OnMouseDown({lb.x + 4.0f, lb.y + 4.0f}, kBtnL);
        Check(!chart.SeriesVisible(1), "chart legend click hides baseline");
        chart.OnMouseDown({lb.x + 4.0f, lb.y + 4.0f}, kBtnL);
        Check(chart.SeriesVisible(1), "chart legend click shows baseline");
    }
    {
        TestChart chart;
        chart.Kind(ChartKind::Line).Values({1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 9.f, 10.f, 11.f, 12.f});
        chart.Measure({240.0f, 120.0f}, theme);
        chart.Arrange({0.0f, 0.0f, 240.0f, 120.0f});
        Check(chart.ViewStart() == 0.0f && chart.ViewEnd() == 1.0f, "chart view default");
        Check(chart.OnWheel(1.0f), "chart wheel zooms");
        Check(chart.ViewEnd() - chart.ViewStart() < 0.99f, "chart wheel shrinks window");
        chart.OnMouseDoubleClick({120.0f, 60.0f});
        Check(chart.ViewStart() == 0.0f && chart.ViewEnd() == 1.0f, "chart double-click resets view");
        Check(chart.OnWheel(1.0f), "chart wheel again");
        chart.ResetView();
        Check(chart.ViewStart() == 0.0f && chart.ViewEnd() == 1.0f, "chart ResetView");
    }
    {
        TestChart chart;
        chart.Kind(ChartKind::Bar).Values({1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f});
        chart.Measure({240.0f, 120.0f}, theme);
        chart.Arrange({0.0f, 0.0f, 240.0f, 120.0f});
        Check(chart.OnWheel(1.0f), "bar wheel zooms");
        const float a = chart.ViewStart() * 7.0f;
        const float b = chart.ViewEnd() * 7.0f;
        Check(std::fabs(a - std::floor(a + 0.5f)) < 0.02f, "bar view start snaps to category");
        Check(std::fabs(b - std::floor(b + 0.5f)) < 0.02f, "bar view end snaps to category");
        Check(b - a >= 1.5f, "bar view keeps at least two categories");
    }
    {
        const float data[] = {0.f, 1.f, 0.4f, 1.6f, 0.2f, 1.8f, 0.5f, 2.f};
        TestSparkline spark;
        spark.Values(std::span<const float>{data, 8});
        Check(spark.Count() == 8, "sparkline Values(span) stores count");
        spark.Measure({160.0f, 36.0f}, theme);
        spark.Arrange({0.0f, 0.0f, 160.0f, 36.0f});
    }
    {
        OffscreenRenderer renderer;
        if (!renderer.Init(220, 90)) {
            Check(false, "table progress renderer");
        } else {
            TestRoot host;
            auto& table = host.Add<Table>();
            table.AddColumn(L"Load", 160.0f).Progress([](size_t) { return 0.8f; });
            table.RowCount(1);
            host.Measure({200.0f, 80.0f}, theme);
            host.Arrange({8.0f, 8.0f, 200.0f, 72.0f});
            ID2D1DeviceContext2* dc = renderer.BeginDraw();
            Painter painter;
            painter.BeginFrame(dc, &UiText(), 1.0f);
            painter.FillRect({0.0f, 0.0f, 220.0f, 90.0f}, theme.bg);
            DrawControlTree(painter, theme, &host);
            painter.EndFrame();
            Check(renderer.EndDraw(), "table progress enddraw");
            Color ink{};
            const Rect body = table.AbsoluteBounds();
            int hits = 0;
            for (int y = static_cast<int>(body.y) + 34; y < static_cast<int>(body.Bottom()) - 2; ++y) {
                for (int x = static_cast<int>(body.x) + 16; x < static_cast<int>(body.x) + 140; ++x) {
                    renderer.ReadPixel(x, y, ink);
                    if (ink.r > 0.2f) ++hits;
                }
            }
            Check(hits > 8, "table progress bar ink");
            renderer.Shutdown();
        }
    }
}

void RenderListScene(const wchar_t* path) {
    OffscreenRenderer renderer;
    if (!renderer.Init(700, 500)) {
        Check(false, "renderer init (list scene)");
        return;
    }
    const Theme theme = MakeTheme();
    TestRoot root;
    root.Padding(8.0f, 8.0f).Spacing(8.0f);
    auto& list = root.Add<TestList>();
    list.ItemCount(8);
    list.ItemText([](size_t i, std::wstring& s) { s = L"行 " + std::to_wstring(i); });
    list.MultiSelect(true);
    list.SelectedIndices({0, 2});

    auto& table = root.Add<Table>();
    table.AddColumn(L"A", 120.0f);
    table.AddColumn(L"B");
    static const std::vector<int> kSceneValues{9, 5, 7, 1, 3};
    table.CellText([](size_t row, size_t col, std::wstring& out) {
        if (col == 0) out = std::to_wstring(kSceneValues[row]);
        else out = L"b" + std::to_wstring(row);
    });
    table.Sortable(0, true);
    table.RowCount(5);
    table.SelectedIndex(1);   // 数据 1（"5"），升序后应落到视图 2
    table.SortBy(0, 1);
    Check(table.SelectedIndex() == 2, "scene table selection follows sort");

    root.Measure({700.0f, 500.0f}, theme);
    root.Arrange({0.0f, 0.0f, 700.0f, 500.0f});

    ID2D1DeviceContext2* dc = renderer.BeginDraw();
    Painter painter;
    painter.BeginFrame(dc, &UiText(), 1.0f);
    painter.FillRect({0, 0, 700, 500}, theme.bg);
    DrawControlTree(painter, theme, &root);
    painter.EndFrame();
    Check(renderer.EndDraw(), "enddraw (list scene)");

    const float rh = theme.list_row_height;
    Color c{};
    const Rect lb = list.AbsoluteBounds();
    const Color plain_row = Over(theme.fill_input, theme.bg);
    const Color picked_row = Over(theme.fill_selected, Over(theme.fill_input, theme.bg));
    renderer.ReadPixel(static_cast<int>(lb.x + lb.w * 0.5f), static_cast<int>(lb.y + 0.5f * rh), c);
    Check(CloseTo(c, picked_row), "multi row0 selected fill");
    renderer.ReadPixel(static_cast<int>(lb.x + lb.w * 0.5f), static_cast<int>(lb.y + 1.5f * rh), c);
    Check(CloseTo(c, plain_row), "multi row1 unselected fill");
    renderer.ReadPixel(static_cast<int>(lb.x + lb.w * 0.5f), static_cast<int>(lb.y + 2.5f * rh), c);
    Check(CloseTo(c, picked_row), "multi row2 selected fill");

    const Rect tb = table.AbsoluteBounds();
    renderer.ReadPixel(static_cast<int>(tb.x + tb.w * 0.5f),
                       static_cast<int>(tb.y + 32.0f + 2.5f * rh), c);
    Check(CloseTo(c, picked_row), "table sorted view selected fill");

    Check(renderer.SavePNG(path), "save png (list scene)");
    renderer.Shutdown();
}

void TestSignal() {
    int n = 0;
    Signal<> sig;
    Connection a = sig.Connect([&] { ++n; });
    Connection b = sig.Connect([&] { n += 10; });
    sig.Emit();
    Check(n == 11, "signal two subscribers");
    a.Disconnect();
    sig.Emit();
    Check(n == 21, "signal disconnect");
    b.Disconnect();
    n = 0;
    {
        ScopedConnection scoped(sig.Connect([&] { n += 100; }));
        sig.Emit();
        Check(n == 100, "scoped connection fires");
    }
    sig.Emit();
    Check(n == 100, "scoped disconnects on dtor");

    TestButton btn;
    int clicks = 0;
    btn.OnClick([&] { ++clicks; });
    btn.OnClick([&] { clicks += 2; });
    Connection extra = btn.BindClick([&] { clicks += 4; });
    btn.OnMouseUp({0.0f, 0.0f}, 1);
    Check(clicks == 7, "button multicasts click");
    extra.Disconnect();
    btn.OnMouseUp({0.0f, 0.0f}, 1);
    Check(clicks == 10, "button bindclick disconnect");

    Property<int> count{1};
    int seen = -1;
    Connection watch = count.OnChanged([&](const int& v) { seen = v; });
    count = 3;
    Check(seen == 3 && count.Get() == 3, "property notifies");
    count = 3;
    Check(seen == 3, "property skips equal");
    watch.Disconnect();
    count = 9;
    Check(seen == 3, "property disconnect");
}

void TestLayout() {
    const Theme theme = MakeTheme();
    using Cross = StackPanel::CrossAlign;
    using Main = StackPanel::MainAlign;

    {
        TestRoot root;
        auto& row = root.Add<Row>().Spacing(4.0f);
        row.Add<Panel>().SetBounds({0.0f, 0.0f, 10.0f, 8.0f});
        auto& b = row.Add<Panel>().SetBounds({0.0f, 0.0f, 20.0f, 10.0f});
        const Size s = root.Measure({400.0f, 400.0f}, theme);
        Check(Near(s.w, 34.0f) && Near(s.h, 10.0f), "row pack size");
        root.Arrange({0.0f, 0.0f, s.w, s.h});
        Check(Near(b.Bounds().x, 14.0f), "row second x");
    }
    {
        TestRoot root;
        auto& row = root.Add<Row>();
        auto& a = row.Add<Panel>().SetBounds({0.0f, 0.0f, 10.0f, 10.0f});
        a.Grow();
        auto& b = row.Add<Panel>().SetBounds({0.0f, 0.0f, 10.0f, 10.0f});
        b.Grow();
        root.Measure({200.0f, 50.0f}, theme);
        root.Arrange({0.0f, 0.0f, 200.0f, 50.0f});
        Check(Near(a.Bounds().w, 100.0f) && Near(b.Bounds().w, 100.0f), "grow equal columns");
        Check(Near(b.Bounds().x, 100.0f), "grow second x");
    }
    {
        TestRoot root;
        auto& row = root.Add<Row>().Spacing(4.0f).AlignMain(Main::End);
        row.Add<Panel>().SetBounds({0.0f, 0.0f, 10.0f, 10.0f});
        auto& b = row.Add<Panel>().SetBounds({0.0f, 0.0f, 20.0f, 10.0f});
        root.Measure({200.0f, 50.0f}, theme);
        root.Arrange({0.0f, 0.0f, 200.0f, 50.0f});
        Check(Near(b.Bounds().x, 180.0f), "align main end");
    }
    {
        TestRoot root;
        auto& row = root.Add<Row>().AlignMain(Main::SpaceBetween);
        auto& a = row.Add<Panel>().SetBounds({0.0f, 0.0f, 10.0f, 10.0f});
        auto& b = row.Add<Panel>().SetBounds({0.0f, 0.0f, 10.0f, 10.0f});
        auto& c = row.Add<Panel>().SetBounds({0.0f, 0.0f, 10.0f, 10.0f});
        root.Measure({100.0f, 50.0f}, theme);
        root.Arrange({0.0f, 0.0f, 100.0f, 50.0f});
        Check(Near(a.Bounds().x, 0.0f) && Near(b.Bounds().x, 45.0f) && Near(c.Bounds().x, 90.0f),
              "align space-between");
    }
    {
        TestRoot root;
        root.AlignCross(Cross::End);
        auto& box = root.Add<Panel>().SetBounds({0.0f, 0.0f, 20.0f, 10.0f});
        root.Measure({100.0f, 50.0f}, theme);
        root.Arrange({0.0f, 0.0f, 100.0f, 50.0f});
        Check(Near(box.Bounds().x, 80.0f), "align cross end");
    }
    {
        TestRoot root;
        auto& grid = root.Add<Grid>(2).Gap(10.0f);
        auto& a = grid.Add<Panel>().SetBounds({0.0f, 0.0f, 10.0f, 20.0f});
        auto& b = grid.Add<Panel>().SetBounds({0.0f, 0.0f, 10.0f, 30.0f});
        root.Measure({210.0f, 100.0f}, theme);
        root.Arrange({0.0f, 0.0f, 210.0f, 50.0f});
        Check(Near(a.Bounds().w, 100.0f) && Near(b.Bounds().x, 110.0f), "grid equal columns");
        Check(Near(a.Bounds().h, 30.0f) && Near(b.Bounds().h, 30.0f), "grid stretch row height");
    }
    {
        TestRoot root;
        auto& grid = root.Add<Grid>(1, 0, 1);
        auto& lead = grid.Add<Panel>().SetBounds({0.0f, 0.0f, 40.0f, 10.0f});
        auto& mid = grid.Add<Panel>().SetBounds({0.0f, 0.0f, 80.0f, 10.0f});
        auto& trail = grid.Add<Panel>().SetBounds({0.0f, 0.0f, 20.0f, 10.0f});
        root.Measure({300.0f, 50.0f}, theme);
        root.Arrange({0.0f, 0.0f, 300.0f, 50.0f});
        Check(Near(lead.Bounds().w, 110.0f) && Near(mid.Bounds().x, 110.0f) && Near(mid.Bounds().w, 80.0f),
              "grid 1fr auto 1fr mid");
        Check(Near(trail.Bounds().x, 190.0f) && Near(trail.Bounds().w, 110.0f), "grid 1fr auto 1fr trail");
    }
    {
        TestRoot root;
        auto& row = root.Add<Row>();
        auto& a = row.Add<Panel>().SetBounds({0.0f, 0.0f, 10.0f, 10.0f});
        a.Grow();
        auto& b = row.Add<Panel>().SetBounds({0.0f, 0.0f, 10.0f, 10.0f});
        b.Grow();
        root.Measure({200.0f, 50.0f}, theme);
        root.Arrange({0.0f, 0.0f, 200.0f, 50.0f});
        root.Measure({300.0f, 80.0f}, theme);
        root.Arrange({0.0f, 0.0f, 300.0f, 80.0f});
        Check(Near(a.Bounds().w, 150.0f) && Near(b.Bounds().w, 150.0f), "grow relayout wider");
        Check(a.AbsoluteBounds().w > 1.0f && a.AbsoluteBounds().h > 1.0f, "grow relayout absolute");
    }
    {
        TestRoot root;
        root.Padding(10.0f, 10.0f);
        auto& grid = root.Add<Grid>(2).Gap(10.0f);
        auto& left = grid.Add<Column>().Spacing(8.0f);
        auto& field = left.Add<Row>();
        auto& box = field.Add<Panel>().SetBounds({0.0f, 0.0f, 40.0f, 24.0f});
        box.Grow();
        root.Measure({400.0f, 300.0f}, theme);
        root.Arrange({0.0f, 40.0f, 400.0f, 260.0f});
        const float w1 = box.AbsoluteBounds().w;
        Check(w1 > 50.0f && box.AbsoluteBounds().h > 0.5f && box.AbsoluteBounds().y >= 40.0f,
              "nested grow first size");
        root.Measure({600.0f, 400.0f}, theme);
        root.Arrange({0.0f, 40.0f, 600.0f, 360.0f});
        Check(box.AbsoluteBounds().w > w1 && box.AbsoluteBounds().y >= 40.0f,
              "nested grow after window resize");
    }
    {
        TestRoot root;
        auto& sv = root.Add<TestScrollViewer>();
        sv.Grow();
        auto& inner = sv.Add<Panel>().SetBounds({0.0f, 0.0f, 40.0f, 240.0f});
        root.Measure({120.0f, 80.0f}, theme);
        root.Arrange({0.0f, 0.0f, 120.0f, 80.0f});
        Check(Near(sv.AbsoluteBounds().h, 80.0f), "scroll viewport height");
        Check(Near(inner.AbsoluteBounds().h, 240.0f), "scroll content height");
        Check(Near(inner.AbsoluteBounds().y, 0.0f), "scroll offset 0");
        sv.ScrollToY(40.0f);
        Check(Near(inner.AbsoluteBounds().y, -40.0f), "scroll offset 40");
        Check(sv.ContentHeight() > sv.AbsoluteBounds().h + 1.0f, "scroll overflow");
        sv.OnWheel(-1.0f);
        Check(Near(sv.OffsetY(), 88.0f), "scroll wheel steps 48");
    }
    {
        TestRoot root;
        auto& sv = root.Add<TestScrollViewer>();
        sv.Grow();
        auto& a = sv.Add<Panel>().SetBounds({0.0f, 0.0f, 40.0f, 80.0f});
        auto& b = sv.Add<Panel>().SetBounds({0.0f, 0.0f, 40.0f, 40.0f});
        auto& c = sv.Add<Panel>().SetBounds({0.0f, 0.0f, 40.0f, 80.0f});
        (void)a;
        (void)c;
        root.Measure({120.0f, 80.0f}, theme);
        root.Arrange({0.0f, 0.0f, 120.0f, 80.0f});
        sv.ScrollIntoView(b);
        Check(Near(sv.OffsetY(), 40.0f), "scroll into view nearest mid item");
        sv.ScrollToY(0.0f);
        sv.ScrollIntoView(b, ScrollAlignment::Center);
        Check(Near(sv.OffsetY(), 60.0f), "scroll into view centers item");
        sv.ScrollToY(0.0f);
        sv.ScrollIntoView(b, ScrollAlignment::Start);
        Check(Near(sv.OffsetY(), 80.0f), "scroll into view start");
    }
    {
        TestRoot root;
        auto& sv = root.Add<TestScrollViewer>();
        sv.Grow();
        auto& top = sv.Add<Panel>().SetBounds({0.0f, 0.0f, 40.0f, 80.0f});
        auto& mid = sv.Add<Panel>().SetBounds({0.0f, 0.0f, 40.0f, 80.0f});
        sv.Add<Panel>().SetBounds({0.0f, 0.0f, 40.0f, 80.0f});
        root.Measure({120.0f, 80.0f}, theme);
        root.Arrange({0.0f, 0.0f, 120.0f, 80.0f});
        sv.ScrollToY(80.0f);
        Check(Near(mid.AbsoluteBounds().y, 0.0f), "scroll mid item at viewport top");
        top.SetBounds({0.0f, 0.0f, 40.0f, 120.0f});
        root.Measure({120.0f, 80.0f}, theme);
        root.Arrange({0.0f, 0.0f, 120.0f, 80.0f});
        Check(Near(sv.OffsetY(), 120.0f) && Near(mid.AbsoluteBounds().y, 0.0f),
              "scroll anchor keeps viewport item when content above grows");
        sv.AnchorEnabled(false);
        sv.ScrollToY(80.0f, false);
        top.SetBounds({0.0f, 0.0f, 40.0f, 160.0f});
        root.Measure({120.0f, 80.0f}, theme);
        root.Arrange({0.0f, 0.0f, 120.0f, 80.0f});
        Check(Near(sv.OffsetY(), 80.0f), "scroll anchor off leaves offset");
    }
    {
        TestRoot root;
        auto& row = root.Add<Row>();
        auto& a = row.Add<Panel>().SetBounds({0.0f, 0.0f, 20.0f, 10.0f});
        a.Margin(8.0f);
        auto& b = row.Add<Panel>().SetBounds({0.0f, 0.0f, 20.0f, 10.0f});
        root.Measure({400.0f, 50.0f}, theme);
        root.Arrange({0.0f, 0.0f, 400.0f, 50.0f});
        Check(Near(a.DesiredSize().w, 36.0f), "margin inflates desired");
        Check(Near(a.AbsoluteBounds().x, 8.0f) && Near(b.AbsoluteBounds().x, 36.0f),
              "margin insets arrange");
    }
    {
        TestRoot root;
        auto& stack = root.Add<ZStack>();
        auto& back = stack.Add<Panel>().SetBounds({0.0f, 0.0f, 80.0f, 40.0f});
        auto& front = stack.Add<Panel>().SetBounds({0.0f, 0.0f, 20.0f, 10.0f});
        root.Measure({200.0f, 100.0f}, theme);
        root.Arrange({0.0f, 0.0f, 200.0f, 100.0f});
        Check(Near(stack.DesiredSize().w, 80.0f) && Near(stack.DesiredSize().h, 40.0f),
              "zstack sizes to largest child");
        Check(Near(front.AbsoluteBounds().x, back.AbsoluteBounds().x + 30.0f),
              "zstack centers overlay");
    }
    {
        TestRoot root;
        auto& box = root.Add<TestPanel>();
        box.SetBounds({0.0f, 0.0f, 10.0f, 10.0f});
        box.MinSize({40.0f, 24.0f});
        const Size s = box.Measure({100.0f, 100.0f}, theme);
        Check(Near(s.w, 10.0f) && Near(s.h, 10.0f), "minsize applied in parent measure");
        root.Measure({100.0f, 100.0f}, theme);
        Check(Near(box.DesiredSize().w, 40.0f) && Near(box.DesiredSize().h, 24.0f),
              "minsize via MeasureChildAt");
    }
    {
        TestRoot root;
        auto& row = root.Add<Row>().AlignCross(Cross::Start);
        auto& box = row.Add<Panel>().SetBounds({0.0f, 0.0f, 100.0f, 80.0f});
        box.MaxSize({40.0f, 24.0f});
        root.Measure({400.0f, 100.0f}, theme);
        root.Arrange({0.0f, 0.0f, 400.0f, 100.0f});
        Check(Near(box.DesiredSize().w, 40.0f) && Near(box.DesiredSize().h, 24.0f),
              "maxsize clamps desired");
        Check(Near(box.AbsoluteBounds().w, 40.0f) && Near(box.AbsoluteBounds().h, 24.0f),
              "maxsize clamps arrange");
    }
    {
        TestRoot root;
        root.Padding(10.0f, 5.0f);
        auto& box = root.Add<Panel>().SetBounds({0.0f, 0.0f, 20.0f, 10.0f});
        const Size s = root.Measure({400.0f, 400.0f}, theme);
        Check(Near(s.w, 40.0f) && Near(s.h, 20.0f), "column padding size");
        root.Arrange({0.0f, 0.0f, s.w, s.h});
        Check(Near(box.AbsoluteBounds().x, 10.0f) && Near(box.AbsoluteBounds().y, 5.0f),
              "column padding origin");
    }
    {
        TestRoot root;
        auto& wrap = root.Add<WrapPanel>().Gap(8.0f);
        auto& a = wrap.Add<Panel>().SetBounds({0.0f, 0.0f, 40.0f, 20.0f});
        auto& b = wrap.Add<Panel>().SetBounds({0.0f, 0.0f, 40.0f, 20.0f});
        auto& c = wrap.Add<Panel>().SetBounds({0.0f, 0.0f, 40.0f, 20.0f});
        const Size s = root.Measure({90.0f, 400.0f}, theme);
        Check(Near(s.w, 88.0f) && Near(s.h, 48.0f), "wrap panel folds third item");
        root.Arrange({0.0f, 0.0f, 90.0f, 100.0f});
        Check(Near(a.AbsoluteBounds().x, 0.0f) && Near(a.AbsoluteBounds().y, 0.0f),
              "wrap first origin");
        Check(Near(b.AbsoluteBounds().x, 48.0f) && Near(b.AbsoluteBounds().y, 0.0f),
              "wrap second same row");
        Check(Near(c.AbsoluteBounds().x, 0.0f) && Near(c.AbsoluteBounds().y, 28.0f),
              "wrap third next row");
    }
}

void TestTypography() {
    const Size overline = UiText().MeasureText(L"SECTION", TextRole::Overline);
    const Size caption = UiText().MeasureText(L"SECTION", TextRole::Caption);
    Check(overline.w > 8.0f && overline.h > 8.0f, "overline measures");
    Check(overline.h <= caption.h + 0.05f, "overline not taller than caption");
    const Size subtitle = UiText().MeasureText(L"Subtitle", TextRole::Subtitle);
    const Size body = UiText().MeasureText(L"Subtitle", TextRole::Body);
    Check(subtitle.h > body.h + 0.4f, "subtitle taller than body");
    const float n0 = UiText().MeasureText(L"0000", TextRole::Numeric).w;
    const float n1 = UiText().MeasureText(L"1111", TextRole::Numeric).w;
    Check(Near(n0, n1, 0.4f), "numeric tabular figures");
    Check(UiText().MeasureText(L"界面", TextRole::Body).w > 12.0f, "cjk fallback measures");
    const float tracked = UiText().MeasureText(L"WWWW", TextRole::Caption).w;
    const float untracked = UiText().MeasureText(L"WWWW", TextRole::Numeric).w;
    // Caption 12px + 0.06em 字距，Numeric 14px 无字距；字距使 Caption 不至于明显窄于更大的 Numeric。
    Check(tracked + 4.0f > untracked * (12.0f / 14.0f), "caption tracking widens");
}

void RenderScene(const wchar_t* path) {
    OffscreenRenderer renderer;
    if (!renderer.Init(1000, 760)) {
        Check(false, "renderer init");
        return;
    }
    const Theme theme = MakeTheme();
    Scene scene;
    scene.Build();
    scene.root.Measure({1000.0f, 760.0f}, theme);
    scene.root.Arrange({0.0f, 0.0f, 1000.0f, 760.0f});

    ID2D1DeviceContext2* dc = renderer.BeginDraw();
    Painter painter;
    painter.BeginFrame(dc, &UiText(), 1.0f);
    painter.FillRect({0, 0, 1000, 760}, theme.bg);
    DrawControlTree(painter, theme, &scene.root);
    painter.EndFrame();
    Check(renderer.EndDraw(), "enddraw");

    Color corner{};
    Check(renderer.ReadPixel(3, 3, corner) && CloseTo(corner, theme.bg), "bg pixel = void black");

    if (scene.primary) {
        const Rect b = scene.primary->AbsoluteBounds();
        Color c{};
        // 采左上内边（避开居中 CJK 字墨），圆角半径内仍是 danger 填充。
        renderer.ReadPixel(static_cast<int>(b.x + 12.0f), static_cast<int>(b.y + 10.0f), c);
        Check(CloseTo(c, theme.danger), "danger button center");
    }
    if (scene.checked_box) {
        const Rect b = scene.checked_box->AbsoluteBounds();
        const float box_y = b.y + (b.h - 20.0f) * 0.5f;
        Color c{};
        renderer.ReadPixel(static_cast<int>(b.x + 3.0f), static_cast<int>(box_y + 4.0f), c);
        Check(CloseTo(c, theme.accent), "checked box fill");
    }
    if (scene.on_switch) {
        const Rect b = scene.on_switch->AbsoluteBounds();
        Color c{};
        renderer.ReadPixel(static_cast<int>(b.x + 7.0f), static_cast<int>(b.y + b.h * 0.5f), c);
        Check(CloseTo(c, theme.accent), "switch on track");
    }
    if (scene.list) {
        const Rect b = scene.list->AbsoluteBounds();
        Color c{};
        renderer.ReadPixel(static_cast<int>(b.x + b.w - 40.0f), static_cast<int>(b.y + 42.0f), c);
        const Color expected = Over(theme.fill_selected, Over(theme.fill_input, theme.bg));
        Check(CloseTo(c, expected), "list selection composite");
    }
    if (scene.spot_card) {
        const Rect b = scene.spot_card->AbsoluteBounds();
        Color center{}, corner_px{};
        renderer.ReadPixel(static_cast<int>(b.x + b.w * 0.5f),
                           static_cast<int>(b.Bottom() - 5.0f), center);
        renderer.ReadPixel(static_cast<int>(b.x + 4.0f), static_cast<int>(b.y + 4.0f), corner_px);
        Check(center.r > corner_px.r + 0.02f && center.g > corner_px.g + 0.02f,
              "spotlight center brighter than corner");
        Color core{}, rim{};
        renderer.ReadPixel(static_cast<int>(b.x + b.w * 0.5f),
                           static_cast<int>(b.y + b.h * 0.5f), core);
        renderer.ReadPixel(static_cast<int>(b.x + 8.0f), static_cast<int>(b.y + b.h * 0.5f), rim);
        Check(core.r > rim.r + 0.015f, "spotlight hot core brighter than rim");
        float prev = 0.0f;
        float max_step = 0.0f;
        for (int i = 0; i < 48; ++i) {
            const float t = static_cast<float>(i) / 47.0f;
            Color sample{};
            renderer.ReadPixel(static_cast<int>(b.x + b.w * 0.5f),
                               static_cast<int>(b.y + 6.0f + t * (b.h - 12.0f)), sample);
            if (i > 0) max_step = std::max(max_step, std::fabs(sample.r - prev));
            prev = sample.r;
        }
        Check(max_step < 0.09f, "spotlight gradient no banding steps");
    }

    Check(renderer.SavePNG(path), "save png");
    renderer.Shutdown();
}

} // namespace

// 弹层/导航/轻量展示控件的像素板：骨架呼吸底色、星级填充差、头像圈+状态点、开关分割钮。
void RenderExtrasScene(const wchar_t* path) {
    OffscreenRenderer renderer;
    if (!renderer.Init(700, 1060)) {
        Check(false, "renderer init (extras)");
        return;
    }
    const Theme theme = MakeTheme();
    TestRoot root;
    root.Padding(16.0f, 16.0f).Spacing(18.0f);
    auto& row1 = root.Add<Row>().Spacing(28.0f).AlignCross(StackPanel::CrossAlign::Center);
    Skeleton* skeleton = &row1.Add<Skeleton>();
    skeleton->Lines(2);
    Avatar* avatar = &row1.Add<Avatar>();
    avatar->PresenceState(Avatar::Presence::Online);
    row1.Add<Avatar>(L"林").Diameter(40.0f).PresenceState(Avatar::Presence::Away);
    Rating* rating = &row1.Add<Rating>();
    rating->Value(2.0);

    auto& row2 = root.Add<Row>().Spacing(28.0f).AlignCross(StackPanel::CrossAlign::Center);
    NumberBox* number = &row2.Add<NumberBox>();
    number->Range(0.0, 100.0).Value(42.0);
    auto& crumb = row2.Add<Breadcrumb>();
    crumb.AddItem(L"库").AddItem(L"项目").AddItem(L"设置");
    crumb.SelectedIndex(0);
    SplitButton* toggle = &row2.Add<SplitButton>(L"自动部署").Toggle(true).Checked(true);

    // 图标字形核对行（kSparkle 码点有效性人工核对 PNG）。
    auto& row3 = root.Add<Row>().Spacing(16.0f);
    for (const wchar_t* glyph :
         {icon::kSparkle, icon::kCalendar, icon::kClock, icon::kPlay, icon::kPause}) {
        row3.Add<IconView>(glyph).Box(24.0f).IconSize(16.0f);
    }
    InfoBadge* info_dot = &row3.Add<InfoBadge>();
    info_dot->Dot();
    row3.Add<InfoBadge>(3);
    row3.Add<IconView>(icon::kBell).Box(24.0f).IconSize(16.0f).Badge(InfoBadgeData::Dot());

    // 步骤条：当前步为空心圆，核对连接线不伸入圆内。
    auto& row4 = root.Add<Row>();
    Stepper* steps = &row4.Add<Stepper>();
    steps->AddStep(L"配置").AddStep(L"构建").AddStep(L"发布").AddStep(L"验证");
    steps->Current(2);

    // 分页器：左垫 60 与原点拉开距离，核对墨迹只落在自身 AbsoluteBounds 内。
    auto& row5 = root.Add<Row>().Padding(60.0f, 0.0f);
    Pagination* pager = &row5.Add<Pagination>();
    pager->PageCount(12).Current(1);

    // 菜单栏：标题行盒须在 bar 内垂直居中（DrawText 顶对齐易贴顶）。
    auto& row6 = root.Add<Row>();
    MenuBar* bar = &row6.Add<MenuBar>();
    bar->AddMenu(L"File", Menu{}).AddMenu(L"Edit", Menu{});

    // 聚光卡内的虚拟化表格：缓冲行子控的避光垫不得露出表体（悬停剪影回归）。
    static std::vector<std::wstring> g_ghost_notes(24);
    auto& ghost_card = root.Add<TestSpotlightCard>();
    ghost_card.Card(Panel::CardStyle::Lumen, 16.0f);
    auto& ghost_table = ghost_card.Add<Table>();
    ghost_table.RowHeight(32.0f);
    ghost_table.AddColumn(L"A", 120.0f);
    ghost_table.AddColumn(L"Note");
    ghost_table.AddColumn(L"Run", 80.0f);
    ghost_table.CellText([](size_t row, size_t col, std::wstring& out) {
        if (col == 0) out = L"cell " + std::to_wstring(row);
        else out.clear();
    });
    ghost_table.BindTextBox(
        1, [](size_t row) { return row < g_ghost_notes.size() ? g_ghost_notes[row] : std::wstring{}; },
        [](size_t row, std::wstring value) {
            if (row < g_ghost_notes.size()) g_ghost_notes[row] = std::move(value);
        });
    ghost_table.BindButton(2, L"Run", [](size_t) {});
    ghost_table.RowCount(24);
    ghost_card.ForceSpotlight();

    // 轮播：页面必须实际渲染内容（回归：Measure 跳过子页导致整页空白）。
    auto& rider = root.Add<Carousel>();
    rider.Card(Panel::CardStyle::Input, 12.0f);
    auto& rider_page = rider.AddPage<StackPanel>();
    rider_page.Padding(16.0f, 12.0f)
        .Spacing(6.0f)
        .AlignCross(StackPanel::CrossAlign::Center)
        .AlignMain(StackPanel::MainAlign::Center);
    rider_page.Add<Label>(L"轮播页内容", TextRole::Body);
    rider_page.Add<Label>(L"第二行", TextRole::Caption).Secondary(true);

    // SplitView：两栏内容必须实际渲染（回归：Measure 跳过 + 绝对坐标双重偏移）。
    auto& shell = root.Add<SplitView>();
    shell.PaneLength(150.0f);
    shell.Pane().Padding(12.0f, 10.0f).Spacing(6.0f);
    shell.Pane().Add<Label>(L"总览", TextRole::Caption);
    shell.Content().Padding(16.0f, 12.0f);
    shell.Content().Add<Label>(L"主区内容", TextRole::Caption);
    StackPanel& shell_pane = shell.Pane();
    StackPanel& shell_content = shell.Content();

    root.Measure({700.0f, 1060.0f}, theme);
    root.Arrange({0.0f, 0.0f, 700.0f, 1060.0f});

    ID2D1DeviceContext2* dc = renderer.BeginDraw();
    Painter painter;
    painter.BeginFrame(dc, &UiText(), 1.0f);
    painter.FillRect({0, 0, 700, 1060}, theme.bg);
    DrawControlTree(painter, theme, &root);
    painter.EndFrame();
    Check(renderer.EndDraw(), "enddraw (extras)");

    std::vector<uint8_t> snapshot;
    Check(renderer.ReadBack(snapshot), "readback (extras)");
    const auto read_pixel = [&](int x, int y, Color& out) {
        if (x < 0 || y < 0 || x >= renderer.Width() || y >= renderer.Height() ||
            snapshot.size() != static_cast<size_t>(renderer.Width()) * renderer.Height() * 4) {
            return false;
        }
        const uint8_t* px = snapshot.data() +
                            (static_cast<size_t>(y) * renderer.Width() + x) * 4;
        out = {px[2] / 255.0f, px[1] / 255.0f, px[0] / 255.0f, px[3] / 255.0f};
        return true;
    };

    Color c{};
    // 骨架首行：呼吸相位 0 → 0.72 倍 fill_hover 合成
    const Rect sk = skeleton->AbsoluteBounds();
    read_pixel(static_cast<int>(sk.x + sk.w * 0.5f), static_cast<int>(sk.y + 3.0f), c);
    Color expected = theme.fill_hover;
    expected.a *= 0.72f;
    Check(CloseTo(c, Over(expected, theme.bg)), "skeleton breathing fill");

    // 星级：填充星中心亮于未填充描边星中心
    const Rect rt = rating->AbsoluteBounds();
    Color filled{};
    Color dim{};
    read_pixel(static_cast<int>(rt.x + 10.0f), static_cast<int>(rt.y + 10.0f), filled);
    read_pixel(static_cast<int>(rt.x + 4 * 24 + 10.0f), static_cast<int>(rt.y + 10.0f), dim);
    Check(filled.r > dim.r + 0.10f && filled.g > dim.g + 0.10f, "rating filled vs dim star");

    // 头像：圈底合成 + Online 状态点亮度
    const Rect av = avatar->AbsoluteBounds();
    read_pixel(static_cast<int>(av.x + 10.0f), static_cast<int>(av.y + 8.0f), c);
    Check(CloseTo(c, Over(theme.fill_input_hover, theme.bg)), "avatar circle fill");
    read_pixel(static_cast<int>(av.x + 28.76f), static_cast<int>(av.y + 28.76f), c);
    Check(c.r > theme.fill_input_hover.r + 0.3f, "avatar presence dot lit");

    // InfoBadge 圆点：accent 实心（白），避免采到计数胶囊上的黑字。
    {
        const Rect ib = info_dot->AbsoluteBounds();
        read_pixel(static_cast<int>(ib.x + ib.w * 0.5f), static_cast<int>(ib.y + ib.h * 0.5f), c);
        Check(c.r > 0.70f && c.g > 0.70f && c.b > 0.70f, "info badge dot fill");
    }

    // 开关分割钮选中态：主区垫 fill_selected
    const Rect sp = toggle->AbsoluteBounds();
    read_pixel(static_cast<int>(sp.x + 10.0f), static_cast<int>(sp.y + 8.0f), c);
    Check(CloseTo(c, Over(theme.fill_selected, Over(theme.fill_input, theme.bg))),
          "toggle split checked wash");

    // 步骤条：末步空心圆内（距圆心 6px 处）必须是背景；线段中点必须是分隔线亮色。
    {
        const float tw = UiText().MeasureText(L"验证", TextRole::Caption).w;
        const float stride = 28.0f + tw + 16.0f + 24.0f;   // 内容宽(20圆+8距+文本) + 留白 + 净线长
        const Rect st = steps->AbsoluteBounds();
        const float cy = st.y + 13.0f;                     // kCircle*0.5 + 3
        const float cx3 = st.x + 8.0f + 3.0f * stride + 10.0f;   // “验证”圆心
        read_pixel(static_cast<int>(cx3 - 6.0f), static_cast<int>(cy), c);
        Check(CloseTo(c, theme.bg), "stepper line stays outside circle");
        // 线段 = 上一步文字末尾 + 8 留白起，净长 24；取中点。
        const float seg_x0 = st.x + 8.0f + 2.0f * stride + 28.0f + tw + 8.0f;
        Color line{};
        read_pixel(static_cast<int>(seg_x0 + 12.0f), static_cast<int>(cy), line);
        Check(line.r > theme.bg.r + 0.02f, "stepper connector visible");
        // 线不得穿过标题文字：文字末尾与线起点之间的空隙必须是背景。
        read_pixel(static_cast<int>(seg_x0 - 4.0f), static_cast<int>(cy), c);
        Check(CloseTo(c, theme.bg), "stepper line clear of label text");
    }

    // 分页器：绘制必须落在自身矩形内（回归：曾按局部坐标画到窗口原点、盖住别的控件）。
    {
        const Rect pg = pager->AbsoluteBounds();
        const int cy = static_cast<int>(pg.y + pg.h * 0.5f);
        int ink = 0;
        for (int dx = 0; dx < 48; ++dx) {
            read_pixel(static_cast<int>(pg.x) + dx, cy, c);
            if (c.r > 0.06f) ++ink;
        }
        Check(ink >= 4, "pagination renders inside its bounds");
        bool stray = false;
        for (int dx = 30; dx > 20; --dx) {
            read_pixel(static_cast<int>(pg.x) - dx, cy, c);
            if (c.r > 0.02f) stray = true;
        }
        Check(!stray, "pagination ink stays right of its bounds");
    }

    // 菜单栏：扫“File”标题墨迹的上下留白，须对称（垂直居中）。
    {
        const Rect mb = bar->AbsoluteBounds();
        int top = -1;
        int bottom = -1;
        for (int y = static_cast<int>(mb.y); y < static_cast<int>(mb.Bottom()); ++y) {
            for (int x = static_cast<int>(mb.x) + 10; x < static_cast<int>(mb.x) + 34; ++x) {
                read_pixel(x, y, c);
                if (c.r > 0.3f) {
                    if (top < 0) top = y;
                    bottom = y;
                    break;
                }
            }
        }
        const int gap_top = top - static_cast<int>(mb.y);
        const int gap_bottom = static_cast<int>(mb.Bottom()) - 1 - bottom;
        Check(top > 0 && gap_top >= 0 && gap_bottom >= 0 && (gap_top - gap_bottom <= 3) &&
                  (gap_bottom - gap_top <= 3),
              "menubar title vertically centered");

        // “File” 墨迹在自身 slot（x=+10, w=文本+20）内须水平居中。
        const float slot_x = mb.x + 10.0f;
        const float slot_right = slot_x + UiText().MeasureText(L"File", TextRole::Body).w + 20.0f;
        int ink_min = -1;
        int ink_max = -1;
        for (int x = static_cast<int>(mb.x); x < static_cast<int>(slot_right) + 2; ++x) {
            for (int y = top; y <= bottom; ++y) {
                read_pixel(x, y, c);
                if (c.r > 0.3f) {
                    if (ink_min < 0) ink_min = x;
                    ink_max = x;
                    break;
                }
            }
        }
        const int gap_left = ink_min - static_cast<int>(slot_x);
        const int gap_right = static_cast<int>(slot_right) - ink_max;
        Check(ink_min > 0 && gap_left >= 4 && gap_right >= 4 &&
                  (gap_left - gap_right <= 4) && (gap_right - gap_left <= 4),
              "menubar title horizontally centered");
    }

    // 聚光卡内表格：缓冲行的避光垫必须被表体视口裁掉（悬停时不再露出控件剪影）。
    {
        const Rect gt = ghost_table.AbsoluteBounds();
        const float ghost_x = gt.x + 120.0f + (gt.w - 200.0f) * 0.5f;   // Note 弹性列中心
        bool stray = false;
        for (int dy = 6; dy < 60; ++dy) {
            const int y = static_cast<int>(gt.Bottom()) + dy;
            Color px{};
            Color ref{};
            read_pixel(static_cast<int>(ghost_x), y, px);
            read_pixel(40, y, ref);
            if (std::fabs(px.r - ref.r) > 0.01f || std::fabs(px.g - ref.g) > 0.01f ||
                std::fabs(px.b - ref.b) > 0.01f) {
                stray = true;
            }
        }
        Check(!stray, "spotlight pads clipped to table body");
    }

    // 轮播页内容：卡内（避开边缘）必须有文字墨迹。
    {
        const Rect rc = rider.AbsoluteBounds();
        int ink = 0;
        for (int y = static_cast<int>(rc.y) + 12; y < static_cast<int>(rc.Bottom()) - 16; ++y) {
            for (int x = static_cast<int>(rc.x) + 20; x < static_cast<int>(rc.Right()) - 20; ++x) {
                read_pixel(x, y, c);
                if (c.r > 0.25f) ++ink;
            }
        }
        Check(ink > 20, "carousel page renders content");
    }

    // SplitView：侧栏与主区都须在各自矩形内渲染出文字墨迹。
    {
        auto any_ink = [&](const Rect& r) {
            for (int y = static_cast<int>(r.y) + 4; y < static_cast<int>(r.Bottom()) - 4; ++y) {
                for (int x = static_cast<int>(r.x) + 4; x < static_cast<int>(r.Right()) - 4; ++x) {
                    read_pixel(x, y, c);
                    if (c.r > 0.25f) return true;
                }
            }
            return false;
        };
        Check(any_ink(shell_pane.AbsoluteBounds().Inset(4.0f, 4.0f)),
              "splitview pane renders content");
        Check(any_ink(shell_content.AbsoluteBounds().Inset(4.0f, 4.0f)),
              "splitview content renders");
    }

    Check(renderer.SavePNG(path), "save png (extras)");
    renderer.Shutdown();
}

void TestGlowPrimitives() {
    OffscreenRenderer renderer;
    if (!renderer.Init(420, 240)) {
        Check(false, "glow renderer init");
        return;
    }
    const Theme theme = MakeTheme();
    TestRoot root;
    root.Padding(24.0f, 24.0f).Spacing(16.0f);
    auto& card = root.Add<StackPanel>();
    card.Card(Panel::CardStyle::Input, 12.0f);
    card.Padding(16.0f, 18.0f);
    card.Add<Label>(L" ");
    auto& row = root.Add<Row>().Spacing(12.0f);
    row.Add<Button>(L"标准");
    Button* primary = &row.Add<Button>(L"主要", ButtonKind::Primary);
    auto& lumen = root.Add<TestSpotlightCard>();
    lumen.Card(Panel::CardStyle::Lumen, 14.0f);
    lumen.Padding(16.0f, 20.0f);
    lumen.Add<Label>(L"SPOT");
    lumen.ForceSpotlight();
    root.Measure({420.0f, 240.0f}, theme);
    root.Arrange({0.0f, 0.0f, 420.0f, 240.0f});

    ID2D1DeviceContext2* dc = renderer.BeginDraw();
    Painter painter;
    painter.BeginFrame(dc, &UiText(), 1.0f);
    painter.FillRect({0.0f, 0.0f, 420.0f, 240.0f}, theme.bg);
    DrawControlTree(painter, theme, &root);
    painter.EndFrame();
    Check(renderer.EndDraw(), "glow enddraw");

    const Rect cb = card.AbsoluteBounds();
    Color top{}, bot{};
    renderer.ReadPixel(static_cast<int>(cb.x + cb.w * 0.5f), static_cast<int>(cb.y + 1.0f), top);
    renderer.ReadPixel(static_cast<int>(cb.x + cb.w * 0.5f), static_cast<int>(cb.Bottom() - 1.0f),
                       bot);
    Check(top.r > bot.r + 0.008f, "card top specular brighter than bottom shade");

    const Rect pb = primary->AbsoluteBounds();
    Color halo{};
    renderer.ReadPixel(static_cast<int>(pb.x - 4.0f), static_cast<int>(pb.y + pb.h * 0.5f), halo);
    Check(halo.r > theme.bg.r + 0.006f, "primary dual-layer glow");

    {
        ID2D1DeviceContext2* dc2 = renderer.BeginDraw();
        Painter p2;
        p2.BeginFrame(dc2, &UiText(), 1.0f);
        p2.FillRect({0.0f, 0.0f, 420.0f, 240.0f}, theme.bg);
        const Rect elevated{80.0f, 80.0f, 160.0f, 72.0f};
        DrawElevated(p2, theme, elevated, 12.0f, Elevation::Overlay, theme.bg);
        p2.EndFrame();
        Check(renderer.EndDraw(), "elevated glow enddraw");
        Color ear{};
        renderer.ReadPixel(static_cast<int>(elevated.x + 2), static_cast<int>(elevated.y + 2), ear);
        Check(ear.r > theme.bg.r + 0.004f, "elevated glow wraps corner");
    }

    renderer.Shutdown();
}

void TestAcrylic() {
    OffscreenRenderer renderer;
    if (!renderer.Init(240, 160)) {
        Check(false, "acrylic renderer init");
        return;
    }
    ID2D1DeviceContext2* dc = renderer.BeginDraw();
    Painter painter;
    painter.BeginFrame(dc, &UiText(), 1.0f);
    painter.FillRect({0.0f, 0.0f, 240.0f, 160.0f}, Color{0.0f, 0.0f, 0.0f, 1.0f});
    painter.FillRoundedRect({80.0f, 50.0f, 80.0f, 60.0f}, 8.0f, Color{1.0f, 1.0f, 1.0f, 1.0f});
    Check(painter.CaptureAcrylic(), "acrylic capture");
    painter.DrawAcrylic({0.0f, 0.0f, 240.0f, 160.0f}, 14.0f, 0.22f);
    painter.EndFrame();
    Check(renderer.EndDraw(), "acrylic enddraw");
    Color core{}, bleed{}, corner{};
    renderer.ReadPixel(120, 80, core);
    renderer.ReadPixel(68, 80, bleed);
    renderer.ReadPixel(6, 6, corner);
    Check(core.r > bleed.r + 0.04f, "acrylic core brighter than halo");
    Check(bleed.r > corner.r + 0.008f, "acrylic blur bleeds past edge");
    renderer.Shutdown();
}

void TestChoreography() {
    {
        TestRoot root;
        auto& host = root.Add<PageHost>();
        host.Page(L"a").Add<Panel>().SetBounds({0.0f, 0.0f, 12.0f, 10.0f});
        host.Page(L"b").Add<Panel>().SetBounds({0.0f, 0.0f, 12.0f, 20.0f});
        Check(host.Current() == L"a", "pagehost default current");
        Check(host.Child(0).Visible() && !host.Child(1).Visible(), "pagehost first visible");
        host.Show(L"b");
        Check(host.Current() == L"b", "pagehost show current");
        Check(host.Direction() == 1, "pagehost direction down");
        Check(!host.Child(0).Visible() && host.Child(1).Visible(), "pagehost snap hides old");
        host.Show(L"a");
        Check(host.Direction() == -1, "pagehost direction up");
        Check(host.Child(0).Visible() && !host.Child(1).Visible(), "pagehost snap back");
    }
}

void TestDefaultChrome() {
    Window window(L"default-chrome");
    Check(window.TitleBar() != nullptr, "Window(title) default Client frame");
    Check(window.Backdrop() == Backdrop::All, "Window(title) default Backdrop::All");
    window.LayoutNow();
    TitleBar* bar = window.TitleBar();
    Check(bar && bar->Title() == L"default-chrome", "default title bar caption");

    OffscreenRenderer renderer;
    constexpr int kW = 960;
    constexpr int kH = 200;
    if (!renderer.Init(kW, kH)) {
        Check(false, "default chrome renderer init");
        return;
    }
    const Theme& theme = window.VisualTheme();
    ID2D1DeviceContext2* dc = renderer.BeginDraw();
    Painter painter;
    painter.BeginFrame(dc, &UiText(), 1.0f);
    const Rect client{0.0f, 0.0f, static_cast<float>(kW), static_cast<float>(kH)};
    painter.FillRect(client, theme.bg);
    painter.FillRectRadial(client, {client.w * 0.5f, client.h * 0.15f}, client.w * 0.9f,
                           theme.ambient_flare,
                           Color{theme.ambient_flare.r, theme.ambient_flare.g,
                                 theme.ambient_flare.b, 0.0f},
                           0.7f);
    if (bar) DrawControlTree(painter, theme, bar);
    painter.EndFrame();
    Check(renderer.EndDraw(), "default chrome enddraw");

    Color flare{}, void_px{};
    renderer.ReadPixel(kW / 2, 80, flare);
    renderer.ReadPixel(8, kH - 8, void_px);
    Check(flare.r > void_px.r + 0.004f, "default chrome ambient flare at top");

    int ink = 0;
    for (int x = 16; x < kW - 16; ++x) {
        Color px{};
        renderer.ReadPixel(x, 20, px);
        if (px.r > 0.25f) ++ink;
    }
    Check(ink > 8, "default chrome title bar caption brightness");
    renderer.Shutdown();

    Window system(L"system-chrome", {320.0f, 200.0f});
    Check(system.TitleBar() == nullptr, "three-arg Window keeps System frame");
    Check(system.Backdrop() == Backdrop::None, "three-arg Window keeps Backdrop::None");
}

void TestInjectedInput() {
    Window window(L"inject-input", {320.0f, 96.0f});
    auto& row = window.Root().Add<Row>().Spacing(8.0f);
    auto& first = row.Add<Button>(L"A");
    auto& second = row.Add<Button>(L"B");
    int clicks = 0;
    first.OnClick([&] { ++clicks; });
    window.LayoutNow();

    Check(window.DispatchKey(VK_TAB), "inject tab consumed");
    Check(window.Focused() == &first, "inject tab focuses first button");
    Check(window.DispatchKey(VK_TAB), "inject tab again consumed");
    Check(window.Focused() == &second, "inject tab focuses second button");

    const Rect a = first.AbsoluteBounds();
    Check(a.w > 1.0f && a.h > 1.0f, "inject layout sizes button");
    const Point center{a.x + a.w * 0.5f, a.y + a.h * 0.5f};
    window.DispatchMouseMove(center);
    Check(window.Hovered() == &first, "inject hover first button");
    window.DispatchMouseDown(center);
    window.DispatchMouseUp(center);
    Check(clicks == 1, "inject click fires");
    Check(window.Focused() == &first, "inject click focuses first button");
}

void TestPointer() {
    {
        Window window(L"touch-slop", {320.0f, 96.0f});
        window.Root().Padding(16.0f);
        auto& btn = window.Root().Add<Button>(L"Hit");
        int clicks = 0;
        btn.OnClick([&] { ++clicks; });
        window.LayoutNow();
        const Rect a = btn.AbsoluteBounds();
        Check(a.x > 4.0f, "touch slop button inset");
        const Point outside{a.x - 4.0f, a.y + a.h * 0.5f};
        const Point center{a.x + a.w * 0.5f, a.y + a.h * 0.5f};
        window.DispatchTouchDown(outside);
        Check(window.Hovered() == &btn, "touch slop hits button");
        window.DispatchTouchUp(center);
        Check(clicks == 1, "touch slop press then release clicks");
        window.Close();
    }
    {
        Window window(L"mouse-no-slop", {320.0f, 96.0f});
        window.Root().Padding(16.0f);
        auto& btn = window.Root().Add<Button>(L"Hit");
        int clicks = 0;
        btn.OnClick([&] { ++clicks; });
        window.LayoutNow();
        const Rect a = btn.AbsoluteBounds();
        const Point outside{a.x - 4.0f, a.y + a.h * 0.5f};
        window.DispatchMouseDown(outside);
        window.DispatchMouseUp(outside);
        Check(clicks == 0 && window.Hovered() != &btn, "mouse has no hit slop");
        window.Close();
    }
    {
        Window window(L"touch-pan", {240.0f, 160.0f});
        auto& sv = window.Root().Add<ScrollViewer>().Grow();
        sv.Add<Panel>().SetBounds({0.0f, 0.0f, 40.0f, 800.0f});
        window.LayoutNow();
        Check(sv.ContentHeight() > sv.AbsoluteBounds().h + 1.0f, "touch pan overflow");
        const Rect r = sv.AbsoluteBounds();
        const Point start{r.x + r.w * 0.5f, r.y + 48.0f};
        window.DispatchTouchDown(start);
        window.DispatchTouchMove({start.x, start.y - 24.0f});
        window.DispatchTouchMove({start.x, start.y - 72.0f});
        window.DispatchTouchUp({start.x, start.y - 72.0f});
        Check(sv.OffsetY() > 20.0f, "touch pan increases offset");
        window.Close();
    }
    {
        Window window(L"touch-slider", {280.0f, 80.0f});
        auto& slider = window.Root().Add<Slider>();
        slider.Range(0.0f, 100.0f).Value(0.0f);
        window.LayoutNow();
        const Rect r = slider.AbsoluteBounds();
        const Point at{r.x + r.w * 0.85f, r.y + r.h * 0.5f};
        window.DispatchTouchDown(at);
        Check(slider.Value() > 50.0f, "touch slider tracks");
        window.DispatchTouchUp(at);
        window.Close();
    }
}

void TestDirtyRects() {
    const Rect a{10.0f, 20.0f, 30.0f, 40.0f};
    const Rect b{25.0f, 30.0f, 30.0f, 40.0f};
    const Rect u = UnionRect(a, b);
    Check(u.x == 10.0f && u.y == 20.0f, "union origin");
    Check(u.Right() == 55.0f && u.Bottom() == 70.0f, "union extent");
    const Rect from_empty = UnionRect({}, a);
    Check(from_empty.x == a.x && from_empty.w == a.w && from_empty.h == a.h, "union empty lhs");
    const Rect distant{200.0f, 200.0f, 10.0f, 10.0f};
    const Rect box = UnionRect(a, distant);
    Check(box.x == 10.0f && box.Right() == 210.0f && box.Bottom() == 210.0f, "union disjoint");

    Window window(L"dirty-host", {400.0f, 280.0f});
    auto& col = window.Root().Add<Column>().Spacing(8.0f);
    auto& btn = col.Add<TestDirtyControl>(L"Pad");
    auto& spot = col.Add<TestDirtyControl>(L"Spot");
    spot.Spotlight(true);
    window.Show();
    window.LayoutNow();

    const Rect abs = btn.AbsoluteBounds();
    const Rect dirty = btn.DirtyBounds();
    Check(!abs.IsEmpty(), "dirty button laid out");
    Check(std::fabs(dirty.x - (abs.x - kDirtyPadDip)) < 0.01f, "dirty pad x");
    Check(std::fabs(dirty.y - (abs.y - kDirtyPadDip)) < 0.01f, "dirty pad y");
    Check(std::fabs(dirty.w - (abs.w + kDirtyPadDip * 2.0f)) < 0.01f, "dirty pad w");
    Check(std::fabs(dirty.h - (abs.h + kDirtyPadDip * 2.0f)) < 0.01f, "dirty pad h");

    const Rect spot_abs = spot.AbsoluteBounds();
    const Rect spot_dirty = spot.DirtyBounds();
    Check(!spot_abs.IsEmpty(), "spotlight button laid out");
    Check(spot_dirty.x == spot_abs.x && spot_dirty.y == spot_abs.y &&
              spot_dirty.w == spot_abs.w && spot_dirty.h == spot_abs.h,
          "spotlight dirty is whole control");

    btn.Invalidate();
    window.Invalidate();
    window.Close();
}

void TestUia() {
    Window window(L"uia-host", {420.0f, 220.0f});
    auto& col = window.Root().Add<Column>().Spacing(8.0f);
    int clicks = 0;
    auto& btn = col.Add<Button>(L"UiaInvoke");
    btn.OnClick([&] { ++clicks; });
    auto& box = col.Add<CheckBox>(L"UiaCheck");
    auto& slider = col.Add<Slider>();
    slider.Range(0.0f, 100.0f).Value(25.0f);
    window.Show();
    window.LayoutNow();

    IUIAutomation* uia = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_IUIAutomation, reinterpret_cast<void**>(&uia));
    Check(SUCCEEDED(hr) && uia, "uia CoCreateInstance");
    if (!uia) {
        window.Close();
        return;
    }

    IUIAutomationElement* root = nullptr;
    hr = uia->ElementFromHandle(static_cast<UIA_HWND>(window.NativeHandle()), &root);
    Check(SUCCEEDED(hr) && root, "uia ElementFromHandle");
    if (!root) {
        uia->Release();
        window.Close();
        return;
    }

    auto find_named = [&](const wchar_t* label) -> IUIAutomationElement* {
        VARIANT name;
        VariantInit(&name);
        name.vt = VT_BSTR;
        name.bstrVal = SysAllocString(label);
        IUIAutomationCondition* cond = nullptr;
        uia->CreatePropertyCondition(UIA_NamePropertyId, name, &cond);
        VariantClear(&name);
        IUIAutomationElement* found = nullptr;
        if (cond) {
            root->FindFirst(TreeScope_Descendants, cond, &found);
            cond->Release();
        }
        return found;
    };

    IUIAutomationElement* found = find_named(L"UiaInvoke");
    Check(found != nullptr, "uia find invoke button");
    if (found) {
        IUIAutomationInvokePattern* invoke = nullptr;
        found->GetCurrentPatternAs(UIA_InvokePatternId, IID_IUIAutomationInvokePattern,
                                   reinterpret_cast<void**>(&invoke));
        Check(invoke != nullptr, "uia invoke pattern");
        if (invoke) {
            hr = invoke->Invoke();
            Check(SUCCEEDED(hr) && clicks == 1, "uia invoke click");
            invoke->Release();
        }
        found->Release();
    }

    found = find_named(L"UiaCheck");
    Check(found != nullptr, "uia find checkbox");
    if (found) {
        IUIAutomationTogglePattern* toggle = nullptr;
        found->GetCurrentPatternAs(UIA_TogglePatternId, IID_IUIAutomationTogglePattern,
                                   reinterpret_cast<void**>(&toggle));
        Check(toggle != nullptr, "uia toggle pattern");
        if (toggle) {
            toggle->Toggle();
            Check(box.Checked(), "uia toggle checks");
            toggle->Release();
        }
        found->Release();
    }

    VARIANT type;
    VariantInit(&type);
    type.vt = VT_I4;
    type.lVal = UIA_SliderControlTypeId;
    IUIAutomationCondition* slider_cond = nullptr;
    uia->CreatePropertyCondition(UIA_ControlTypePropertyId, type, &slider_cond);
    VariantClear(&type);
    IUIAutomationElement* slider_el = nullptr;
    if (slider_cond) {
        root->FindFirst(TreeScope_Descendants, slider_cond, &slider_el);
        slider_cond->Release();
    }
    Check(slider_el != nullptr, "uia find slider");
    if (slider_el) {
        IUIAutomationRangeValuePattern* range = nullptr;
        slider_el->GetCurrentPatternAs(UIA_RangeValuePatternId, IID_IUIAutomationRangeValuePattern,
                                       reinterpret_cast<void**>(&range));
        Check(range != nullptr, "uia range pattern");
        if (range) {
            range->SetValue(80.0);
            Check(slider.Value() > 79.0f && slider.Value() < 81.0f, "uia range set");
            range->Release();
        }
        slider_el->Release();
    }

    root->Release();
    window.Close();
    uia->Release();
}

void TestDebugChecks() {
    Control::SetDebugHandler([](const wchar_t*) { throw std::runtime_error("lumen-debug"); });
    int hits = 0;
    auto trap = [&](auto&& fn) {
        try {
            fn();
        } catch (const std::runtime_error& e) {
            if (std::strcmp(e.what(), "lumen-debug") == 0) ++hits;
        }
    };

    {
        TestRoot root;
        root.Add<Label>(L"a");
        trap([&] { root.Child(999); });
    }
    {
        struct Probe : Label {
            void FakeParent(Panel* p) { parent_ = p; }
        };
        TestRoot root;
        auto owned = std::make_unique<Probe>();
        owned->FakeParent(&root);
        trap([&] { root.Add(std::unique_ptr<Control>(std::move(owned))); });
    }
    {
        Window window(L"debug-check", {240.0f, 120.0f});
        auto& label = window.Root().Add<Label>(L"x");
        std::thread worker([&] { trap([&] { (void)label.Text(); }); });
        worker.join();
        window.Close();
    }

    Control::SetDebugHandler(nullptr);
    Check(hits == 3, "debug checks child/add/thread");
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);   // 崩溃时也要能看到已通过的断言
    AddVectoredExceptionHandler(1, CrashReport);
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        std::printf("[FAIL] CoInitializeEx\n");
        return 1;
    }
    TestSignal();
    TestLayout();
    TestTypography();
    TestInteraction();
    TestImageViewRendering();
    TestExtras();
    TestGlowPrimitives();
    TestAcrylic();
    TestChoreography();
    TestDefaultChrome();
    TestInjectedInput();
    TestPointer();
    TestDirtyRects();
    TestUia();
    TestDebugChecks();
    RenderScene(L"lumen_visual_dark.png");
    RenderListScene(L"lumen_visual_lists.png");
    RenderExtrasScene(L"lumen_visual_extras.png");
    CoUninitialize();
    std::printf("%s\n", g_failures == 0 ? "ALL PASS" : "FAILURES PRESENT");
    return g_failures == 0 ? 0 : 1;
}
