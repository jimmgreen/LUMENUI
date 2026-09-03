#include "common.h"
#include <string>

namespace gallery {

void BuildNavigation(lumen::StackPanel& column, lumen::Window& window) {
    using namespace lumen;
    PageHead(column, L"Navigation",
             L"Move somewhere else. Nested NavigationView is a demo — it is not the gallery shell.");

    auto& crumb_card = Sample(column, L"Breadcrumb", L"Click any level.");
    auto& crumb = crumb_card.Add<Breadcrumb>();
    crumb.AddItem(L"组件库").AddItem(L"列表与树").AddItem(L"TreeView");
    crumb.OnNavigate([](size_t index) {
        if (index >= 1) ShowPage(L"collections");
    });

    auto& tabs_card = Sample(column, L"TabControl",
                             L"× or middle-click closes Archive / Sent. Ctrl+W closes the selected closable tab. Inbox stays pinned.");
    auto& tabs = Wide(tabs_card).Add<TabControl>();
    tabs.Grow();
    auto& inbox_page = tabs.AddTab({L"inbox", L"Inbox", icon::kInbox, false, InfoBadgeData::Count(12)});
    auto& archive_page = tabs.AddTab({L"archive", L"Archive · 100000", icon::kFolder, true});
    auto& sent_page = tabs.AddTab({L"sent", L"Sent · 0", icon::kSend, true});
    tabs.OnTabClosing([&window](std::wstring_view id) {
        if (id == L"sent") window.ShowToast(L"Sent view closed");
        return true;
    }).OnTabClosed([&window](std::wstring_view id) {
        if (id == L"archive") window.ShowToast(L"Archive tab closed");
    });
    inbox_page.Add<Label>(L"Inbox tab body. ListView samples live on Collections.", TextRole::Caption)
        .Secondary(true)
        .Wrap(true);
    inbox_page.Add<Row>().Add<HyperlinkButton>(L"Open Collections").OnClick([] {
        ShowPage(L"collections");
    });
    archive_page.Add<Label>(L"Closable archive tab.", TextRole::Caption).Secondary(true);
    sent_page.Add<EmptyState>()
        .Title(L"Nothing in the beam")
        .Hint(L"Sent is empty.")
        .Glyph(icon::kSend);

    auto& overflow_card =
        Sample(column, L"TabControl · overflow",
               L"Drag a tab to reorder. Wheel the strip (not the page) to scroll. ⋯ lists every tab.");
    auto& file_tabs = Wide(overflow_card).Add<TabControl>();
    file_tabs.Grow().CanReorder(true);
    static const wchar_t* kFiles[] = {
        L"main.cpp",      L"painter.cpp", L"window_impl.cpp", L"tab_control.cpp",
        L"dialog.cpp",    L"list_view.cpp", L"combo_box.cpp", L"theme.cpp",
        L"CMakeLists.txt", L"README.md"};
    for (const wchar_t* name : kFiles) {
        auto& page = file_tabs.AddTab({name, name, icon::kCode, true});
        page.Add<Label>(name, TextRole::BodyStrong);
        page.Add<Label>(L"Drag this tab, wheel the strip, or pick it from ⋯.", TextRole::Caption)
            .Secondary(true)
            .Wrap(true);
    }

    auto& stepper_card = Sample(column, L"Stepper", L"Completed steps can jump back.");
    auto& stepper = stepper_card.Add<Stepper>();
    stepper.AddStep(L"配置").AddStep(L"构建").AddStep(L"发布").AddStep(L"验证");
    stepper.Current(0);
    auto& stepper_nav = stepper_card.Add<Row>().Spacing(8.0f);
    auto& prev =
        stepper_nav.Add<Button>(L"上一步", ButtonKind::Subtle).SizeClass(ButtonSize::Small).Height(30.0f);
    auto& next =
        stepper_nav.Add<Button>(L"下一步", ButtonKind::Primary).SizeClass(ButtonSize::Small).Height(30.0f);
    auto sync = [&stepper, &prev, &next](size_t step) {
        stepper.Current(step);
        prev.Enabled(stepper.Current() > 0);
        next.Enabled(stepper.Current() + 1 < stepper.Count());
    };
    prev.OnClick([&stepper, sync] { sync(stepper.Current() - 1); });
    next.OnClick([&stepper, sync] { sync(stepper.Current() + 1); });
    stepper.OnStepChanged([&prev, &next, &stepper](size_t step) {
        prev.Enabled(step > 0);
        next.Enabled(step + 1 < stepper.Count());
    });
    prev.Enabled(stepper.Current() > 0);
    next.Enabled(stepper.Current() + 1 < stepper.Count());

    auto& title_card = Sample(column, L"TitleBar",
                              L"Window chrome lives on the real caption. This is a layout sketch.");
    auto& chrome = title_card.Add<Grid>(0.0, 1.0, 0.0).Gap(8.0f);
    auto& brand = chrome.Add<Row>().Spacing(8.0f).AlignCross(Cross::Center);
    brand.Add<IconView>(icon::kPackage)
        .Box(22.0f)
        .IconSize(16.0f)
        .Weight(1.5f)
        .CornerRadius(4.0f)
        .Background(Color{0.0f, 0.0f, 0.0f, 0.0f})
        .Stroke(Color{0.0f, 0.0f, 0.0f, 0.0f});
    brand.Add<Label>(L"LUMEN Gallery", TextRole::BodyStrong);
    chrome.Add<Label>(L"Frame::Client caption", TextRole::Caption)
        .Secondary(true)
        .Alignment(Align::Center);
    auto& caps = chrome.Add<Row>().Spacing(4.0f).AlignCross(Cross::Center).AlignMain(Main::End);
    caps.Add<Button>(L"", ButtonKind::Transparent)
        .Glyph(icon::kMinimize)
        .SizeClass(ButtonSize::Small)
        .OnClick([&window] { window.ShowToast(L"Minimize"); });
    caps.Add<Button>(L"", ButtonKind::Transparent)
        .Glyph(icon::kMaximize)
        .SizeClass(ButtonSize::Small)
        .OnClick([&window] { window.ShowToast(L"Maximize"); });
    caps.Add<Button>(L"", ButtonKind::Transparent)
        .Glyph(icon::kClose)
        .SizeClass(ButtonSize::Small)
        .OnClick([&window] { window.ShowToast(L"Close"); });

    auto& nav_card = Sample(column, L"NavigationView (nested)",
                            L"Pane search filters matching lineages; pick a suggestion to jump. Compact: search icon under the hamburger, or Ctrl+F. Breadcrumb above the page tracks the path — click a parent to jump. PageHost fades between pages.");
    auto& nav = nav_card.Add<NavigationView>();
    nav.Height(440.0f);
    nav.Items({
            {L"overview", L"Overview", icon::kHome},
            {L"files", L"Project files", icon::kFolder, NavigationItemType::Item, true, L"", true},
            {L"source", L"Source", icon::kCode, NavigationItemType::Item, true, L"files", true},
            {L"controls", L"Controls", icon::kLayers, NavigationItemType::Item, true, L"source"},
            {L"assets", L"Assets", icon::kPackage, NavigationItemType::Item, true, L"files"},
            {L"activity", L"Activity", icon::kClock},
        })
        .FooterItems({{L"settings", L"Project settings", icon::kSettings}})
        .ItemBadge(L"activity", InfoBadgeData::Count(3))
        .ItemBadge(L"files", InfoBadgeData::Dot())
        .ItemBadge(L"settings", InfoBadgeData::Dot());
    nav.DisplayMode(NavigationDisplayMode::Expanded)
        .SearchEnabled(true)
        .SearchPlaceholder(L"Filter pages")
        .ShowBreadcrumb(true)
        .SelectedId(L"overview");
    nav.Content().Padding(16.0f, 14.0f);
    auto& pages = nav.Content().Add<PageHost>().Grow();

    auto& overview = pages.Page(L"overview");
    overview.Spacing(8.0f).Grow();
    overview.Add<Label>(L"Project overview", TextRole::BodyStrong);
    overview.Add<Label>(L"4 source files · saved a moment ago", TextRole::Caption).Secondary(true);
    overview.Add<InfoBar>(L"On track")
        .Message(L"Preview build is green.")
        .Tone(InfoBar::InfoTone::Success)
        .Closable(false);

    auto& files = pages.Page(L"files");
    files.Spacing(8.0f).Grow();
    files.Add<Label>(L"Project files", TextRole::BodyStrong);
    auto& file_list = files.Add<ListView>();
    file_list.ItemCount(8)
        .ItemText([](size_t i, std::wstring& s) {
            static const wchar_t* names[] = {
                L"CMakeLists.txt", L"README.md", L"include/lumen/lumen.h",
                L"src/core/app.cpp", L"src/core/painter.cpp", L"src/controls/button.cpp",
                L"examples/gallery/main.cpp", L"tests/visual/main.cpp"};
            s = names[i % 8];
        })
        .ItemGlyph([](size_t i, std::wstring& s) { s = i < 2 ? icon::kFile : icon::kCode; })
        .Grow();

    auto& source = pages.Page(L"source");
    source.Spacing(8.0f).Grow();
    source.Add<Label>(L"Source tree", TextRole::BodyStrong);
    auto& source_list = source.Add<ListView>();
    source_list.ItemCount(6)
        .ItemText([](size_t i, std::wstring& s) {
            static const wchar_t* names[] = {L"control.cpp", L"layout.cpp", L"painter.cpp",
                                             L"theme.cpp", L"window_impl.cpp", L"animate.cpp"};
            s = names[i % 6];
        })
        .ItemGlyph([](size_t, std::wstring& s) { s = icon::kCode; })
        .Grow();

    auto& controls_page = pages.Page(L"controls");
    controls_page.Spacing(8.0f).Grow();
    controls_page.Add<Label>(L"Controls", TextRole::BodyStrong);
    auto& control_chips = controls_page.Add<WrapPanel>().Gap(8.0f, 8.0f);
    for (const wchar_t* name : {L"Button", L"ListView", L"Slider", L"Dialog"}) {
        control_chips.Add<Chip>(name).Selectable(true);
    }

    auto& assets = pages.Page(L"assets");
    assets.Spacing(8.0f).Grow();
    assets.Add<Label>(L"Assets", TextRole::BodyStrong);
    auto& asset_image = assets.Add<ImageView>();
    asset_image.SetBounds({0.0f, 0.0f, 220.0f, 124.0f});
    asset_image.LoadMemory(MakeSceneBitmap());
    asset_image.Stretch(ImageStretch::UniformToFill).CornerRadius(10.0f);

    auto& activity = pages.Page(L"activity");
    activity.Spacing(8.0f).Grow();
    activity.Add<Label>(L"Recent activity", TextRole::BodyStrong);
    auto& activity_list = activity.Add<ListView>();
    activity_list.ItemCount(4)
        .ItemText([](size_t i, std::wstring& s) {
            static const wchar_t* names[] = {L"README.md · edited", L"main.cpp · edited",
                                             L"theme.cpp · viewed", L"button.cpp · viewed"};
            s = names[i % 4];
        })
        .ItemGlyph([](size_t, std::wstring& s) { s = icon::kClock; })
        .Grow();

    auto& settings = pages.Page(L"settings");
    settings.Spacing(8.0f).Grow();
    settings.Add<Label>(L"Project settings", TextRole::BodyStrong);
    auto& fmt = settings.Add<SettingsCard>();
    fmt.Title(L"Format on save").Description(L"Apply clang-format on commit.").Glyph(icon::kCode);
    fmt.Add<Switch>().Checked(true);

    nav.OnSelectionChanged([&pages](std::wstring_view id) { pages.Show(id); });
}

} // namespace gallery
