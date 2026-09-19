#include "common.h"
#include <algorithm>
#include <cwctype>
#include <memory>
#include <string>
#include <vector>

namespace gallery {
namespace {

struct FilterPagingRow {
    uint64_t id = 0;
    std::wstring task;
    std::wstring status;
    std::wstring owner;
    bool enabled = false;
};

std::wstring Lower(std::wstring_view value) {
    std::wstring out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(static_cast<wint_t>(ch)));
    });
    return out;
}

struct TableFilterPagingState {
    lumen::VectorModel<FilterPagingRow> source;
    lumen::FilteredModel filtered;
    lumen::PagedModel paged;
    lumen::Flyout filters;
    lumen::Table* table = nullptr;
    lumen::TextBox* search = nullptr;
    lumen::TextBox* owner_filter = nullptr;
    lumen::ComboBox* status_filter = nullptr;
    lumen::ComboBox* enabled_filter = nullptr;
    lumen::WrapPanel* chips = nullptr;
    lumen::Pagination* pager = nullptr;
    lumen::Label* range = nullptr;
    lumen::EmptyState* empty = nullptr;

    TableFilterPagingState() : filtered(source), paged(filtered, 5) {
        source.Key([](const FilterPagingRow& row) { return row.id; });
        source.Map([](const FilterPagingRow& row, lumen::ItemRow& out) {
            out.text = row.task;
            out.cells = {row.task, row.status, row.owner, row.enabled ? L"Enabled" : L"Disabled"};
        });
        source.Reset({
            {1, L"Review project settings", L"Open", L"Alex", true},
            {2, L"Verify keyboard navigation", L"Open", L"Morgan", true},
            {3, L"Prepare release notes", L"Closed", L"Sam", false},
            {4, L"Audit deployment policy", L"Open", L"Alex", true},
            {5, L"Refresh screenshots", L"Closed", L"Morgan", false},
            {6, L"Triage accessibility report", L"Open", L"Sam", true},
            {7, L"Check package manifest", L"Closed", L"Alex", true},
            {8, L"Validate upgrade path", L"Open", L"Morgan", false},
            {9, L"Review telemetry copy", L"Closed", L"Sam", true},
            {10, L"Confirm signing pipeline", L"Open", L"Alex", true},
            {11, L"Archive old snapshots", L"Closed", L"Morgan", false},
            {12, L"Publish migration notes", L"Open", L"Sam", true},
        });
    }

    void RefreshStatus() {
        if (pager) pager->PageCount(paged.PageCount()).Current(paged.Current());
        const size_t total = paged.TotalCount();
        if (range) {
            if (total == 0) range->Text(L"0 of 0");
            else range->Text(std::to_wstring(paged.BeginIndex() + 1) + L"–" +
                             std::to_wstring(paged.EndIndex()) + L" of " + std::to_wstring(total));
        }
        if (table) table->Visible(total != 0);
        if (empty) empty->Visible(total == 0);
    }

    void RefreshChips() {
        if (!chips || !table) return;
        chips->Clear();
        auto add = [&](int col, std::wstring text) {
            chips->Add<lumen::Chip>(text).Closable(true).OnClosed([this, col] {
                if (col == 2 && owner_filter) owner_filter->Text(L"");
                if (col == 1 && status_filter) status_filter->SelectedIndex(0);
                if (col == 3 && enabled_filter) enabled_filter->SelectedIndex(0);
                table->ClearFilter(col);
            });
        };
        const auto& owner = table->ColumnFilter(2);
        if (!owner.Empty()) add(2, L"Owner: " + owner.text);
        const auto& status = table->ColumnFilter(1);
        if (!status.values.empty()) add(1, L"Status: " + status.values.front());
        const auto& enabled = table->ColumnFilter(3);
        if (enabled.boolean) add(3, std::wstring(L"Enabled: ") + (*enabled.boolean ? L"Yes" : L"No"));
    }

    void ApplyFilters(bool reset_page) {
        if (!table) return;
        const std::wstring query = search ? Lower(search->Text()) : std::wstring{};
        const auto owner_state = table->ColumnFilter(2);
        const auto status_state = table->ColumnFilter(1);
        const auto enabled_state = table->ColumnFilter(3);
        filtered.Where([this, query, owner_state, status_state, enabled_state](size_t index,
                                                                               const lumen::ItemRow&) {
            if (index >= source.Count()) return false;
            const auto& row = source.At(index);
            if (!query.empty()) {
                const std::wstring haystack = Lower(row.task + L" " + row.status + L" " + row.owner);
                if (haystack.find(query) == std::wstring::npos) return false;
            }
            if (!owner_state.text.empty() &&
                Lower(row.owner).find(Lower(owner_state.text)) == std::wstring::npos)
                return false;
            if (!status_state.values.empty() &&
                std::find(status_state.values.begin(), status_state.values.end(), row.status) ==
                    status_state.values.end())
                return false;
            if (enabled_state.boolean && row.enabled != *enabled_state.boolean) return false;
            return true;
        });
        if (reset_page) paged.Current(1);
        RefreshChips();
        RefreshStatus();
    }
};

} // namespace

