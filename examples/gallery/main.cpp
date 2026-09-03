// LUMEN gallery: NavigationView shell, one category page at a time.
#include "common.h"
#include "resources/resource.h"
#include <lumen/Main.h>
#include <sstream>

int lumen_main(std::span<const std::wstring_view>) {
    using namespace lumen;
    using namespace gallery;

    App app;
    Window window(L"LUMEN Gallery", {kWinW, 860.0f}, Frame::Client);
    window.MinSize({960.0f, 640.0f});
    window.Backdrop(Backdrop::All);
    window.Icon(IDR_LUMEN_GALLERY_ICO);
    window.BindShortcut(L"F12", [&window] {
        std::wostringstream out;
        window.DumpTree(out);
        DebugWrite(out.str());
    });

    auto& root = window.Root();
    auto& nav = root.Add<NavigationView>();
    BindShell(nav);
    nav.Grow().DisplayMode(NavigationDisplayMode::Auto).PaneLength(220.0f);
    nav.Items({
        {L"overview", L"Overview", icon::kHome},
        {L"", L"Basics", L"", NavigationItemType::Header},
        {L"buttons", L"Buttons", icon::kSparkle},
        {L"input", L"Input", icon::kKeyboard},
        {L"selection", L"Selection", icon::kCheckSquare},
        {L"", L"Structure", L"", NavigationItemType::Header},
        {L"layout", L"Layout", icon::kGrid},
        {L"collections", L"Collections", icon::kRows},
        {L"navigation", L"Navigation", icon::kMenu},
        {L"", L"Chrome", L"", NavigationItemType::Header},
        {L"overlays", L"Overlays", icon::kCards},
        {L"status", L"Status", icon::kInfo},
        {L"charts", L"Charts", icon::kChart},
    });
    nav.FooterItems({{L"github", L"GitHub", icon::kExternalLink, NavigationItemType::Item, true, {},
                      false, true}});
    nav.OnItemInvoked([](std::wstring_view id) {
        if (id == L"github") shell::OpenUrl(L"https://github.com/jimmgreen/LUMENUI");
    });

    auto& scroll = nav.Content().Add<ScrollViewer>();
    scroll.Grow();
    auto& host = scroll.Add<PageHost>();
    nav.BindPages(host);

    auto add_page = [&](std::wstring_view id) -> StackPanel& {
        auto& page = nav.Page(id);
        page.Padding(kPad, 20.0f).Spacing(kGap);
        return page;
    };

    BuildOverview(add_page(L"overview"), window);
    BuildButtons(add_page(L"buttons"), window);
    BuildInput(add_page(L"input"), window);
    BuildSelection(add_page(L"selection"), window);
    BuildLayout(add_page(L"layout"), window);
    BuildCollections(add_page(L"collections"), window);
    BuildNavigation(add_page(L"navigation"), window);
    BuildOverlays(add_page(L"overlays"), window);
    BuildStatus(add_page(L"status"), window);
    BuildCharts(add_page(L"charts"), window);

    nav.Navigate(L"overview");

    auto& status = root.Add<StatusBar>();
    status.Path(L"examples\\gallery").CountText(L"Ready").Zoom(L"100%");
    status.OnInvoked([&window, &status](std::wstring_view id) {
        if (id == L"zoom") {
            const std::wstring& cur = status.Zoom();
            const wchar_t* next = L"100%";
            if (cur == L"100%") next = L"150%";
            else if (cur == L"150%") next = L"75%";
            status.Zoom(next);
            window.ShowToast(next);
        } else if (id == L"path") {
            window.ShowToast(status.Path());
        } else {
            window.ShowToast(status.CountText());
        }
    });

    SetIntensity(window, 0.5f);
    window.Show();
    return app.Run();
}
