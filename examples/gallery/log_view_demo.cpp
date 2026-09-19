#include "common.h"
#include <array>
#include <cwchar>
#include <memory>
#include <vector>

namespace gallery {
namespace {
constexpr const wchar_t* kLevelNames[]{L"DEBUG", L"INFO", L"WARN", L"ERROR"};

struct LogDemo {
    std::vector<lumen::LogEntry> rows;
    lumen::LogView* view = nullptr;
    lumen::Label* count = nullptr;
    lumen::ToggleButton* follow = nullptr;
    std::array<lumen::ToggleButton*, 4> levels{};
    size_t next = 0;

    void Append(size_t count_to_add) {
        for (size_t i = 0; i < count_to_add; ++i) {
            const size_t id = next++;
            lumen::LogEntry entry;
            wchar_t timestamp[32]{};
            std::swprintf(timestamp, std::size(timestamp), L"17:%02u:%02u.%03u",
                          static_cast<unsigned>((id / 60) % 60), static_cast<unsigned>(id % 60),
                          static_cast<unsigned>((id * 137) % 1000));
            entry.timestamp = timestamp;
            entry.level = id % 11 == 0 ? lumen::LogLevel::Error : id % 7 == 0 ? lumen::LogLevel::Warn :
                          id % 4 == 0 ? lumen::LogLevel::Debug : lumen::LogLevel::Info;
            constexpr const wchar_t* sources[]{L"worker", L"orderbook", L"auth", L"ingest", L"api"};
            entry.source = sources[id % std::size(sources)];
            switch (entry.level) {
            case lumen::LogLevel::Error: entry.message = L"Retrying upstream call attempt=" + std::to_wstring(id); break;
            case lumen::LogLevel::Warn: entry.message = L"Disk usage 87% on /data"; break;
            case lumen::LogLevel::Debug: entry.message = L"Slow query detected duration=" + std::to_wstring(30 + id) + L"ms"; break;
            default: entry.message = L"Order matched id=" + std::to_wstring(2900 + id) + L" px=341.50"; break;
            }
            if (id % 5 == 0) entry.trace = L"trace=" + std::to_wstring(10000 + id);
            rows.push_back(std::move(entry));
        }
        // Keep the interactive sample bounded during repeated append operations.
        if (rows.size() > 10000) {
            rows.erase(rows.begin(), rows.begin() + static_cast<ptrdiff_t>(rows.size() - 10000));
            if (view) view->ItemCount(rows.size()).Refresh();
        } else if (view) view->ItemCount(rows.size());
    }

    void RefreshToolbar() {
        if (!view || !count) return;
        count->Text(std::to_wstring(view->VisibleCount()) + L" / " + std::to_wstring(rows.size()) + L" lines");
        for (size_t i = 0; i < levels.size(); ++i) {
            if (!levels[i]) continue;
            const auto level = static_cast<lumen::LogLevel>(i);
            levels[i]->Text(std::wstring(kLevelNames[i]) + L" " + std::to_wstring(view->LevelCount(level)))
                .Checked(view->LevelEnabled(level));
        }
        if (follow) follow->Text(view->Following() ? L"Following" : L"Paused").Checked(view->Following());
    }
};
}

void BuildLogViewDemo(lumen::StackPanel& column, lumen::Window& window) {
    using namespace lumen;
    auto state = std::make_shared<LogDemo>();
    state->Append(120);
    auto& card = Sample(column, L"LogView", L"Search, level filters and follow-tail. Scroll up to pause; End resumes. Ctrl+C copies the complete selected entry.");
    auto& toolbar = card.Add<WrapPanel>().Gap(8.0f).FillCross();
    auto& search = toolbar.Add<TextBox>().Placeholder(L"Filter logs...").AccessibleName(L"Search log entries")
        .MinSize({200.0f, 0.0f}).MaxSize({200.0f, 0.0f});
    for (size_t i = 0; i < state->levels.size(); ++i)
        state->levels[i] = &toolbar.Add<ToggleButton>(kLevelNames[i]).Pill(true).SizeClass(ButtonSize::Small).Checked(true);
    state->count = &toolbar.Add<Label>(L"", TextRole::Caption).Secondary(true);
    state->follow = &toolbar.Add<ToggleButton>(L"Following").SizeClass(ButtonSize::Small).Checked(true);
    state->view = &card.Add<LogView>().FillCross().MinSize({0.0f, 312.0f}).MaxSize({0.0f, 312.0f})
        .AccessibleName(L"Application log");
    LogDemo* raw = state.get();
    state->view->Entry([raw](size_t index, LogEntry& out) {
        if (index < raw->rows.size()) out = raw->rows[index];
    }).ItemCount(state->rows.size()).Follow(true);
    state->view->OnViewChanged([raw] { raw->RefreshToolbar(); })
        .OnFollowingChanged([raw](bool) { raw->RefreshToolbar(); });
    search.OnTextChanged([state](std::wstring_view text) { state->view->Query(text); });
    for (size_t i = 0; i < state->levels.size(); ++i)
        state->levels[i]->OnToggled([state, i](bool enabled) { state->view->LevelEnabled(static_cast<LogLevel>(i), enabled); });
    state->follow->OnToggled([state](bool on) {
        state->view->Follow(on);
        state->RefreshToolbar();
    });
    auto& actions = card.Add<Row>().Spacing(8.0f).FillCross();
    actions.Add<Button>(L"Append 20 entries", ButtonKind::Subtle).OnClick([state] { state->Append(20); });
    actions.Add<Button>(L"Copy selected", ButtonKind::Subtle).OnClick([state, &window] {
        window.ShowToast(state->view->CopySelection() ? L"Log entry copied" : L"Select a log entry first");
    });
    actions.Add<Button>(L"Clear", ButtonKind::Subtle).OnClick([state] {
        state->rows.clear();
        state->view->ItemCount(0);
    });
    state->RefreshToolbar();
}

} // namespace gallery
