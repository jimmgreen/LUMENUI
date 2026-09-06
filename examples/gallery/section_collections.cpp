#include "common.h"
#include <algorithm>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace gallery {
namespace {
lumen::Flyout g_mail_flyout;
lumen::Label* g_mail_title = nullptr;
lumen::Label* g_mail_body = nullptr;

struct InboxDemo {
    lumen::VectorModel<lumen::ItemData> items;
    lumen::Label* count = nullptr;
    InboxDemo() {
        items.Reset({
            {L"Standup notes", std::wstring(lumen::icon::kMail)},
            {L"Build pulse", std::wstring(lumen::icon::kBell)},
            {L"Design review", std::wstring(lumen::icon::kPin)},
            {L"Ship checklist", std::wstring(lumen::icon::kCheckMark)},
            {L"On-call rotation", std::wstring(lumen::icon::kClock)},
            {L"Release notes", std::wstring(lumen::icon::kFile)},
            {L"Bug triage", std::wstring(lumen::icon::kBug)},
            {L"Weekly digest", std::wstring(lumen::icon::kMail)},
        });
    }
    void RefreshCount() {
        if (!count) return;
        count->Text(std::to_wstring(items.Count()) + L" threads");
    }
};
InboxDemo g_inbox;

struct FilterDemo {
    std::wstring query;
    bool alpha = false;
    lumen::VectorModel<std::wstring> names;
    lumen::FilteredModel filtered;
    lumen::SortedModel sorted;
    lumen::Label* tally = nullptr;
    FilterDemo()
        : filtered(names,
                   [this](size_t, const lumen::ItemRow& row) {
                       if (query.empty()) return true;
                       return row.text.find(query) != std::wstring::npos;
                   }),
          sorted(filtered) {
        names.Reset({L"Aurora", L"Beacon", L"Cascade", L"Drift", L"Ember", L"Flux", L"Grove",
                     L"Harbor"});
    }
    void ApplySort() {
        if (!alpha) {
            sorted.ClearOrder();
            return;
        }
        sorted.OrderBy([this](size_t a, size_t b) {
            lumen::ItemRow ra, rb;
            filtered.Get(a, ra);
            filtered.Get(b, rb);
            return ra.text < rb.text;
        });
    }
    void RefreshTally() {
        if (tally) tally->Text(std::to_wstring(sorted.Count()) + L" matches");
    }
    void SetQuery(std::wstring value) {
        query = std::move(value);
        filtered.Where([this](size_t, const lumen::ItemRow& row) {
            if (query.empty()) return true;
            return row.text.find(query) != std::wstring::npos;
        });
        RefreshTally();
    }
};
FilterDemo g_filter;

struct PlotRow {
    bool checked = true;
    std::wstring title;
    std::wstring paper;
    std::wstring out_name;
    std::wstring status;
    float progress = 0.0f;
};

struct LiveRow {
    std::wstring name;
    double value = 0.0;
    int count = 0;
};

struct PlotLiveDemo {
    lumen::VectorModel<PlotRow> model;
    std::vector<PlotRow> rows;
    lumen::Connection frame;
    lumen::Label* status = nullptr;
    bool running = false;
    float acc = 0.0f;
    int current = 0;

    void SyncModel() {
        model.Reset(rows);
        if (!status) return;
        if (running && current < static_cast<int>(rows.size())) {
            status->Text(L"Printing " + std::to_wstring(current + 1) + L"/" +
                         std::to_wstring(rows.size()));
            return;
        }
        status->Text(running ? L"Printing" : L"Idle");
    }

    bool Tick(float dt) {
        if (!running) return false;
        if (dt > 0.25f) dt = 0.016f;
        acc += dt;
        if (acc < 0.12f) return true;
        acc = 0.0f;
        if (current >= static_cast<int>(rows.size())) {
            running = false;
            if (status) status->Text(L"Done");
            return false;
        }
        PlotRow& row = rows[static_cast<size_t>(current)];
        row.status = L"Printing";
        row.progress = std::min(1.0f, row.progress + 0.2f);
        if (row.progress >= 0.999f) {
            row.status = L"Done";
            row.progress = 1.0f;
            ++current;
        }
        SyncModel();
        return true;
    }

