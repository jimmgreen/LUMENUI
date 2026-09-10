// chain_compile.cpp — 10.1 回归：每个默认可构造控件都能链基类 setter。
// 另含修复包行为断言（Signal 生命期 / TextBox 同值 / NumberBox 解析 / Post 与 RunAsync 协议）。
#include <windows.h>
#include <lumen/lumen.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <cstdio>
#include <thread>
#include "../../src/core/dispatch_state.h"

#define LUMEN_CHAIN(T) \
    host.Add<T>().OnFocused([](bool) {}).Margin(4.0f).Visible(true).ToolTip(L"").ToolTipDelay(0.12f).Grow().Enabled(true)

namespace {
int g_behavior_failures = 0;

void Check(bool ok, const char* name) {
    std::printf("%s %s\n", ok ? "[PASS]" : "[FAIL]", name);
    if (!ok) ++g_behavior_failures;
}

// 轮询任务终态（最长 ~5s），返回到达的终态。
template <class Wait>
lumen::TaskStatus WaitFor(const lumen::TaskHandle& task, Wait extra) {
    for (int i = 0; i < 500; ++i) {
        (void)extra();
        const auto status = task.Status();
        if (status != lumen::TaskStatus::Running && status != lumen::TaskStatus::CancelRequested &&
            lumen::App::RunningTasks() == 0) return status;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return task.Status();
}

// Close() 是异步请求（投 WM_CLOSE）；泵一轮消息让销毁真正走完。
void PumpOnce() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}
} // namespace

// 宿主嵌入 / 自定义字体 API 编译回归（不执行）。
[[maybe_unused]] void HostApiCompiles() {
    lumen::App::HostMode(true);
    (void)lumen::App::HostMode();
    lumen::App::LumaTextLibrary(L"C:\\plugin\\lumatext.dll");
    (void)lumen::App::AddFont(std::span<const std::byte>{});
    (void)lumen::App::AddFont(L"C:\\plugin\\SJQY.ttf");
    lumen::WindowSpec spec{.title = L"host", .size = {480.0f, 380.0f}, .owner = nullptr, .parent = nullptr, .frameTarget = nullptr};
    lumen::Column host;
    host.Add<lumen::Label>(L"C").FontFamily(L"SJQY").Role(lumen::TextRole::Body);
    host.Add<lumen::CheckBox>(L"C").Role(lumen::TextRole::Caption);
    host.Add<lumen::RichLabel>().Font(L"C", L"SJQY").Add(L"12@200");
    host.Add<lumen::Table>().CellCharacterFont(L"ABCD", L"SJQY");
    static_assert(noexcept(lumen::App::HasActiveCallbacks()));
    (void)lumen::App::HasActiveCallbacks();
    (void)lumen::App::CanShutdown();
    lumen::App::Shutdown();
}

[[maybe_unused]] void HostFocusApi(lumen::Window& w, lumen::Button& b) {
    w.ClearFocus();
    b.Blur();
    w.OnNativeMessage([](std::uint32_t, std::uintptr_t, std::intptr_t) {});
    (void)w.BindNativeMessage([](std::uint32_t, std::uintptr_t, std::intptr_t) {});
}

