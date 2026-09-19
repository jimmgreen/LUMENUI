#include <lumen/lumen.h>
#include <cmath>
#include <cstdio>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace {
int failures = 0;
void Check(bool ok, const char* name) {
    std::printf("%s %s\n", ok ? "[PASS]" : "[FAIL]", name);
    if (!ok) ++failures;
}

struct TestRow {
    uint64_t id = 0;
    std::wstring name;
    std::wstring status;
    bool enabled = false;
    double score = 0.0;
};

struct TableProbe : lumen::Table {
    using Table::Arrange;
    using Table::GroupHeaderContentRect;
    using Table::OnMouseDown;
    using Table::OnMouseUp;
};
}

int main() {
    using namespace lumen;

    {
        Table table;
        table.RowCount(5);
        table.AddColumn(L"Name").Filterable(TableFilterKind::Text);
        table.AddColumn(L"Status").Filterable(TableFilterKind::Choice)
            .FilterChoices({L"Open", L"Closed", L"Open"});
        table.AddColumn(L"Enabled").Filterable(TableFilterKind::Boolean);
        table.AddColumn(L"Score").Filterable(TableFilterKind::NumberRange);

        Check(table.ColumnCount() == 4 && table.ColumnTitle(1) == L"Status",
              "table exposes column discovery for external filter UI");
        Check(table.ColumnFilterChoices(1) == std::vector<std::wstring>{L"Open", L"Closed"},
              "filter choices are deduplicated");

        int events = 0;
        int last_col = -1;
        TableFilterState last;
        auto connection = table.BindFilterChanged([&](int col, TableFilterState state) {
            ++events;
            last_col = col;
            last = std::move(state);
        });

        TableFilterState text;
        text.text = L"pulse";
        text.minimum = 1.0;
        text.values = {L"ignored"};
        table.ColumnFilter(0, text);
        Check(events == 1 && last_col == 0 && last.text == L"pulse" && !last.minimum && last.values.empty(),
              "text filter normalizes unrelated fields");
        table.ColumnFilter(0, text);
        Check(events == 1, "same normalized filter state is a no-op");

        TableFilterState choice;
        choice.values = {L"Open", L"Bogus", L"Open", L"Closed"};
        table.ColumnFilter(1, choice);
        Check(table.ColumnFilter(1).values == std::vector<std::wstring>{L"Open", L"Closed"},
              "choice filter removes invalid and duplicate values");

        TableFilterState boolean;
        boolean.boolean = false;
        boolean.text = L"ignored";
        table.ColumnFilter(2, boolean);
        Check(table.ColumnFilter(2).boolean == std::optional<bool>(false) && table.ColumnFilter(2).text.empty(),
              "boolean filter keeps explicit false");

        TableFilterState range;
        range.minimum = 20.0;
        range.maximum = 10.0;
        table.ColumnFilter(3, range);
        Check(table.ColumnFilter(3).minimum == std::optional<double>(10.0) &&
                  table.ColumnFilter(3).maximum == std::optional<double>(20.0),
              "number range normalizes reversed bounds");
        range.minimum = std::numeric_limits<double>::infinity();
        range.maximum.reset();
        table.ColumnFilter(3, range);
        Check(table.ColumnFilter(3).Empty(), "number range drops nonfinite bounds");

        Check(table.ActiveFilterCount() == 3, "active filter count excludes empty range");
        const size_t rows_before = table.RowCount();
        const size_t data_before = table.DataRowAt(3);
        table.ClearFilter(1);
        Check(table.RowCount() == rows_before && table.DataRowAt(3) == data_before,
              "table filter state never filters or reorders rows itself");
        table.ClearFilters();
        Check(table.ActiveFilterCount() == 0, "clear filters removes all active states");

        TableFilterState active;
        active.text = L"x";
        table.ColumnFilter(0, active);
        const int before_none = events;
        table.ColumnFilterKind(0, TableFilterKind::None);
        Check(table.ColumnFilter(0).Empty() && events == before_none + 1,
              "disabling filter kind clears active state and notifies once");
        (void)connection;
    }

    {
        TableProbe table;
        table.AddColumn(L"#", 64.0f).Frozen();
        table.AddColumn(L"Environment", 120.0f);
        table.Arrange({10.0f, 20.0f, 140.0f, 120.0f});
        const float frozen = table.ColumnPixelWidth(0);
        const Rect content = table.GroupHeaderContentRect(52.0f, frozen);
        Check(std::fabs(content.x - (10.0f + frozen)) < 0.01f &&
                  std::fabs(content.Right() - 150.0f) < 0.01f,
              "group header content starts after frozen separator and stays inside viewport");
        table.Arrange({10.0f, 20.0f, 40.0f, 120.0f});
        const Rect narrow = table.GroupHeaderContentRect(52.0f, table.ColumnPixelWidth(0));
        Check(std::fabs(narrow.x - (10.0f + table.ColumnPixelWidth(0))) < 0.01f && narrow.w == 0.0f,
              "group header content collapses instead of crossing separator at narrow width");
    }

    {
        VectorModel<TestRow> source;
        source.Key([](const TestRow& row) { return row.id; });
        source.Map([](const TestRow& row, ItemRow& out) {
            out.text = row.name;
            out.cells = {row.name, row.status, row.enabled ? L"true" : L"false", std::to_wstring(row.score)};
        });
        for (uint64_t i = 1; i <= 10; ++i)
            source.Push({i, L"Row " + std::to_wstring(i), i % 2 ? L"Open" : L"Closed", (i % 3) == 0,
                         static_cast<double>(i) * 10.0});

        PagedModel paged(source, 3);
        int page_events = 0;
        size_t event_page = 0, event_count = 0;
        auto page_connection = paged.BindPageChanged([&](size_t page, size_t count) {
            ++page_events;
            event_page = page;
            event_count = count;
        });

        Check(paged.PageCount() == 4 && paged.Current() == 1 && paged.Count() == 3 &&
                  paged.BeginIndex() == 0 && paged.EndIndex() == 3 && paged.TotalCount() == 10,
              "paged model reports first page geometry");
        paged.Current(2);
        ItemRow row;
        paged.Get(0, row);
        Check(page_events == 1 && event_page == 2 && event_count == 4 && row.text == L"Row 4" &&
                  paged.SourceIndex(0) == 3 && paged.RowKey(0) == 4,
              "paged model maps get source index and stable key through offset");
        paged.Current(2);
        Check(page_events == 1, "setting current page to same value is a no-op");
        paged.Current(4);
        Check(paged.Count() == 1 && paged.BeginIndex() == 9 && paged.EndIndex() == 10,
              "last page exposes partial range");
        source.Reset({{1, L"one", L"Open", true, 1.0}, {2, L"two", L"Closed", false, 2.0},
                      {3, L"three", L"Open", true, 3.0}, {4, L"four", L"Closed", false, 4.0}});
        Check(paged.PageCount() == 2 && paged.Current() == 2 && paged.Count() == 1 &&
                  page_events == 3 && event_page == 2 && event_count == 2,
              "source shrink clamps current page and reports page-count change");
        paged.PageSize(10);
        Check(paged.PageCount() == 1 && paged.Current() == 1 && paged.Count() == 4,
              "page size change clamps page");
        (void)page_connection;
    }

    {
        auto source = std::make_shared<VectorModel<TestRow>>();
        source->Key([](const TestRow& row) { return row.id; });
        source->Map([](const TestRow& row, ItemRow& out) { out.text = row.name; });
        source->Reset({{11, L"alpha"}, {22, L"beta"}, {33, L"gamma"}, {44, L"delta"}, {55, L"epsilon"}});
        auto filtered = std::make_shared<FilteredModel>(source, [](size_t, const ItemRow& row) {
            return row.text.find(L'a') != std::wstring::npos;
        });
        PagedModel paged(filtered, 2);
        Check(paged.Count() == 2 && paged.SourceIndex(0) == 0 && paged.SourceIndex(1) == 1 &&
                  paged.RowKey(1) == 22,
              "filtered to paged chain preserves original source mapping and keys");
        paged.Current(2);
        Check(paged.Count() == 2 && paged.SourceIndex(0) == 2 && paged.SourceIndex(1) == 3,
              "later filtered page maps through decorator chain");
    }

    {
        auto source = std::make_unique<VectorModel<std::wstring>>();
        source->Reset({L"a", L"b", L"c"});
        PagedModel paged(*source, 2);
        paged.Current(2);
        int events = 0;
        paged.OnPageChanged([&](size_t, size_t) { ++events; });
        source.reset();
        ItemRow row;
        row.text = L"stale";
        paged.Get(0, row);
        Check(paged.Count() == 0 && paged.TotalCount() == 0 && paged.PageCount() == 1 &&
                  paged.Current() == 1 && row.text.empty() && events == 1,
              "paged model safely empties and clamps when borrowed source is destroyed");
    }

    {
        // 与 Gallery 任务表相同的 ItemRow 模型；覆盖表头点击，而非绕过开关直接 SortBy。
        VectorModel<ItemRow> tasks;
        tasks.Map([](const ItemRow& item, ItemRow& row) { row = item; });
        const std::vector<ItemRow> all{
            {L"Review", L"", {L"Review", L"In progress", L"Alex"}},
            {L"Verify", L"", {L"Verify", L"To do", L"Morgan"}},
            {L"Prepare", L"", {L"Prepare", L"Done", L"Sam"}}};
        tasks.Reset(all);
        TableProbe table;
        table.Bind(tasks);
        table.AddColumn(L"Task", 180.0f).Sortable(true);
        table.AddColumn(L"Status", 140.0f).Sortable(true);
        table.AddColumn(L"Owner", 120.0f).Sortable(true);
        table.Arrange({0.0f, 0.0f, 440.0f, 200.0f});
        const auto click = [&](float x) {
            table.OnMouseDown({x, 12.0f}, 1);
            table.OnMouseUp({x, 12.0f}, 1);
        };
        Check(table.Sortable(0) && table.Sortable(1) && table.Sortable(2),
              "task table enables all three sortable headers");
        click(40.0f);
        Check(table.SortedColumn() == 0 && table.SortDirection() == 1 &&
                  table.DataRowAt(0) == 2 && table.DataRowAt(2) == 1,
              "task header click sorts ascending");
        click(40.0f);
        Check(table.SortDirection() == -1 && table.DataRowAt(0) == 1,
              "second task header click sorts descending");
        click(40.0f);
        Check(table.SortedColumn() == -1 && table.DataRowAt(0) == 0,
              "third task header click restores source order");
        click(220.0f);
        Check(table.SortedColumn() == 1 && table.DataRowAt(0) == 2,
              "status header sorts its own column");
        click(360.0f);
        Check(table.SortedColumn() == 2 && table.DataRowAt(0) == 0 && table.DataRowAt(2) == 2,
              "owner header sorts its own column");
        click(40.0f);
        tasks.Reset({all[1], all[2]});
        Check(table.RowCount() == 2 && table.SortedColumn() == 0 && table.DataRowAt(0) == 1,
              "filtered task reset retains sorting and remaps rows");
        tasks.Reset({});
        Check(table.RowCount() == 0 && table.SortedColumn() == 0,
              "empty task results retain the selected sort");
        tasks.Reset(all);
        Check(table.RowCount() == 3 && table.DataRowAt(0) == 2,
              "clearing task filter restores sorted results");
        tasks.Push({L"New task 4", L"", {L"New task 4", L"To do", L"You"}});
        Check(table.RowCount() == 4 && table.DataRowAt(0) == 3,
              "new task enters its sorted position");
    }

    return failures == 0 ? 0 : 1;
}