void BuildTableFilterPagingDemo(lumen::StackPanel& column, lumen::Window& window) {
    using namespace lumen;
    auto state = std::make_shared<TableFilterPagingState>();
    auto& card = Sample(
        column, L"Table · filters & paging",
        L"Search + column filters are external composition. FilteredModel projects rows; PagedModel windows the result.");

    auto& tools = card.Add<Row>().Spacing(8.0f).AlignCross(Cross::Center).FillCross();
    state->search = &tools.Add<TextBox>().Placeholder(L"Search task, status, owner").AccessibleName(L"Search table").Grow();
    auto& filter_button = tools.Add<Button>(L"Filters", ButtonKind::Subtle).Glyph(icon::kFilter);
    state->range = &tools.Add<Label>(L"", TextRole::Caption).Secondary(true);

    state->chips = &card.Add<WrapPanel>().Gap(6.0f);
    state->table = &card.Add<Table>().FillCross().MinSize({0.0f, 220.0f}).MaxSize({0.0f, 220.0f}).RowHeight(36.0f);
    state->table->Bind(state->paged);
    state->table->AddColumn(L"Task");
    state->table->AddColumn(L"Status", 120.0f)
        .Filterable(TableFilterKind::Choice)
        .FilterChoices({L"Open", L"Closed"});
    state->table->AddColumn(L"Owner", 120.0f).Filterable(TableFilterKind::Text);
    state->table->AddColumn(L"Enabled", 100.0f).Filterable(TableFilterKind::Boolean);

    state->empty = &card.Add<EmptyState>();
    state->empty->Title(L"No matching rows").Hint(L"Clear search or remove a filter.").Visible(false);

    auto& footer = card.Add<Row>().Spacing(10.0f).AlignCross(Cross::Center).FillCross();
    state->pager = &footer.Add<Pagination>();
    footer.Add<Label>(L"Paging is optional; virtualized tables can also show the full local model.", TextRole::Caption)
        .Secondary(true).Grow();

    state->filters.FlyoutWidth(300.0f).Placement(FlyoutPlacement::Below).Spacing(8.0f);
    state->filters.Add<Label>(L"Column filters", TextRole::BodyStrong);
    state->filters.Add<Label>(L"Owner contains", TextRole::Caption).Secondary(true);
    state->owner_filter = &state->filters.Add<TextBox>().Placeholder(L"e.g. Alex");
    state->filters.Add<Label>(L"Status", TextRole::Caption).Secondary(true);
    state->status_filter = &state->filters.Add<ComboBox>();
    state->status_filter->AddItems({L"Any", L"Open", L"Closed"}).SelectedIndex(0);
    state->filters.Add<Label>(L"Enabled", TextRole::Caption).Secondary(true);
    state->enabled_filter = &state->filters.Add<ComboBox>();
    state->enabled_filter->AddItems({L"Any", L"Yes", L"No"}).SelectedIndex(0);

    TableFilterPagingState* raw = state.get();
    filter_button.OnClick([state, &window, &filter_button] {
        window.ShowFlyout(state->filters, &filter_button);
    });
    state->search->OnTextChanged([state](std::wstring_view) { state->ApplyFilters(true); });
    state->table->OnFilterChanged([state](int, TableFilterState) { state->ApplyFilters(true); });
    state->owner_filter->OnTextChanged([raw](std::wstring_view text) {
        TableFilterState filter;
        filter.text = text;
        raw->table->ColumnFilter(2, std::move(filter));
    });
    state->status_filter->OnSelectionChanged([raw](ptrdiff_t index, ptrdiff_t) {
        TableFilterState filter;
        if (index == 1) filter.values = {L"Open"};
        else if (index == 2) filter.values = {L"Closed"};
        raw->table->ColumnFilter(1, std::move(filter));
    });
    state->enabled_filter->OnSelectionChanged([raw](ptrdiff_t index, ptrdiff_t) {
        TableFilterState filter;
        if (index == 1) filter.boolean = true;
        else if (index == 2) filter.boolean = false;
        raw->table->ColumnFilter(3, std::move(filter));
    });
    state->pager->OnNavigate([state](size_t page) {
        state->paged.Current(page);
        state->RefreshStatus();
    });
    state->paged.OnPageChanged([raw](size_t, size_t) { raw->RefreshStatus(); });
    state->empty->Action(L"Clear filters", [state] {
        if (state->search) state->search->Text(L"");
        if (state->owner_filter) state->owner_filter->Text(L"");
        if (state->status_filter) state->status_filter->SelectedIndex(0);
        if (state->enabled_filter) state->enabled_filter->SelectedIndex(0);
        state->table->ClearFilters();
        state->ApplyFilters(true);
    });

    state->ApplyFilters(true);
}

} // namespace gallery
