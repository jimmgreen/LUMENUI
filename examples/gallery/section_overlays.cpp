#include "common.h"
#include <memory>
#include <string>

namespace gallery {
namespace {
lumen::Flyout g_filter_flyout;
lumen::Flyout g_above_flyout;
lumen::TeachingTip g_dd_tip;
lumen::TeachingTip g_plain_tip;
lumen::Dialog g_dismiss;
lumen::Dialog g_wide;
lumen::Dialog g_confirm;
lumen::Drawer g_drawer;
lumen::Button* g_filter_anchor = nullptr;
lumen::Button* g_above_anchor = nullptr;
lumen::DropDownButton* g_export_dd = nullptr;
lumen::Button* g_plain_anchor = nullptr;
}

void BuildOverlays(lumen::StackPanel& column, lumen::Window& window) {
    using namespace lumen;
    PageHead(column, L"Overlays",
             L"Things that sit on top: dialog, flyout, teaching tip, tooltip, context menu.");

    auto& dialog = Sample(column, L"Dialog",
                          L"Content between title and footer. Enter = default, Esc = cancel. "
                          L"Result toast. Compact 320 / Standard 420 / Wide 560.");
    auto& dialog_row = dialog.Add<Row>().Spacing(8.0f).AlignCross(Cross::Center);
    dialog_row.Add<Button>(L"Add Component", ButtonKind::Primary).OnClick([&window] {
        ShowDialog(window);
    });
    if (g_dismiss.Title().empty()) {
        g_dismiss.Title(L"Nothing to confirm")
            .Message(L"DefaultClose() — click the dimmed mask or press Esc. No footer buttons.")
            .DefaultClose();
    }
    dialog_row.Add<Button>(L"Esc / mask close", ButtonKind::Standard).OnClick([&window] {
        window.ShowDialog(g_dismiss);
    });
    if (g_wide.Title().empty()) {
        g_wide.CardSize(DialogSize::Wide)
            .Title(L"Export settings")
            .Message(L"Wide card. Enter saves. Esc is Close (not Discard).")
            .CloseButton(L"Not now")
            .SecondaryButton(L"Discard")
            .PrimaryButton(L"Save")
            .DefaultButton(DialogCommand::Primary)
            .CancelButton(DialogCommand::Close)
            .OnResult([&window](DialogResult r) {
                if (r == DialogResult::Primary) window.ShowToast(L"Saved");
                else if (r == DialogResult::Secondary) window.ShowToast(L"Discarded");
                else if (r == DialogResult::Close) window.ShowToast(L"Not now");
            });
        g_wide.Add<CheckBox>(L"Include metadata").Checked(true);
        g_wide.Add<CheckBox>(L"Flatten nested folders");
    }
    dialog_row.Add<Button>(L"Wide · 3 buttons").OnClick([&window] { window.ShowDialog(g_wide); });
    if (g_confirm.Title().empty()) {
        g_confirm.CardSize(DialogSize::Compact)
            .Title(L"Delete file")
            .Message(L"Compact card. Enter deletes. Esc cancels.")
            .SecondaryButton(L"Cancel")
            .PrimaryButton(L"Delete")
            .DefaultButton(DialogCommand::Primary)
            .CancelButton(DialogCommand::Secondary)
            .OnResult([&window](DialogResult r) {
                if (r == DialogResult::Primary) window.ShowToast(L"Deleted");
                else if (r == DialogResult::Secondary) window.ShowToast(L"Kept");
            });
    }
    dialog_row.Add<Button>(L"Compact confirm").OnClick([&window] {
        window.ShowDialog(g_confirm);
    });
    dialog_row.Add<Button>(L"Busy overlay", ButtonKind::Standard).OnClick([&window] {
        window.ShowBusy(L"正在导入…", [&window] { window.ShowToast(L"已取消"); });
        window.SetTimeout(1.8f, [&window] {
            if (window.BusyActive()) {
                window.CloseBusy();
                window.ShowToast(L"导入完成");
            }
        });
    });

    auto& drawer = Sample(column, L"Drawer", L"Edge overlay. Click the mask or Esc to dismiss.");
    if (g_drawer.ChildCount() == 0) {
        g_drawer.PanelWidth(280.0f);
        g_drawer.Add<Label>(L"FILTERS", TextRole::CaptionStrong);
        g_drawer.Add<CheckBox>(L"Only favorites").Checked(true);
        g_drawer.Add<CheckBox>(L"Hide archived");
        g_drawer.Add<Separator>();
        g_drawer.Add<Button>(L"Apply", ButtonKind::Primary).OnClick([&window] {
            window.CloseDrawer();
            window.ShowToast(L"Drawer applied");
        });
    }
    drawer.Add<Button>(L"Open right drawer").OnClick([&window] {
        window.ShowDrawer(g_drawer, Edge::Right);
    });

    auto& system = Sample(column, L"Window helpers",
                          L"Ctrl+S is BindShortcut. File dialogs are lumen::dialogs.");
    window.BindShortcut(L"Ctrl+S", [&window] { window.ShowToast(L"Ctrl+S · saved"); });
    auto& sys_row = system.Add<Row>().Spacing(8.0f).AlignCross(Cross::Center);
    sys_row.Add<Button>(L"Open file…").OnClick([&window] {
        if (auto path = dialogs::PickFile(window, L"文本|*.txt;*.md|全部|*.*")) {
            window.ShowToast(*path);
        }
    });
    sys_row.Add<Button>(L"Copy hello", ButtonKind::Subtle).OnClick([&window] {
        clipboard::Text(L"hello from LUMEN");
        window.ShowToast(L"Copied");
    });

    auto& flyout = Sample(column, L"Flyout",
                          L"Light layer. Click outside to dismiss. Placement Below / Above.");
    auto& filter = g_filter_flyout;
    if (filter.ChildCount() == 0) {
        filter.FlyoutWidth(240.0f).Placement(FlyoutPlacement::Below);
        filter.Add<Label>(L"FILTER VIEWS", TextRole::CaptionStrong);
        filter.Add<CheckBox>(L"Only favorites").Checked(true);
        filter.Add<CheckBox>(L"Hide archived");
        filter.Add<Separator>();
        filter.Add<Button>(L"Apply", ButtonKind::Primary).OnClick([&window] {
            window.CloseFlyout();
            window.ShowToast(L"Filter applied");
        });
    }
    auto& above = g_above_flyout;
    if (above.ChildCount() == 0) {
        above.FlyoutWidth(200.0f).Placement(FlyoutPlacement::Above);
        above.Add<Label>(L"Placement::Above", TextRole::CaptionStrong);
        above.Add<Label>(L"Flips if there is no room.", TextRole::Caption).Secondary(true).Wrap(true);
    }
    auto& fly_row = flyout.Add<Row>().Spacing(8.0f).AlignCross(Cross::Center);
    auto& filter_btn = fly_row.Add<Button>(L"Filter views").Glyph(icon::kFilter);
    g_filter_anchor = &filter_btn;
    filter_btn.OnClick([&window] { window.ShowFlyout(g_filter_flyout, g_filter_anchor); });
    auto& above_btn = fly_row.Add<Button>(L"Open above", ButtonKind::Subtle);
    g_above_anchor = &above_btn;
    above_btn.OnClick([&window] { window.ShowFlyout(g_above_flyout, g_above_anchor); });

    auto& tip = Sample(column, L"TeachingTip",
                       L"Arrowed coach mark. Action, Closable, Placement::Above.");
    if (g_dd_tip.Title().empty()) {
        g_dd_tip.Title(L"TeachingTip")
            .Message(L"Anchored bubble with an action. Close with Esc, ×, or a click outside.")
            .Glyph(icon::kSparkle)
            .Action(L"知道了", [&window] { window.ShowToast(L"TeachingTip · 知道了"); });
    }
    if (g_plain_tip.Title().empty()) {
        g_plain_tip.Title(L"No action")
            .Message(L"Placement::Above. Closable(false) — only Esc or an outside click.")
            .Glyph(icon::kInfo)
            .Placement(TeachingTipPlacement::Above)
            .Closable(false);
    }
    auto& tip_row = tip.Add<Row>().Spacing(8.0f).AlignCross(Cross::Center);
    auto& export_dd = tip_row.Add<DropDownButton>(L"Export");
    g_export_dd = &export_dd;
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
    tip_row.Add<Button>(L"Show tip", ButtonKind::Subtle)
        .SizeClass(ButtonSize::Small)
        .OnClick([&window] {
            if (g_export_dd) window.ShowTeachingTip(g_dd_tip, g_export_dd);
        });
    auto& plain_btn = tip_row.Add<Button>(L"Above, no ×", ButtonKind::Subtle)
                          .SizeClass(ButtonSize::Small);
    g_plain_anchor = &plain_btn;
    plain_btn.OnClick([&window] { window.ShowTeachingTip(g_plain_tip, g_plain_anchor); });

    auto& tooltip = Sample(column, L"ToolTip",
                           L"String, custom tree, Closable(false). Hover and wait.");
    auto& tip_btns = tooltip.Add<Row>().Spacing(8.0f).AlignCross(Cross::Center);
    auto& primary = tip_btns.Add<Button>(L"Custom tip", ButtonKind::Primary);
    auto custom = std::make_unique<ToolTip>();
    custom->Add<Label>(L"PRO TIP", TextRole::CaptionStrong).TextGlow(true);
    auto& custom_row = custom->Add<Row>().Spacing(8.0f).AlignCross(Cross::Center);
    custom_row.Add<IconView>(icon::kZap).Box(20.0f).IconSize(16.0f);
    custom_row.Add<Label>(L"Primary fills with light. Hover to feel the glow.")
        .Secondary(true)
        .Wrap(true)
        .Grow();
    primary.ToolTip(std::move(custom));
    tip_btns.Add<Button>(L"String tip", ButtonKind::Standard)
        .ToolTip(L"次级操作（悬停试试提示）");
    auto& sticky = tip_btns.Add<Button>(L"No close button", ButtonKind::Subtle);
    auto sticky_tip = std::make_unique<ToolTip>();
    sticky_tip->Closable(false).MaxWidth(200.0f);
    sticky_tip->Add<Label>(L"Closable(false)", TextRole::CaptionStrong);
    sticky_tip->Add<Label>(L"No ×. Leaves when the pointer leaves.", TextRole::Caption)
        .Secondary(true)
        .Wrap(true);
    sticky.ToolTip(std::move(sticky_tip));
    tip_btns.Add<IconView>(icon::kInfo)
        .Box(28.0f)
        .IconSize(16.0f)
        .ToolTip(L"IconView · string tip");

    auto& menu = Sample(column, L"Menu",
                          L"Mnemonic &X. Header groups. Radio / check in the icon column. "
                          L"Hover diagonally into a submenu — the safe triangle keeps it open.");
    auto& menu_row = menu.Add<Row>().Spacing(8.0f).AlignCross(Cross::Center);
    auto& menu_btn = menu_row.Add<Button>(L"Open menu", ButtonKind::Standard);
    menu_btn.OnClick([&window, &menu_btn] {
        Menu popup;
        popup.AddHeader(L"Clipboard");
        popup.AddItem(L"&Copy", [&window] { window.ShowToast(L"Copy"); })
            .Glyph(icon::kCopy)
            .Shortcut(L"Ctrl+C");
        popup.AddItem(L"&Paste", [&window] { window.ShowToast(L"Paste"); }).Shortcut(L"Ctrl+V");
        popup.AddSeparator();
        popup.AddHeader(L"View");
        popup.AddItem(L"&Compact", [&window] { window.ShowToast(L"Compact"); })
            .Radio()
            .RadioGroup(L"density")
            .Checked(true);
        popup.AddItem(L"C&omfortable", [&window] { window.ShowToast(L"Comfortable"); })
            .Radio()
            .RadioGroup(L"density");
        popup.AddItem(L"&Word wrap", [&window] { window.ShowToast(L"Word wrap"); }).Checked(true);
        popup.AddSeparator();
        MenuItem& more = popup.AddItem(L"More", nullptr);
        more.AddChild(L"&Canary", [&window] { window.ShowToast(L"Canary"); });
        more.AddChild(L"&Edge", [&window] { window.ShowToast(L"Edge"); });
        popup.AddItem(L"&Delete", nullptr).Disabled(true);
        popup.PopupTo(menu_btn);
    });
    auto& expander = menu.Add<Expander>(L"Right-click for a context menu");
    expander.Add<Label>(L"Same Menu type as MenuBar overflow and DropDownButton.",
                        TextRole::Caption)
        .Secondary(true)
        .Wrap(true);
    Menu token_menu;
    token_menu.AddItem(L"Copy tokens", [&window] { window.ShowToast(L"Copied glow tokens"); });
    token_menu.AddItem(L"Reset intensity", [&window] { SetIntensity(window, 0.5f); });
    token_menu.AddSeparator();
    MenuItem& more = token_menu.AddItem(L"More", nullptr);
    more.AddChild(L"Canary", [&window] { window.ShowToast(L"Canary"); });
    expander.ContextMenu(std::move(token_menu));
}

} // namespace gallery