int main() {
    Check(lumen::TextRoleStyle(lumen::TextRole::Body).size == 14.0f &&
              lumen::TextRoleStyle(lumen::TextRole::Caption).size == 12.0f &&
              lumen::TextRoleStyle(lumen::TextRole::Title).size == 20.0f &&
              lumen::TextRoleStyle(lumen::TextRole::Subtitle).size == 16.0f &&
              lumen::TextRoleStyle(lumen::TextRole::Numeric).size == 14.0f &&
              lumen::TextRoleStyle(lumen::TextRole::Mono).size == 12.0f &&
              lumen::TextRoleStyle(lumen::TextRole::Icon).size == 16.0f,
          "text role size contract");
    {
        lumen::Button compact(L"Pick", lumen::ButtonKind::Primary);
        compact.SizeClass(lumen::ButtonSize::Small);
        Check(compact.Role() == lumen::TextRole::CaptionStrong,
              "small primary keeps compact size and emphasis");
        compact.Role(lumen::TextRole::Caption);
        lumen::Button moved(std::move(compact));
        Check(moved.Role() == lumen::TextRole::Caption, "button move keeps explicit typography");
        lumen::Button assigned;
        assigned = std::move(moved);
        Check(assigned.Role() == lumen::TextRole::Caption, "button move assignment keeps typography");
        lumen::TextBox input;
        input.Role(lumen::TextRole::Caption);
        Check(input.PlaceholderRole() == input.Role(), "placeholder follows input typography");
        input.PlaceholderRole(lumen::TextRole::BodyStrong);
        input.Role(lumen::TextRole::Body);
        Check(input.PlaceholderRole() == lumen::TextRole::BodyStrong, "explicit placeholder role survives content change");
        lumen::NumberBox number;
        number.Role(lumen::TextRole::Caption).Value(12);
        Check(number.Role() == lumen::TextRole::Caption && number.PlaceholderRole() == number.Role(),
              "number typography override reaches effective content role");
        lumen::DropDownButton dropdown;
        dropdown.Role(lumen::TextRole::Caption).SizeClass(lumen::ButtonSize::Large);
        Check(dropdown.Role() == lumen::TextRole::Caption, "dropdown size does not discard explicit text role");
        lumen::ToggleButton toggle;
        toggle.Role(lumen::TextRole::Caption).Checked(true);
        Check(toggle.Role() == lumen::TextRole::Caption, "toggle state preserves typography");
    }
    lumen::Column host;
    host.Add<lumen::TextBox>().Placeholder(L"hint").PlaceholderRole(lumen::TextRole::Caption);
    host.Add<lumen::TextBox>().Text(L"top;bottom").Select(4, 10).OnFocused([](bool) {});
    LUMEN_CHAIN(lumen::Panel);
    LUMEN_CHAIN(lumen::StackPanel);
    LUMEN_CHAIN(lumen::Row);
    LUMEN_CHAIN(lumen::Column);
    LUMEN_CHAIN(lumen::WrapPanel);
    LUMEN_CHAIN(lumen::Grid);
    LUMEN_CHAIN(lumen::ZStack);
    LUMEN_CHAIN(lumen::Spacer);
    LUMEN_CHAIN(lumen::Button);
    LUMEN_CHAIN(lumen::RepeatButton);
    LUMEN_CHAIN(lumen::ToggleButton);
    LUMEN_CHAIN(lumen::DropDownButton);
    LUMEN_CHAIN(lumen::SplitButton);
    LUMEN_CHAIN(lumen::HyperlinkButton);
    LUMEN_CHAIN(lumen::CheckBox);
    LUMEN_CHAIN(lumen::RadioButton);
    LUMEN_CHAIN(lumen::Switch);
    LUMEN_CHAIN(lumen::Label);
    LUMEN_CHAIN(lumen::RichLabel);
    LUMEN_CHAIN(lumen::TextBox);
    LUMEN_CHAIN(lumen::PasswordBox);
    LUMEN_CHAIN(lumen::NumberBox);
    LUMEN_CHAIN(lumen::AutoSuggestBox);
    LUMEN_CHAIN(lumen::HotkeyBox);
    LUMEN_CHAIN(lumen::Slider);
    LUMEN_CHAIN(lumen::RangeSlider);
    LUMEN_CHAIN(lumen::ProgressBar);
    LUMEN_CHAIN(lumen::ProgressRing);
    LUMEN_CHAIN(lumen::Gauge);
    LUMEN_CHAIN(lumen::Sparkline);
    LUMEN_CHAIN(lumen::Chart);
    LUMEN_CHAIN(lumen::ComboBox);
    LUMEN_CHAIN(lumen::ListView);
    LUMEN_CHAIN(lumen::Table);
    LUMEN_CHAIN(lumen::TreeView);
    LUMEN_CHAIN(lumen::TreeTable);
    LUMEN_CHAIN(lumen::GridView);
    LUMEN_CHAIN(lumen::TabControl);
    LUMEN_CHAIN(lumen::Segmented);
    LUMEN_CHAIN(lumen::Chip);
    LUMEN_CHAIN(lumen::TokenBox);
    LUMEN_CHAIN(lumen::Badge);
    LUMEN_CHAIN(lumen::InfoBadge);
    LUMEN_CHAIN(lumen::IconView);
    LUMEN_CHAIN(lumen::Avatar);
    LUMEN_CHAIN(lumen::Rating);
    LUMEN_CHAIN(lumen::ImageView);
    LUMEN_CHAIN(lumen::Skeleton);
    LUMEN_CHAIN(lumen::Separator);
    LUMEN_CHAIN(lumen::Expander);
    LUMEN_CHAIN(lumen::SettingsCard);
    LUMEN_CHAIN(lumen::InfoBar);
    LUMEN_CHAIN(lumen::FormField);
    LUMEN_CHAIN(lumen::Form);
    LUMEN_CHAIN(lumen::GroupBox);
    LUMEN_CHAIN(lumen::ScrollViewer);
    LUMEN_CHAIN(lumen::SplitView);
    LUMEN_CHAIN(lumen::Splitter);
    LUMEN_CHAIN(lumen::Viewbox);
    LUMEN_CHAIN(lumen::Carousel);
    LUMEN_CHAIN(lumen::Stepper);
    LUMEN_CHAIN(lumen::Pagination);
    LUMEN_CHAIN(lumen::DatePicker);
    LUMEN_CHAIN(lumen::TimePicker);
    LUMEN_CHAIN(lumen::CalendarView);
    LUMEN_CHAIN(lumen::ColorPicker);
    LUMEN_CHAIN(lumen::FileDropZone);
    LUMEN_CHAIN(lumen::NavigationView);
    LUMEN_CHAIN(lumen::PageHost);
    LUMEN_CHAIN(lumen::Breadcrumb);
    LUMEN_CHAIN(lumen::MenuBar);
    LUMEN_CHAIN(lumen::CommandBar);
    LUMEN_CHAIN(lumen::StatusBar);
    LUMEN_CHAIN(lumen::TitleBar);
    LUMEN_CHAIN(lumen::LogView);
    LUMEN_CHAIN(lumen::Dialog);
    LUMEN_CHAIN(lumen::Flyout);
    LUMEN_CHAIN(lumen::TeachingTip);
    LUMEN_CHAIN(lumen::ToolTip);
    LUMEN_CHAIN(lumen::Drawer);
    LUMEN_CHAIN(lumen::BusyOverlay);
    LUMEN_CHAIN(lumen::EmptyState);
    // ColorSwatch 无默认构造，不进此链。
    (void)host;

    // —— R03：事件源先析构，连接断开安全 ——
    {
        int hits = 0;
        lumen::Connection conn;
        {
            lumen::Signal<int> src;
            conn = src.Connect([&hits](int) { ++hits; });
            src.Emit(1);
        }   // 源先亡
        conn.Disconnect();   // 不得访问已释放源
        Check(hits == 1, "signal emit before source death");
        Check(!static_cast<bool>(conn), "connection dead after source death");
    }
    {
        // 槽内销毁事件源：Emit 已把槽拷到栈上，必须安全跑完本槽。
        auto src = std::make_unique<lumen::Signal<>>();
        int hits = 0;
        lumen::Connection conn = src->Connect([&] {
            ++hits;
            src.reset();
        });
        src->Emit();
        conn.Disconnect();
        Check(hits == 1, "slot may destroy its own signal");
    }
    {
        // 令牌转移：Signal 移动后旧连接仍能断开新持有者。
        lumen::Signal<> a;
        lumen::Connection conn = a.Connect([] {});
        lumen::Signal<> b = std::move(a);
        conn.Disconnect();
        Check(b.Empty(), "disconnect reaches moved-to signal");
    }

    // —— R12：TextBox 同值同步不破坏编辑态 ——
    {
        lumen::TextBox box;
        box.Text(L"hello");
        box.Select(2, 3);
        box.Text(L"hello");   // 同值：光标/选区/撤销保持
        Check(box.SelectionStart() == 2 && box.SelectionEnd() == 3,
              "textbox same-value sync keeps caret and selection");
        box.Text(L"world");   // 真换文档：光标到末尾
        Check(box.SelectionStart() == 5, "textbox new value moves caret to end");
    }

    // —— R14：NumberBox 严格解析，非法草稿不静默变 min ——
    {
        struct NumberProbe : lumen::NumberBox {
            using NumberBox::OnKey;
            using NumberBox::AutomationSetRange;
            using NumberBox::AutomationSetValue;
        } box;
        box.Range(0.0, 100.0).Value(42.0);
        box.Text(L"abc");
        Check(std::fabs(box.Value() - 42.0) < 1e-9, "numberbox invalid draft keeps last value");
        box.Text(L"");
        Check(std::fabs(box.Value() - 42.0) < 1e-9, "numberbox empty draft keeps last value");
        box.Text(L"1e999");
        Check(std::fabs(box.Value() - 42.0) < 1e-9, "numberbox overflow rejected");
        box.Text(L"12abc");
        Check(std::fabs(box.Value() - 42.0) < 1e-9, "numberbox partial parse rejected");
        box.Text(L"3.5");
        Check(std::fabs(box.Value() - 3.5) < 1e-9, "numberbox valid draft readable");
        int notifications = 0;
        box.OnValueChanged([&](double) { ++notifications; });
        box.Text(L"20");
        box.OnKey(VK_UP);
        Check(box.Value() == 21.0 && notifications == 1, "numberbox steps from valid draft");
        box.Text(L"bad");
        box.OnKey(VK_DOWN);
        Check(box.Value() == 20.0, "numberbox invalid draft steps from committed value");
        box.Value(100.0);
        const int before = notifications;
        box.OnKey(VK_UP);
        Check(notifications == before, "numberbox at range limit does not notify unchanged value");
        lumen::Property<double> bound(30.0);
        box.BindValue(bound);
        Check(box.AutomationSetRange(45.0) && bound.Get() == 45.0,
              "numberbox UIA uses committing binding path");
        const double nan = std::numeric_limits<double>::quiet_NaN();
        box.Value(nan);
        Check(!box.AutomationSetRange(nan) && box.Value() == 45.0,
              "numberbox rejects nonfinite program and UIA values");
        Check(box.AutomationSetValue(L"46") && bound.Get() == 46.0,
              "numberbox UIA text pattern also commits binding");
        Check(!box.AutomationSetValue(L"46oops") && bound.Get() == 46.0,
              "invalid UIA text cannot mutate numeric value");
    }

    {
        auto source = std::make_unique<lumen::VectorModel<std::wstring>>();
        source->Reset({L"one", L"two"});
        lumen::FilteredModel filtered(*source);
        lumen::SortedModel sorted(filtered);
        struct UiaTable : lumen::Table { using Table::AutomationSetCellValue; };
        UiaTable table;
        table.AddColumn(L"Name");
        table.Bind(sorted);
        source.reset();
        lumen::ItemRow row;
        row.text = L"stale";
        sorted.Get(0, row);
        Check(filtered.Count() == 0 && sorted.Count() == 0 && table.RowCount() == 0 && row.text.empty(),
              "source destruction empties nested decorators and their bound view");
        filtered.Where({});
        sorted.OrderBy({});
        Check(sorted.Count() == 0, "detached decorators remain safely reusable");
    }

    // —— R02：Post 结果协议（真实窗口，不 Show、不泵消息）——
    // —— R01：RunAsync 任务状态机 ——
    {
        lumen::App app;
        lumen::Window window(L"lumen api", {320.0f, 240.0f}, lumen::Frame::System);
        Check(window.Post({}) == lumen::PostResult::Accepted, "post accepted on live window");
        Check(window.Post(nullptr) == lumen::PostResult::Accepted, "post empty is a no-op accept");

        std::atomic<int> ran{0};
        lumen::TaskHandle task = window.RunAsync([&ran] { ++ran; }, [] {});
        Check(WaitFor(task, [&] { return ran.load() > 0; }) == lumen::TaskStatus::Succeeded,
              "runasync success reaches terminal state");
        task.Cancel();   // 已结束：取消请求不再改变终态
        Check(task.Status() == lumen::TaskStatus::Succeeded, "cancel after finish is inert");

        lumen::TaskHandle bad =
            window.RunAsync([] { throw std::runtime_error("boom"); }, [] {});
        Check(WaitFor(bad, [] { return false; }) == lumen::TaskStatus::Failed,
              "runasync exception -> failed");
        Check(bad.Error().find(L"boom") != std::wstring::npos, "failed task keeps error text");

        std::atomic<bool> finish_cancel{false};
        lumen::TaskHandle cancel = window.RunAsync([&] {
            while (!finish_cancel.load()) std::this_thread::yield();
        }, [] {});
        cancel.Cancel();
        Check(cancel.CancelRequested(), "cancel request observable on running task");
        finish_cancel.store(true);
        WaitFor(cancel, [] { return false; });

        // 工作结果已发布仍不能卸载：TLS 析构是在函数返回之后执行的模块代码。
        std::atomic<bool> exiting{false}, release_exit{false};
        struct ExitGate {
            std::atomic<bool>* entered;
            std::atomic<bool>* release;
            ~ExitGate() {
                entered->store(true);
                while (!release->load()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        };
        auto finishing = window.RunAsync([&] {
            thread_local ExitGate gate{&exiting, &release_exit};
            (void)gate;
        }, [] {});
        for (int i = 0; i < 500 && !exiting.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        Check(exiting.load() && finishing.Status() == lumen::TaskStatus::Succeeded &&
              lumen::App::RunningTasks() > 0 && !lumen::App::CanShutdown(),
              "shutdown gate waits for actual thread exit including TLS destructors");
        release_exit.store(true);
        WaitFor(finishing, [] { return false; });

        // 窗口先亡：结果未能交付 → Dropped；此后 Post 一律拒绝。
        lumen::TaskHandle late = window.RunAsync(
            [] { std::this_thread::sleep_for(std::chrono::milliseconds(120)); }, [] {});
        window.Close();
        PumpOnce();   // 让 WM_CLOSE → DestroyWindow → NCDESTROY 同步走完
        Check(window.Post([] {}) == lumen::PostResult::Closed, "post rejected after close");
        Check(WaitFor(late, [] { return false; }) == lumen::TaskStatus::Succeeded && late.Delivery() == lumen::TaskDelivery::Dropped,
              "undelivered result marked dropped");
        Check(lumen::App::RunningTasks() == 0, "task counter drains to zero");
        Check(lumen::App::CanShutdown(), "can shutdown once tasks finish");
    }

    // —— R08：Table 布局查询（应用不再复制表头/滚动条/弹性下限常量）——
    {
        lumen::Table table;
        table.AddColumn(L"名称", 120.0f);
        table.AddColumn(L"数量", 80.0f);
        table.AddColumn(L"比例", 0.0f);   // 弹性
        table.RowCount(3);
        Check(std::fabs(table.ColumnPixelWidth(0, 800.0f) - 120.0f) < 0.5f,
              "fixed column reports pixel width");
        Check(table.ColumnPixelWidth(2, 800.0f) >= 96.0f,
              "flex column reports real width >= flex minimum");
        Check(table.ColumnsPixelWidth(800.0f) > 296.0f, "columns total includes flex columns");
        Check(table.ColumnPixelWidth(9, 800.0f) == 0.0f, "out-of-range column width is zero");
        const float natural = table.NaturalHeight(800.0f);
        Check(natural > 32.0f && natural < 300.0f,
              "natural height = header + rows, no phantom scrollbar");
    }

    // —— R15：Form 配置期校验 + ValidateAll ——
    {
        lumen::Form form;
        auto& field = form.Field(L"数量");
        lumen::NumberBox* nb = nullptr;
        field.Child(lumen::NumberBox{}.Value(42.0).Ref(nb))
            .Validate(lumen::validate::Range(1.0, 10.0));
        // 挂钩发生在配置阶段：首次布局前 Valid 就不可信（旧实现初始恒 true）。
        Check(field.HasError(), "invalid initial value caught before first layout");
        Check(!form.Valid().Get(), "form valid reflects field error pre-layout");
        nb->Value(5.0);   // 程序赋值静默：文本已改但错误文本保留（R13 契约）
        Check(field.HasError(), "program value write stays silent by contract");
        Check(form.ValidateAll(), "ValidateAll re-evaluates and clears stale error");
        Check(!field.HasError(), "ValidateAll surfaces the error text");
        nb->Text(L"999");   // 程序改文本同样静默：提交前显式 ValidateAll 抓住
        Check(!field.HasError(), "program text write stays silent by contract");
        Check(!form.ValidateAll(), "ValidateAll catches silent draft at submit");
    }

    // —— R17：TextBox 选区事件 / IME 组合查询 ——
    // （BindX 返回 RAII 连接，丢弃即断开；链上持久订阅用 OnX。）
    {
        lumen::TextBox box;
        int sel_events = 0;
        box.OnSelectionChanged([&] { ++sel_events; });
        box.Text(L"hello");   // 换文档：光标重置 → 事件
        box.Select(1, 3);     // 程序选区 → 事件
        box.Text(L"hello");   // 同值同步：不发
        if (sel_events != 2) std::printf("  [debug] sel_events=%d\n", sel_events);
        Check(sel_events == 2, "selection event fires on change, not same-value sync");
        Check(!box.Composing(), "composing query false offscreen");
    }

    // —— R18/R20：数字列默认文本渲染 + 数值排序 + 值变更重排 ——
    {
        struct Gauge {
            std::wstring name;
            double ratio = 0.0;
            int count = 0;
        };
        lumen::VectorModel<Gauge> model;
        model.Push({L"a", 30.0, 2});
        model.Push({L"b", 10.0, 100});
        model.Push({L"c", 20.0, 10});
        lumen::Table table;
        table.Bind(model).Column(L"名称", &Gauge::name, 100.0f).Column(L"比例", &Gauge::ratio, 80.0f)
            .Column(L"数量", &Gauge::count, 80.0f);
        // R18：浮点/整数成员默认文本渲染，进度条必须显式 .Progress(get)。
        Check(table.ColumnKind(1) == lumen::CellKind::Text, "double column renders as text");
        Check(table.ColumnKind(2) == lumen::CellKind::Text, "int column renders as text");
        table.ColumnPrecision(1, 1);
        table.SortBy(1, 1);   // 按比例升序：10, 20, 30（数值序，不是字典序）
        Check(table.DataRowAt(0) == 1 && table.DataRowAt(1) == 2 && table.DataRowAt(2) == 0,
              "numeric sort orders 10,20,30 not lexicographic");
        table.SortBy(2, 1);   // 按数量升序：2, 10, 100（旧实现字典序会排成 10,100,2）
        Check(table.DataRowAt(0) == 0 && table.DataRowAt(1) == 2 && table.DataRowAt(2) == 1,
              "int sort keeps numeric order 2,10,100");
        // R20：排序激活时值变更重算视图映射（旧实现只刷新不重排，箭头指向旧序）。
        model.At(0).count = 500;      // 数据行 0 的数量改成最大
        model.At(0, model.At(0));     // 两参 At 发 OnChanged
        Check(table.DataRowAt(0) == 2 && table.DataRowAt(2) == 0,
              "value change re-sorts active view");
        // R19：数字格编辑事务 API（编辑器交互序列待实机；锁定形状与编译回归）。
        table.CellEditable([](size_t data_row, int col) { return col > 0 || data_row > 0; });
        table.ColumnPrecision(2, 0);
    }

    {
        lumen::Property<int> a(0), b(0);
        int a_calls = 0, b_calls = 0;
        auto ac = a.OnChanged([&](const int& value) { ++a_calls; b = value + 1; });
        auto bc = b.OnChanged([&](const int&) { ++b_calls; });
        {
            lumen::UpdateScope outer;
            a = 1;
            { lumen::UpdateScope inner; a = 2; }
            Check(a.Get() == 2 && a_calls == 0, "nested update exposes values but defers notifications");
        }
        Check(a_calls == 1 && b_calls == 1 && b.Get() == 3,
              "batch coalesces values and drains callback changes in next wave");
        try {
            lumen::UpdateScope update;
            a = 4;
            throw std::runtime_error("original");
        } catch (const std::runtime_error&) {}
        Check(a_calls == 2 && b.Get() == 5 && !lumen::UpdateScope::Active(),
              "update scope restores state during exception unwinding");
    }
    {
        lumen::Form form;
        int calls = 0;
        form.Validate([&] {
            ++calls;
            if (calls == 1) form.RefreshValid();
            return std::wstring{};
        });
        Check(calls == 2 && form.Valid().Get(), "form validation coalesces reentrant refresh");
    }
    {
        struct Record { uint64_t id; std::wstring name; };
        auto model = std::make_shared<lumen::VectorModel<Record>>();
        model->Key([](const Record& row) { return row.id; });
        model->Map([](const Record& row, lumen::ItemRow& out) { out.text = row.name; });
        model->Reset({{11, L"a"}, {22, L"b"}, {33, L"c"}});
        auto filtered = std::make_shared<lumen::FilteredModel>(model);
        lumen::SortedModel sorted(filtered, [](size_t a, size_t b) { return a > b; });
        lumen::Table table;
        table.AddColumn(L"Name");
        table.Bind(sorted).SelectKey(22);
        Check(table.SelectedKey() == 22 && table.SourceRowAt(0) == 2,
              "nested models preserve stable keys and source mapping");
        model->Insert(0, {44, L"d"});
        Check(table.SelectedKey() == 22 && table.SourceRowAt(static_cast<size_t>(table.SelectedIndex())) == 2,
              "inserting before selection keeps business identity");
        filtered->Where([](size_t, const lumen::ItemRow& row) { return row.text != L"b"; });
        Check(table.SelectedIndex() == -1 && table.SelectedKey() == 22,
              "filtering selected row retains key without selecting another record");
        filtered->Where({});
        Check(table.SelectedIndex() >= 0 && table.SelectedKey() == 22,
              "clearing filter restores selected record");
        model->Reset({{22, L"new b"}, {11, L"a"}});
        Check(table.SourceRowAt(static_cast<size_t>(table.SelectedIndex())) == 0,
              "reset follows key across reordered data");
        std::weak_ptr<lumen::ItemsModel> weak = model;
        model.reset(); filtered.reset();
        Check(!weak.expired() && sorted.Count() == 2, "shared decorators retain source ownership");
    }
    {
        struct UiaRow { int rank; std::wstring name; };
        struct UiaTable : lumen::Table {
            using Table::AutomationCellValue;
            using Table::AutomationSetCellValue;
        };
        lumen::VectorModel<UiaRow> model;
        model.Reset({{30, L"third"}, {10, L"first"}, {20, L"second"}});
        UiaTable table;
        table.Bind(model).Column(L"Rank", &UiaRow::rank, 80.0f).CellEditEnabled();
        table.SortBy(0, 1);
        Check(table.DataRowAt(0) == 1 && table.AutomationCellValue(0, 0) == L"10",
              "UIA cell follows sorted business row");
        Check(table.AutomationSetCellValue(0, 0, L"11") && model.At(1).rank == 11 && model.At(0).rank == 30,
              "UIA edit writes sorted row identity");
        model.RemoveAt(1);
        Check(table.RowCount() == 2 && table.AutomationCellValue(0, 0) == L"20",
              "UIA mapping remains valid after business row removal");
    }
    {
        lumen::VectorModel<std::wstring> model({L"a", L"b", L"c"});
        size_t first = 99, count = 0;
        int changes = 0, resets = 0;
        auto changed = model.OnChanged([&](size_t i, size_t n) { first = i; count = n; ++changes; });
        auto reset = model.OnReset([&] { ++resets; });
        { lumen::UpdateScope update; model.At(0, L"aa"); model.At(2, L"cc"); }
        Check(changes == 1 && first == 0 && count == 3, "batch merges model changed ranges");
        { lumen::UpdateScope update; model.Push(L"d"); model.RemoveAt(0); }
        Check(resets == 1, "batch structural mutations publish one reset");
    }

    {
        struct Record { int64_t value; };
        lumen::VectorModel<Record> model({{INT64_MAX}, {9007199254740993LL}, {9007199254740992LL}});
        lumen::Table table;
        table.Bind(model).Column(L"Exact", &Record::value).CellEditEnabled();
        table.SortBy(0, 1);
        Check(table.DataRowAt(0) == 2 && table.DataRowAt(1) == 1 && table.DataRowAt(2) == 0,
              "int64 sort distinguishes adjacent values above double precision");
        int commits = 0;
        table.OnEditCommitted([&](const lumen::CellEdit& edit) {
            ++commits;
            Check(edit.before == L"9223372036854775807" && edit.after == L"-9223372036854775808",
                  "edit event records exact old and new int64 text");
        });
        Check(!table.CommitCell(0, 0, L"9223372036854775808") && model.At(0).value == INT64_MAX,
              "int64 overflow rejects without mutation");
        Check(!table.CommitCell(0, 0, L"1.5"), "integer draft rejects fractions");
        Check(table.CommitCell(0, 0, L"-9223372036854775808") && model.At(0).value == INT64_MIN && commits == 1,
              "int64 minimum commits exactly once");
    }
    {
        lumen::Table table;
        table.AddColumn(L"Optional"); table.AddColumn(L"Choice");
        table.RowCount(1).CellText([](size_t, size_t, std::wstring& out) { out.clear(); });
        std::optional<double> value;
        std::wstring choice = L"A";
        table.BindNullableNumber(0, [&](size_t) { return value; }, [&](size_t, std::optional<double> next) { value = next; });
        table.BindChoice(1, {L"A", L"B"}, [&](size_t) { return choice; }, [&](size_t, std::wstring next) { choice = next; });
        Check(table.CommitCell(0, 0, L"12.5") && value == 12.5, "nullable table number commits finite value");
        Check(table.CommitCell(0, 0, L"") && !value, "nullable table number commits empty as null");
        Check(!table.CommitCell(0, 0, L"nan") && !value, "nullable table number rejects nonfinite draft");
        Check(!table.CommitCell(0, 1, L"C") && choice == L"A", "choice editor rejects unknown value");
        Check(table.CommitCell(0, 1, L"B") && choice == L"B", "choice editor commits listed value");
        table.ValidateCell([](const lumen::CellEdit&) { return std::wstring(L"business rule"); });
        Check(!table.CommitCell(0, 1, L"A") && choice == L"B" && table.EditError() == L"business rule",
              "business validation blocks commit and exposes persistent error");
    }

    {
        struct EditProbe : lumen::TextBox { using TextBox::OnImeCompose; using TextBox::OnImeCommit; using TextBox::OnImeEnd; } box;
        box.Text(L"old").Select(0, 3);
        int filters = 0;
        std::wstring filtered;
        box.OnTextChanged([&](std::wstring_view value) { if (!box.Composing()) { ++filters; filtered = value; } });
        box.OnComposingChanged([&](bool composing) { if (!composing) { ++filters; filtered = box.Text(); } });
        box.OnImeCompose(L"zhong", 5, {});
        Check(box.Composing() && filters == 0, "IME selection replacement does not filter intermediate text");
        box.OnImeCommit(L"中");
        box.OnImeEnd();
        Check(!box.Composing() && filters == 1 && filtered == L"中",
              "IME completion exposes final text once after document mutation");
        const auto revision = box.Revision();
        Check(box.SyncText(L"external", revision - 1) == lumen::TextSyncResult::Conflict && box.Text() == L"中",
              "stale external text cannot overwrite newer draft");
    }
    {
        lumen::DispatchState port;
        port.target = &port;
        int wakes = 0, ran = 0;
        port.wake = [&](void*) { ++wakes; return false; };
        Check(port.Post([&] { ++ran; }) == lumen::PostResult::WakeFailed && port.queue.empty(),
              "failed wake rolls back queued callback");
        port.wake = [&](void*) { ++wakes; return true; };
        for (int i = 0; i < 10000; ++i) port.Post([&] { ++ran; });
        Check(wakes == 2 && ran == 0, "dispatcher flood merges wake and never executes inline");
        std::deque<std::function<void()>> batch;
        port.TryDrain(batch);
        for (auto& fn : batch) fn();
        Check(ran == 10000, "dispatcher drains accepted batch exactly once");
        port.Close();
        Check(port.Post({}) == lumen::PostResult::Closed, "closed dispatcher rejects empty posts too");
    }
    {
        struct Capture {
            int* copies;
            std::array<int, 128> large{};
            explicit Capture(int& n) : copies(&n) {}
            Capture(const Capture& other) : copies(other.copies), large(other.large) { ++*copies; }
            void operator()() const {}
        };
        int copies = 0;
        lumen::Signal<> signal;
        signal.Subscribe(Capture(copies));
        const int before = copies;
        for (int i = 0; i < 10000; ++i) signal.Emit();
        Check(copies == before, "signal emits large captures without copying callables");
        signal.Clear();
        int calls = 0;
        lumen::Connection second;
        signal.Subscribe([&] { ++calls; second.Disconnect(); signal.Subscribe([&] { calls += 100; }); });
        second = signal.Connect([&] { calls += 10; });
        signal.Emit();
        Check(calls == 1, "signal disconnects later slot and defers newly subscribed slot");
    }

    {
        struct Record { int first = 1, second = 2; };
        lumen::VectorModel<Record> model({{}});
        lumen::Table table;
        table.Bind(model).Column(L"A", &Record::first).Column(L"B", &Record::second);
        int notifications = 0;
        auto changed = model.OnChanged([&](size_t, size_t) {
            ++notifications;
            Check(model.At(0).first == 3 && model.At(0).second == 4, "TSV observers see complete row");
        });
        Check(!table.PasteRow(0, 0, L"3\tbad") && model.At(0).first == 1 && model.At(0).second == 2,
              "invalid TSV cell rejects entire row before mutation");
        Check(table.PasteRow(0, 0, L"3\t4") && notifications == 1, "valid TSV row coalesces model notification");
        table.ValidateRow([](const std::vector<lumen::CellEdit>&) { return std::wstring(L"cross-column error"); });
        Check(!table.PasteRow(0, 0, L"5\t6") && model.At(0).first == 3,
              "cross-column validation rejects TSV row");
    }
    {
        lumen::Table table;
        table.AddColumn(L"A").Key(L"a").Sizing(60, 100, 140, 1);
        table.AddColumn(L"B").Key(L"b").Sizing(80, 100, 1000, 3);
        Check(table.ColumnPixelWidth(0, 400) == 140 && table.ColumnPixelWidth(1, 400) == 260,
              "weighted columns redistribute width after maximum clamp");
        Check(table.ColumnsPixelWidth(100) == 140, "column minimums produce horizontal overflow");
        table.ColumnWidth(0, 125); table.ColumnFrozen(0, true); table.ColumnVisible(1, false);
        auto saved = table.CaptureColumns();
        lumen::Table restored;
        restored.AddColumn(L"B renamed").Key(L"b"); restored.AddColumn(L"A renamed").Key(L"a");
        restored.RestoreColumns(saved);
        Check(restored.ColumnWidth(1) == 125 && restored.ColumnFrozen(1) && !restored.ColumnVisible(0),
              "column settings restore by stable key after schema order changes");
    }

    {
        lumen::Property<std::optional<double>> value{std::nullopt};
        lumen::NumberBox box;
        box.BindValue(value).Range(-10, 10).Decimals(2);
        box.Text(L"3.25");
        Check(box.CommitValue() && value.Get() == std::optional<double>(3.25), "nullable number binding commits valid draft");
        box.Text(L"");
        Check(box.CommitValue() && !value.Get(), "nullable number binding commits empty as null");
        box.Text(L"bad");
        Check(!box.CommitValue() && !value.Get(), "nullable number binding preserves null on invalid draft");
        value = 4.5;
        Check(box.Text() == L"4.50", "nullable number binding accepts external value");
        lumen::Form form;
        auto& field = form.Field(L"Integer");
        auto& number = field.Add<lumen::NumberBox>();
        number.Integer().ClampOnCommit(false).Range(0, 10).Text(L"1.5");
        Check(!form.ValidateAll() && field.HasError(), "form catches number policy without duplicate validation rule");
        number.Text(L"3");
        Check(form.CommitAll() && !field.HasError(), "form commits corrected numeric draft");
        auto dying = std::make_unique<lumen::NumberBox>(1);
        dying->OnValueChanged([&](double) { dying.reset(); });
        dying->Text(L"2");
        Check(dying->CommitValue() && !dying, "numeric commit callback may destroy editor safely");
    }
    {
        lumen::Table table;
        table.AddColumn(L"A"); table.AddColumn(L"B"); table.AddColumn(L"C");
        int calls = 0;
        table.OnColumnsChanged([&] { ++calls; });
        table.ColumnFrozen(0, true); table.ColumnVisible(1, false); table.MoveColumn(2, 1);
        Check(calls == 3, "column persistence notification covers pin visibility and reorder");
    }

    {
        struct Entry { uint64_t id; std::wstring text; };
        lumen::VectorModel<Entry> source({{1, L"one"}, {2, L"two"}});
        source.Key([](const Entry& row) { return row.id; });
        source.Map([](const Entry& row, lumen::ItemRow& out) { out.text = row.text; });
        lumen::ComboBox box;
        box.Bind(source).SelectedIndex(1);
        source.Insert(0, {3, L"three"});
        Check(box.SelectedKey() == 2 && box.SelectedIndex() == 2, "combo insertion keeps selected identity");
        source.Reset({{2, L"renamed"}, {1, L"one"}});
        Check(box.SelectedKey() == 2 && box.SelectedIndex() == 0 && box.SelectedText() == L"renamed", "combo same-count reset restores stable key");
    }

    if (g_behavior_failures > 0) {
        std::printf("%d behavior check(s) failed\n", g_behavior_failures);
        return 1;
    }
    return 0;
}
