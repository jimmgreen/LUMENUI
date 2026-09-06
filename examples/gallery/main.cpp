// LUMEN gallery: NavigationView shell, one category page at a time.
#include "common.h"
#include "resources/resource.h"
#include <lumen/Main.h>
#include <sstream>
#include <string>

int lumen_main(std::span<const std::wstring_view> args) {
    using namespace lumen;
    using namespace gallery;

    std::wstring start_page = L"overview";
    bool sim_print = false;
    int quit_ms = 0;
    bool skip_exe = true;
    for (const std::wstring_view arg : args) {
        if (skip_exe) {
            skip_exe = false;
            continue;
        }
        if (arg == L"--sim-print") {
            sim_print = true;
            continue;
        }
        if (arg.size() > 10 && arg.substr(0, 10) == L"--quit-ms=") {
            quit_ms = std::stoi(std::wstring(arg.substr(10)));
            continue;
        }
        if (!arg.empty() && arg[0] != L'-') start_page = std::wstring(arg);
    }

    App app;
    Window window(L"LUMEN Gallery", {kWinW, 860.0f}, Frame::Client);
    window.MinSize({960.0f, 640.0f});
    window.Backdrop(Backdrop::All);
    window.PerfHud(true);
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

    nav.Navigate(start_page);
    if (sim_print) StartPlotLiveDemo(window);
    Connection quit_frame;
    int quit_frames = 0;
    if (quit_ms > 0) {
        const int quit_need = quit_ms < 16 ? 1 : quit_ms / 16;
        quit_frame = window.OnFrame([&](float) {
            ++quit_frames;
            if (quit_frames < quit_need) return true;
            App::Quit(0);
            return false;
        });
    }

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
