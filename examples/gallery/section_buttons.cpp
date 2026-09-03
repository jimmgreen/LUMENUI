#include "common.h"
#include <cstdlib>
#include <string>

namespace gallery {
namespace {
lumen::Label* g_tally = nullptr;
}

void BuildButtons(lumen::StackPanel& column, lumen::Window& window) {
    using namespace lumen;
    PageHead(column, L"Buttons",
             L"Click to do something. Kinds, sizes, disabled, pill, and shimmer live here.");

    auto& kinds = Sample(column, L"Button", L"Primary / Standard / Subtle / Transparent / Danger.");
    auto& kind_row = kinds.Add<Row>().Spacing(8.0f).AlignCross(Cross::Center);
    auto add_kind = [&](std::wstring_view label, ButtonKind kind) {
        kind_row.Add<Button>(label, kind).OnClick([&window, label] {
            window.ShowToast(std::wstring(label));
        });
    };
    add_kind(L"Primary", ButtonKind::Primary);
    add_kind(L"Standard", ButtonKind::Standard);
    add_kind(L"Subtle", ButtonKind::Subtle);
    add_kind(L"Transparent", ButtonKind::Transparent);
    add_kind(L"Danger", ButtonKind::Danger);

    auto& sizes = kinds.Add<Row>().Spacing(8.0f).AlignCross(Cross::Center);
    sizes.Add<Button>(L"Small").SizeClass(ButtonSize::Small);
    sizes.Add<Button>(L"Medium").SizeClass(ButtonSize::Medium);
    sizes.Add<Button>(L"Large").SizeClass(ButtonSize::Large);
    sizes.Add<Button>(L"Disabled").Enabled(false);
    sizes.Add<Button>(L"Pill").Pill(true);
    auto& shimmer = sizes.Add<Button>(L"Shimmer", ButtonKind::Standard);
    shimmer.Glyph(icon::kZap).Shimmer(true).OnClick([&window] {
        window.ShowToast(L"Shimmer");
        g_job.Start();
        ShowPage(L"status");
    });
    auto& icons = kinds.Add<Row>().Spacing(8.0f).AlignCross(Cross::Center);
    icons.Add<Button>(L"", ButtonKind::Transparent)
        .Glyph(icon::kSearch)
        .SizeClass(ButtonSize::Small)
        .ToolTip(L"Focus AutoSuggestBox on Input")
        .OnClick([&window] {
            ShowPage(L"input");
            if (g_suggest) g_suggest->Focus();
            window.ShowToast(L"AutoSuggestBox focused");
        });
    icons.Add<Button>(L"", ButtonKind::Transparent)
        .Glyph(icon::kZap)
        .SizeClass(ButtonSize::Small)
        .ToolTip(L"Start the build job on Status")
        .OnClick([&window] {
            g_job.Start();
            ShowPage(L"status");
            window.ShowToast(L"Build job");
        });
    icons.Add<Button>(L"Run build", ButtonKind::Primary)
        .OnClick([] {
            g_job.Start();
            ShowPage(L"status");
        });

    auto& toggles = Sample(column, L"ToggleButton", L"Sticky; independent of SplitButton.");
    auto& toggle_row = toggles.Add<Row>().Spacing(8.0f).AlignCross(Cross::Center);
    auto& bold = toggle_row.Add<ToggleButton>(L"Bold");
    bold.Glyph(icon::kFavorite).Checked(true).OnToggled([&window, &bold](bool) {
        window.ShowToast(bold.Checked() ? L"Bold on" : L"Bold off");
    });
    toggle_row.Add<ToggleButton>(L"Wrap").OnToggled([&window](bool) { window.ShowToast(L"Wrap toggled"); });
    auto& live = toggle_row.Add<ToggleButton>(L"Live");
    live.SizeClass(ButtonSize::Small).Pill(true).Checked(true).OnToggled([&window, &live](bool) {
        window.ShowToast(live.Checked() ? L"Live preview" : L"Live off");
    });
    toggle_row.Add<ToggleButton>(L"Off").Enabled(false);

    auto& repeat = Sample(column, L"RepeatButton", L"Hold to fire. Default delay 0.40s / interval 0.05s.");
    auto& repeat_row = repeat.Add<Row>().Spacing(8.0f).AlignCross(Cross::Center);
    g_tally = &repeat_row.Add<Label>(L"0", TextRole::BodyStrong);
    auto step_tally = [](int delta) {
        if (!g_tally) return;
        int n = static_cast<int>(wcstol(g_tally->Text().c_str(), nullptr, 10));
        g_tally->Text(std::to_wstring(Clamp(n + delta, 0, 999)));
    };
    repeat_row.Add<RepeatButton>(L"−")
        .SizeClass(ButtonSize::Small)
        .Kind(ButtonKind::Subtle)
        .OnClick([step_tally] { step_tally(-1); });
    repeat_row.Add<RepeatButton>(L"+")
        .SizeClass(ButtonSize::Small)
        .Kind(ButtonKind::Subtle)
        .OnClick([step_tally] { step_tally(1); });

    auto& split = Sample(column, L"SplitButton", L"Main click vs arrow menu.");
    auto& split_row = split.Add<Row>().Spacing(12.0f);
    auto& toggle_split = split_row.Add<SplitButton>(L"Auto deploy").Toggle(true).Checked(true);
    toggle_split.OnToggled([&window](bool on) {
        window.ShowToast(on ? L"Auto deploy on" : L"Auto deploy off");
    });
    toggle_split.OnDropdown([&window, &toggle_split] {
        Menu menu;
        for (const wchar_t* env : {L"Canary", L"Staging", L"Production"}) {
            menu.AddItem(env, [&window, &toggle_split, env] {
                toggle_split.Text(std::wstring(L"Auto deploy · ") + env);
                window.ShowToast(std::wstring(L"Auto deploy → ") + env);
            });
        }
        menu.PopupTo(toggle_split);
    });
    auto& publish = split_row.Add<SplitButton>(L"Publish");
    publish.Primary(true).StatusDot(Color::Hex(0xFFFFFF));
    publish.OnClick([&window] { window.ShowToast(L"Published"); });
    Menu targets;
    MenuItem& more = targets.AddItem(L"More targets", nullptr);
    more.AddChild(L"Canary", [&window] { window.ShowToast(L"Deploy canary"); });
    more.AddChild(L"Edge", [&window] { window.ShowToast(L"Deploy edge"); });
    targets.AddItem(L"Staging", [&window] { window.ShowToast(L"Deploy staging"); }).shortcut =
        L"Ctrl+S";
    targets.AddItem(L"Production", [&window] { window.ShowToast(L"Deploy production"); }).shortcut =
        L"Ctrl+P";
    for (int i = 1; i <= 24; ++i) {
        targets.AddItem(L"Region " + std::to_wstring(i), [&window, i] {
            window.ShowToast(L"Deploy region " + std::to_wstring(i));
        });
    }
    publish.DropdownMenu(std::move(targets));

    auto& dd = Sample(column, L"DropDownButton", L"Whole button opens a menu — no primary split.");
    auto& dd_row = dd.Add<Row>().Spacing(12.0f).AlignCross(Cross::Center);
    auto& export_dd = dd_row.Add<DropDownButton>(L"Export");
    export_dd.OnDropdown([&window, &export_dd] {
        Menu menu;
        for (const wchar_t* fmt : {L"PNG", L"SVG", L"JSON"}) {
            menu.AddItem(fmt, [&window, &export_dd, fmt] {
                export_dd.Text(std::wstring(L"Export · ") + fmt);
                window.ShowToast(std::wstring(L"Export → ") + fmt);
            });
        }
        menu.PopupTo(export_dd);
    });
    auto& new_dd = dd_row.Add<DropDownButton>(L"New", ButtonKind::Primary);
    new_dd.Glyph(icon::kAdd);
    new_dd.OnDropdown([&window, &new_dd] {
        Menu menu;
        menu.AddItem(L"File", [&window, &new_dd] {
            new_dd.Text(L"New file");
            window.ShowToast(L"New file");
        }).glyph = icon::kFile;
        menu.AddItem(L"Folder", [&window, &new_dd] {
            new_dd.Text(L"New folder");
            window.ShowToast(L"New folder");
        }).glyph = icon::kFolder;
        menu.PopupTo(new_dd);
    });
    auto& sort_dd = dd_row.Add<DropDownButton>(L"Sort");
    sort_dd.SizeClass(ButtonSize::Small).Kind(ButtonKind::Subtle).Glyph(icon::kSort);
    Menu sort_menu;
    sort_menu.AddItem(L"Name", [&window, &sort_dd] {
        sort_dd.Text(L"Name");
        window.ShowToast(L"Sort · Name");
    });
    sort_menu.AddItem(L"Date", [&window, &sort_dd] {
        sort_dd.Text(L"Date");
        window.ShowToast(L"Sort · Date");
    });
    sort_dd.DropdownMenu(std::move(sort_menu));

    auto& link = Sample(column, L"HyperlinkButton", L"Inline action, not a filled button.");
    link.Add<Row>().Add<HyperlinkButton>(L"Read the control reference").OnClick([&window] {
        ShowPage(L"overview");
        window.ShowToast(L"lumen://docs");
    });

    auto& bar = Sample(column, L"MenuBar", L"Window menus. Alt+F / Alt+E. Click a title to pop a Menu.");
    auto& menubar = Wide(bar).Add<MenuBar>();
    menubar.Grow();
    Menu file;
    file.AddItem(L"&New build", [&window] { window.ShowToast(L"New build"); });
    file.AddItem(L"&Open log", [&window] { window.ShowToast(L"Open log"); }).Glyph(icon::kFile);
    file.AddSeparator();
    file.AddItem(L"Save snapshot", nullptr).Disabled(true);
    file.AddItem(L"E&xit", [&window] { window.Close(); });
    Menu edit;
    edit.AddItem(L"&Copy settings", [&window] { window.ShowToast(L"Copy settings"); });
    edit.AddItem(L"&Word wrap", [&window] { window.ShowToast(L"Word wrap"); }).Checked(true);
    menubar.AddMenu(L"&File", std::move(file)).AddMenu(L"&Edit", std::move(edit));

    auto& commands = Sample(column, L"CommandBar", L"Overflow goes into ⋯ as a Menu.");
    auto& cmd = commands.Add<CommandBar>();
    cmd.Items({
           {L"new", L"New file", icon::kAdd},
           {L"open", L"Open folder", icon::kFolderOpen},
           {L"save", L"Save", icon::kSave},
           {L"divider", L"", L"", CommandBarItemType::Separator},
           {L"preview", L"Preview", icon::kView, CommandBarItemType::Toggle, true, true},
           {L"copy_path", L"Copy path", icon::kCopy, CommandBarItemType::Action, true, false, true},
           {L"duplicate", L"Duplicate", icon::kCopy, CommandBarItemType::Action, true, false,
            true},
           {L"export", L"Export snapshot", icon::kDownload, CommandBarItemType::Action, true, false,
            true},
       })
        .ItemBadge(L"new", InfoBadgeData::Dot())
        .OnInvoked([&window](std::wstring_view id, bool checked) {
            std::wstring message;
            if (id == L"new") message = L"New file";
            else if (id == L"open") message = L"Open folder";
            else if (id == L"save") message = L"Project saved";
            else if (id == L"preview") message = checked ? L"Preview on" : L"Preview off";
            else if (id == L"copy_path") message = L"Path copied";
            else if (id == L"duplicate") message = L"Duplicated";
            else if (id == L"export") message = L"Snapshot exported";
            window.ShowToast(message);
        });
}

} // namespace gallery