    void Start(lumen::Window& window) {
        current = 0;
        acc = 0.0f;
        for (PlotRow& row : rows) {
            row.progress = 0.0f;
            row.status = L"Queued";
        }
        running = true;
        SyncModel();
        frame.Disconnect();
        frame = window.OnFrame([this](float dt) { return Tick(dt); });
    }
};
PlotLiveDemo g_plot_live;
} // namespace

void StartPlotLiveDemo(lumen::Window& window) { g_plot_live.Start(window); }

void BuildCollections(lumen::StackPanel& column, lumen::Window& window) {
    using namespace lumen;
    PageHead(column, L"Collections",
             L"Lots of data: lists, grids, tables, trees, pages, carousels.");

    if (g_mail_flyout.ChildCount() == 0) {
        g_mail_flyout.FlyoutWidth(280.0f).Placement(FlyoutPlacement::Below);
        g_mail_title = &g_mail_flyout.Add<Label>(L"Message", TextRole::BodyStrong);
        g_mail_body = &g_mail_flyout.Add<Label>(L"Body", TextRole::Caption);
        g_mail_body->Secondary(true).Wrap(true);
    }

    auto& list = Sample(column, L"ListView",
                        L"Grouped sticky headers, Ctrl/Shift multi-select, in-group reorder, swipe.");
    auto& inbox_head = list.Add<Row>().AlignCross(Cross::Center);
    inbox_head.Add<Label>(L"12 threads", TextRole::Caption).Secondary(true);
    inbox_head.Add<Spacer>();
    auto& inbox_sel = inbox_head.Add<Label>(L"0 selected", TextRole::Caption).Secondary(true);
    auto& inbox = Wide(list).Add<ListView>();
    inbox.Grow().Groups({{L"today", L"Today", 5, true},
                     {L"yesterday", L"Yesterday", 4, true},
                     {L"older", L"Earlier", 3, false}})
        .MultiSelect(true)
        .CanReorder(true)
        .ItemText([](size_t i, std::wstring& s) {
            s = L"Re: Pulse build #" + std::to_wstring(i + 1);
        })
        .ItemGlyph([](size_t i, std::wstring& s) { s = (i % 2) == 0 ? icon::kMail : icon::kBell; })
        .SwipeLeading({L"Pin", icon::kPin, [&window, &inbox](size_t view) {
            window.ShowToast(L"Pinned #" + std::to_wstring(inbox.DataIndex(view) + 1));
        }})
        .SwipeTrailing({L"Archive", icon::kFolder, [&window, &inbox](size_t view) {
            window.ShowToast(L"Archived #" + std::to_wstring(inbox.DataIndex(view) + 1));
        }})
        .OnReordered([&window](size_t from, size_t to) {
            window.ShowToast(L"Moved " + std::to_wstring(from + 1) + L" → " +
                             std::to_wstring(to + 1));
        })
        .OnSelectionChanged([&inbox, &inbox_sel](ptrdiff_t, ptrdiff_t) {
            inbox_sel.Text(std::to_wstring(inbox.SelectionCount()) + L" selected");
        })
        .OnActivate([&window, &inbox](size_t) {
            const ptrdiff_t i = inbox.SelectedDataIndex();
            if (g_mail_title && i >= 0) {
                g_mail_title->Text(L"Re: Pulse build #" + std::to_wstring(i + 1));
            }
            if (g_mail_body) {
                g_mail_body->Text(L"Activate opens a Flyout. Dedicated Flyout samples are on Overlays.");
            }
            window.ShowFlyout(g_mail_flyout, &inbox);
        });

    auto& archive_card =
        Sample(column, L"ListView · virtualized", L"ItemCount(100000). Ctrl / Shift / Ctrl+A.");
    auto& archive_sel = archive_card.Add<Label>(L"0 selected", TextRole::Caption).Secondary(true);
    auto& archive = Wide(archive_card).Add<ListView>();
    archive.Grow().ItemCount(100000)
        .ItemText([](size_t i, std::wstring& s) { s = L"Archived pulse #" + std::to_wstring(i + 1); })
        .ItemGlyph([](size_t i, std::wstring& s) { s = (i % 3) == 0 ? icon::kFolder : icon::kFile; })
        .MultiSelect(true)
        .SelectedIndex(2)
        .OnSelectionChanged([&archive, &archive_sel](ptrdiff_t, ptrdiff_t) {
            archive_sel.Text(std::to_wstring(archive.SelectionCount()) + L" selected");
        })
        .OnActivate([&window, &archive](size_t) {
            const ptrdiff_t i = archive.SelectedDataIndex();
            if (g_mail_title && i >= 0) {
                g_mail_title->Text(L"Archived pulse #" + std::to_wstring(i + 1));
            }
            if (g_mail_body) g_mail_body->Text(L"Virtualized row activated.");
            window.ShowFlyout(g_mail_flyout, &archive);
        });

    auto& live_card = Sample(
        column, L"ListView · ItemsModel",
        L"VectorModel Bind: Add inserts with row tween. Swipe left to delete. EmptyState when cleared.");
    auto& live_head = live_card.Add<Row>().AlignCross(Cross::Center);
    g_inbox.count = &live_head.Add<Label>(L"8 threads", TextRole::Caption).Secondary(true);
    g_inbox.RefreshCount();
    live_head.Add<Spacer>();
    auto& live_add = live_head.Add<Button>(L"Add", ButtonKind::Subtle).SizeClass(ButtonSize::Small);
    auto& live = Wide(live_card).Add<ListView>();
    auto insert_live = [&] {
        g_inbox.items.Insert(0, {L"New thread #" + std::to_wstring(g_inbox.items.Count() + 1),
                                 std::wstring(icon::kMail)});
        g_inbox.RefreshCount();
    };
    live.Grow()
        .Bind(g_inbox.items)
        .MultiSelect(true)
        .CanReorder(true)
        .EmptyTitle(L"Inbox empty")
        .EmptyHint(L"Add a thread, or swipe remaining items away.")
        .EmptyGlyph(icon::kMail)
        .EmptyAction(L"Add thread", insert_live)
        .SwipeLeading({L"Pin", icon::kPin, [&window, &live](size_t view) {
            window.ShowToast(L"Pinned " + std::to_wstring(live.DataIndex(view) + 1));
        }})
        .SwipeTrailing({L"Delete", icon::kDelete, [&live](size_t view) {
            const size_t data = live.DataIndex(view);
            live.AnimateRemoved(view, [data] {
                g_inbox.items.RemoveAt(data);
                g_inbox.RefreshCount();
            });
        }})
        .OnActivate([&window, &live](size_t) {
            const ptrdiff_t i = live.SelectedDataIndex();
            if (i < 0 || static_cast<size_t>(i) >= g_inbox.items.Count()) return;
            window.ShowToast(g_inbox.items.At(static_cast<size_t>(i)).text);
        });
    live_add.OnClick(insert_live);

    auto& model_card = Sample(
        column, L"ItemsModel · filter & sort",
        L"FilteredModel + SortedModel wrap VectorModel. Type to filter; switch sorts A–Z. Table Bind uses cells.");
    auto& model_head = model_card.Add<Row>().Spacing(8.0f).AlignCross(Cross::Center);
    auto& filter_box = model_head.Add<TextBox>();
    filter_box.Placeholder(L"Filter names").Grow().Text(g_filter.query);
    g_filter.tally = &model_head.Add<Label>(L"8 matches", TextRole::Caption).Secondary(true);
    g_filter.RefreshTally();
    auto& az = model_head.Add<Switch>(L"A–Z");
    az.Checked(g_filter.alpha);
    az.OnToggled([](bool on) {
        g_filter.alpha = on;
        g_filter.ApplySort();
        g_filter.RefreshTally();
    });
    auto& model_list = Wide(model_card).Add<ListView>();
    model_list.Grow().Bind(g_filter.sorted);
    filter_box.OnTextChanged([&filter_box](std::wstring_view) { g_filter.SetQuery(filter_box.Text()); });
    auto& model_table = model_card.Add<Table>();
    model_table.RowHeight(32.0f);
    static VectorModel<ItemData> people;
    if (people.Count() == 0) {
        people.Map([](const ItemData& d, ItemRow& row) {
            row.text = d.text;
            row.glyph = d.glyph;
            row.cells = {d.text};
        });
        people.Reset({
            {L"Nova", std::wstring(icon::kSparkle)},
            {L"Quill", std::wstring(icon::kEdit)},
            {L"Ridge", std::wstring(icon::kFolder)},
        });
    }
    model_table.Bind(people).Column(L"Name", &ItemData::text, 160.0f);
    model_table.AddColumn(L"Glyph", 56.0f).Icon([](size_t row, std::wstring& s) {
        s = row < people.Count() ? people.At(row).glyph : L"";
    });

    auto& table_card = Sample(
        column, L"Table",
        L"Pin, drag headers to reorder, right-click columns, Shift+click multi-sort. "
        L"Arrows move cells, F2 edits, Ctrl+C copies TSV. Grouped by Environment.");
    static std::vector<uint8_t> checked(100000, 0);
    static std::unordered_map<size_t, std::wstring> notes;
    auto& table = table_card.Add<Table>();
    table.RowHeight(40.0f);
    table.AddColumn(L"#", 64.0f).Sortable(true).Frozen().Aggregate(ColumnAggregate::Count);
    table.AddColumn(L"On", 56.0f).CheckBox(
        [](size_t row) { return checked[row] != 0; },
        [](size_t row, bool value) { checked[row] = value ? 1 : 0; });
    table.AddColumn(L"Note").TextBox(
        [](size_t row) -> std::wstring {
            const auto it = notes.find(row);
            if (it != notes.end()) return it->second;
            return L"Note " + std::to_wstring(row + 1);
        },
        [](size_t row, std::wstring value) {
            if (value.empty() || value == L"Note " + std::to_wstring(row + 1)) notes.erase(row);
            else notes[row] = std::move(value);
        });
    table.AddColumn(L"Owner", 140.0f);
    table.AddColumn(L"Action", 108.0f).Button(L"Run", [&window](size_t row) {
        window.ShowToast(L"Run pulse " + std::to_wstring(row + 1));
    });
    table.AddColumn(L"Signal", 180.0f).Sortable(true);
    table.AddColumn(L"Updated", 180.0f);
    table.AddColumn(L"Environment").Sortable(true);
    table.AddColumn(L"Load", 80.0f).Progress([](size_t row) {
        return static_cast<float>((row % 10) + 1) / 10.0f;
    }).Aggregate(ColumnAggregate::Average);
    table.AddColumn(L"State", 48.0f).Icon([](size_t row, std::wstring& s) {
        s = (row % 5 == 0) ? icon::kWarning : icon::kCheckMark;
    });
    table.OnFrozenChanged([&window](int col, bool frozen) {
        static const wchar_t* kCol[] = {L"#", L"On", L"Note", L"Owner", L"Action",
                                        L"Signal", L"Updated", L"Environment", L"Load", L"State"};
        const wchar_t* name = (col >= 0 && col < 10) ? kCol[col] : L"?";
        window.ShowToast(std::wstring(frozen ? L"已固定 " : L"已取消固定 ") + name);
    })
        .OnSortChanged([&window](int col, int dir) {
            static const wchar_t* kCol[] = {L"#", L"On", L"Note", L"Owner", L"Action",
                                            L"Signal", L"Updated", L"Environment", L"Load", L"State"};
            const wchar_t* name = (col >= 0 && col < 10) ? kCol[col] : L"?";
            const wchar_t* state = dir > 0 ? L"升序" : (dir < 0 ? L"降序" : L"清除");
            window.ShowToast(std::wstring(L"排序 ") + name + L" · " + state);
        })
        .CellText([](size_t row, size_t col, std::wstring& out) {
            if (col == 0) out = std::to_wstring(row + 1);
            else if (col == 3) out = row % 2 == 0 ? L"Mira Chen" : L"Noah Lin";
            else if (col == 5) out = L"Pulse " + std::to_wstring(row + 1);
            else if (col == 6) out = L"A moment ago";
            else if (col == 7) out = row % 3 == 0 ? L"Production" : L"Staging";
            else out.clear();
        })
        .CellEditEnabled(true)
        .Footer(true)
        .GroupBy(7)
        .OnCellEdited([&window](size_t data_row, int col, std::wstring text) {
            window.ShowToast(L"单元格已更新 · 行 " + std::to_wstring(data_row + 1) + L" 列 " +
                             std::to_wstring(col) + L" → " + text);
        })
        .RowCount(100000)
        .SelectedIndex(2);

    // R21：逐行增量更新。1 万行、每拍只 At 一行——可见行带 + 页脚增量失效，
    // 更新期间输入/滚动/选择不整表刷新。
    auto& number_live_card = Sample(column, L"Table · live updates",
                                    L"10000 rows, one cell per tick. Incremental dirty-band "
                                    L"invalidation: no full-frame repaint per update. "
                                    L"Try scrolling or editing while it runs.");
    static VectorModel<LiveRow> live_model;
    if (live_model.Count() == 0) {
        for (int i = 0; i < 10000; ++i) {
            LiveRow row;
            row.name = L"Pulse " + std::to_wstring(i + 1);
            row.value = (i % 100) + 1;
            row.count = i % 7;
            live_model.Push(std::move(row));
        }
    }
    auto& number_live_table = number_live_card.Add<Table>();
    number_live_table.RowHeight(32.0f);
    number_live_table.Bind(live_model)
        .Column(L"Name", &LiveRow::name, 160.0f)
        .Column(L"Value", &LiveRow::value, 96.0f)
        .Column(L"Count", &LiveRow::count, 96.0f)
        .ColumnPrecision(1, 1)
        .Footer(true);
    number_live_table.Aggregate(1, ColumnAggregate::Sum);   // 页脚 Sum 随单行更新重算
    static size_t live_cursor = 0;
    static lumen::Window::TimerId live_timer = 0;
    number_live_card.Add<Row>().Spacing(8.0f).AlignCross(Cross::Center)
        .Add<ToggleButton>(L"Run updates")
        .OnToggled([&window](bool on) {
            if (on) {
                // 每拍只 At 一行：两参 At 发 OnChanged → 表格只失效该行可见行带 + 页脚。
                live_cursor = 0;
                live_timer = window.SetInterval(0.033f, [] {
                    const size_t row = live_cursor++ % live_model.Count();
                    live_model.At(row).value = static_cast<double>((live_cursor * 7) % 1000) / 10.0;
                    live_model.At(row, live_model.At(row));
                });
                window.ShowToast(L"Live updates on — scroll/edit while it runs");
            } else if (live_timer != 0) {
                window.ClearTimer(live_timer);
                live_timer = 0;
            }
        });

    if (g_plot_live.rows.empty()) {
        for (int i = 0; i < 11; ++i) {
            PlotRow row;
            row.title = L"Sheet " + std::to_wstring(i + 1);
            row.paper = L"A0";
            row.out_name = std::to_wstring(i + 1) + L"_sheet.pdf";
            row.status = L"Queued";
            g_plot_live.rows.push_back(std::move(row));
        }
        g_plot_live.model.Map([](const PlotRow& item, ItemRow& out) {
            out.cells = {item.checked ? L"1" : L"0", item.title, item.paper, item.out_name, L"",
                         item.status};
        });
        g_plot_live.model.Reset(g_plot_live.rows);
    }
    auto& plot_live_card = Sample(
        column, L"Table · live Reset",
        L"Same pattern as batch-plot: VectorModel Reset while progress/status change every frame.");
    auto& plot_live_head = plot_live_card.Add<Row>().Spacing(8.0f).AlignCross(Cross::Center);
    plot_live_head.Add<Button>(L"Simulate print", ButtonKind::Primary)
        .OnClick([&window] { g_plot_live.Start(window); });
    g_plot_live.status = &plot_live_head.Add<Label>(L"Idle", TextRole::Caption);
    g_plot_live.status->Secondary(true).Grow();
    auto& plot_live_table = plot_live_card.Add<Table>();
    plot_live_table.FillCross().RowHeight(36.0f).MinSize({0.0f, 280.0f});
    plot_live_table.Bind(g_plot_live.model);
    plot_live_table.AddColumn(L"On", 56.0f).CheckBox(
        [](size_t i) {
            return i < g_plot_live.rows.size() ? g_plot_live.rows[i].checked : false;
        },
        [](size_t i, bool v) {
            if (i < g_plot_live.rows.size()) g_plot_live.rows[i].checked = v;
        });
    plot_live_table.AddColumn(L"Title");
    plot_live_table.AddColumn(L"Paper", 80.0f).TextBox(
        [](size_t i) {
            return i < g_plot_live.rows.size() ? g_plot_live.rows[i].paper : std::wstring{};
        },
        [](size_t i, std::wstring v) {
            if (i < g_plot_live.rows.size()) g_plot_live.rows[i].paper = std::move(v);
        });
    plot_live_table.AddColumn(L"File");
    plot_live_table.AddColumn(L"Load", 120.0f).Progress([](size_t i) {
        return i < g_plot_live.rows.size() ? g_plot_live.rows[i].progress : 0.0f;
    });
    plot_live_table.AddColumn(L"State", 100.0f);
    plot_live_table.CellText([](size_t row, size_t col, std::wstring& out) {
        if (row >= g_plot_live.rows.size()) {
            out.clear();
            return;
        }
        const PlotRow& item = g_plot_live.rows[row];
        if (col == 1) out = item.title;
        else if (col == 3) out = item.out_name;
        else if (col == 5) out = item.status;
        else out.clear();
    });

    auto& log_card = Sample(column, L"LogView", L"Monospace, follow-tail, Ctrl+C copies the selected line.");
    auto& log = Wide(log_card).Add<LogView>();
    log.Grow()
        .Follow(true)
        .ItemCount(240)
        .LineText([](size_t i, std::wstring& s) {
            s = L"[" + std::to_wstring(i) + L"] pulse replica heartbeat";
        })
        .LineLevel([](size_t i) {
            if (i % 17 == 0) return LogLevel::Error;
            if (i % 7 == 0) return LogLevel::Warn;
            if (i % 5 == 0) return LogLevel::Debug;
            return LogLevel::Info;
        });

    auto& grid_card = Sample(column, L"GridView", L"Virtualized icon grid. Right-click for a Menu.");
    auto& tiles = grid_card.Add<GridView>();
    tiles.ItemCount(48)
        .ItemSize({96.0f, 88.0f})
        .ItemGap(8.0f)
        .ItemGlyph([](size_t i, std::wstring& s) {
            static const wchar_t* kGlyphs[] = {icon::kFolder, icon::kImage, icon::kFile, icon::kCode,
                                               icon::kChart, icon::kSettings};
            s = kGlyphs[i % 6];
        })
        .ItemText([](size_t i, std::wstring& s) { s = L"Item " + std::to_wstring(i + 1); })
        .OnActivate([&window, &tiles](size_t) {
            window.ShowToast(L"Open " + std::to_wstring(tiles.SelectedIndex() + 1));
        });
    Menu grid_menu;
    grid_menu.AddItem(L"Open", [&window] { window.ShowToast(L"Open"); });
    grid_menu.AddItem(L"Pin", [&window] { window.ShowToast(L"Pinned"); });
    grid_menu.AddSeparator();
    grid_menu.AddItem(L"Remove", [&window] { window.ShowToast(L"Removed"); });
    tiles.ContextMenu(std::move(grid_menu));

    auto& tree_card = Sample(column, L"TreeView", L"Virtualized. → expand, ← collapse, double-click activate.");
    auto& tree_head = tree_card.Add<Row>().AlignCross(Cross::Center);
    tree_head.Add<Label>(L"Library / Gallery / Button", TextRole::Caption).Secondary(true);
    tree_head.Add<Spacer>();
    auto& tree = Wide(tree_card).Add<TreeView>();
    tree.Grow();
    tree.Roots(3);
    tree.ChildCount([](size_t id) { return id < 3 ? 3 : (id >= 10 && id < 40 ? 2 : 0); });
    tree.ChildAt([](size_t id, size_t index) { return 10 + id * 10 + index; });
    tree.ItemText([](size_t id, std::wstring& s) {
        if (id == 0) s = L"Library";
        else if (id == 1) s = L"Gallery";
        else if (id == 2) s = L"Button";
        else s = L"Node " + std::to_wstring(id);
    });
    tree.ItemGlyph([](size_t id, std::wstring& s) {
        if (id < 3) s = icon::kLayers;
    });
    tree.Expand(0);
    tree.Expand(10);
    tree.SelectedId(12);
    tree.OnActivate([&window](size_t id) {
        window.ShowToast(L"激活节点 " + std::to_wstring(id));
    });
    tree_head.Add<Button>(L"ExpandAll", ButtonKind::Subtle)
        .SizeClass(ButtonSize::Small)
        .OnClick([&tree] { tree.ExpandAll(); });

    auto& tt_card = Sample(column, L"TreeTable", L"Tree in the first column, extra columns beside it.");
    auto& tree_table = tt_card.Add<TreeTable>();
    tree_table.AddColumn(L"Name", 220.0f).AddColumn(L"Kind", 100.0f).AddColumn(L"Size");
    tree_table.SetFlatData({TreeTable::kNone, TreeTable::kNone, 0, 0, 1, 1, 5});
    tree_table.ItemGlyph([](size_t id, std::wstring& s) {
        s = (id == 0 || id == 1 || id == 5) ? icon::kFolder : icon::kCode;
    });
    tree_table.ItemText([](size_t id, std::wstring& s) {
        static const wchar_t* kName[] = {L"src", L"include", L"tree_table.cpp", L"view_box.cpp",
                                         L"lumen.h", L"lumen", L"TreeTable.h"};
        s = id < 7 ? kName[id] : L"?";
    });
    tree_table.CellText([](size_t id, size_t col, std::wstring& out) {
        if (col == 1) out = (id == 0 || id == 1 || id == 5) ? L"Folder" : L"File";
        else if (col == 2) out = (id == 0 || id == 1 || id == 5) ? L"—" : L"12 KB";
        else out.clear();
    });
    tree_table.Expand(0).Expand(1).SelectedId(2);
    tree_table.OnActivate([&window](size_t id) {
        window.ShowToast(L"打开节点 " + std::to_wstring(id));
    });
    tt_card.Add<Button>(L"ExpandAll", ButtonKind::Subtle)
        .SizeClass(ButtonSize::Small)
        .OnClick([&tree_table] { tree_table.ExpandAll(); });

    auto& pages = Sample(column, L"Pagination", L"1-based. Programming Current() does not fire OnNavigate.");
    auto& page_row = pages.Add<Row>().Spacing(12.0f).AlignCross(Cross::Center);
    auto& page_label = page_row.Add<Label>(L"Page 1 / 12", TextRole::CaptionStrong);
    auto& pager = pages.Add<Pagination>();
    pager.PageCount(12).Current(1).OnNavigate([&window, &page_label](size_t page) {
        page_label.Text(L"Page " + std::to_wstring(page) + L" / 12");
        window.ShowToast(L"Page " + std::to_wstring(page));
    });

    auto& carousel_card = Sample(column, L"Carousel", L"Pages + dots. Independent of Stepper.");
    auto& carousel = Wide(carousel_card).Add<Carousel>();
    carousel.Grow();
    carousel.Card(Panel::CardStyle::Input, 16.0f);
    auto& page0 = carousel.AddPage<StackPanel>();
    page0.Padding(20.0f, 12.0f).Spacing(8.0f);
    page0.Add<Label>(L"Replica count", TextRole::Caption).Secondary(true);
    page0.Add<Row>().Add<ComboBox>()
        .AddItems({L"staging.lumen.dev", L"canary.lumen.dev", L"prod.lumen.dev"})
        .SelectedIndex(0);
    page0.Add<Row>().Add<NumberBox>().Range(0.0, 10.0).Value(6.5).Decimals(1).Step(0.5);
    auto& page1 = carousel.AddPage<StackPanel>();
    page1.Padding(20.0f, 12.0f).Spacing(8.0f);
    page1.Add<Label>(L"Build snapshot", TextRole::Caption).Secondary(true);
    auto& snap_head = page1.Add<Row>().AlignCross(Cross::Center);
    snap_head.Add<Label>(L"ProgressBar", TextRole::Caption).Secondary(true);
    snap_head.Add<Spacer>();
    g_job.wizard_pct = &snap_head.Add<Label>(L"0%", TextRole::CaptionStrong);
    auto& snap_bar = page1.Add<ProgressBar>();
    g_job.wizard_bar = &snap_bar;
    snap_bar.Value(0.0f);
    page1.Add<Row>().Add<Button>(L"Run build", ButtonKind::Standard)
        .SizeClass(ButtonSize::Small)
        .OnClick([] {
            g_job.Start();
            ShowPage(L"status");
        });
    auto& page2 = carousel.AddPage<StackPanel>();
    page2.Padding(20.0f, 12.0f).Spacing(8.0f).AlignCross(Cross::Center);
    page2.Add<Label>(L"Publish", TextRole::Caption).Secondary(true);
    page2.Add<Button>(L"Publish", ButtonKind::Primary).OnClick([&window] {
        window.ShowToast(L"Published");
        if (g_job.publish_empty) g_job.publish_empty->Visible(false);
        if (g_job.publish_ok) g_job.publish_ok->Visible(true);
        ShowPage(L"status");
    });
}

} // namespace gallery
