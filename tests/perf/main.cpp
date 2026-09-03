// perf — 帧耗时基准：典型界面（列表 + 按钮 + 文本）全帧重绘的平均耗时。
#include "lumen/lumen.h"
#include "core/offscreen.h"
#include "core/text_service.h"
#include <objbase.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace lumen;

namespace {

struct BenchRoot : StackPanel {
    using StackPanel::Measure;
    using StackPanel::Arrange;
};



double Mean(const std::vector<double>& values) {
    double sum = 0.0;
    for (double v : values) sum += v;
    return values.empty() ? 0.0 : sum / static_cast<double>(values.size());
}

} // namespace

int main() {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        std::printf("[FAIL] CoInitializeEx\n");
        return 1;
    }
    constexpr int kW = 1280, kH = 800, kFrames = 300;

    OffscreenRenderer renderer;
    if (!renderer.Init(kW, kH)) {
        std::printf("[FAIL] renderer init\n");
        return 1;
    }
    const Theme theme = MakeTheme();

    BenchRoot root;
    root.Padding(16.0f, 12.0f).Spacing(10.0f);
    auto& toolbar = root.Add<StackPanel>(StackPanel::Orientation::Horizontal);
    toolbar.Spacing(8.0f);
    for (int i = 0; i < 8; ++i) toolbar.Add<Button>(L"工具按钮");
    auto& list = root.Add<ListView>();
    list.ItemCount(100000);
    list.ItemText([](size_t i, std::wstring& s) { s = L"数据行 " + std::to_wstring(i); });
    list.ItemGlyph([](size_t i, std::wstring& s) {
        if ((i % 3) == 0) s = icon::kFolder;
    });
    list.SelectedIndex(5);
    // 聚光卡压力项：离屏 spotlight_t_ 到位，每帧绘制径向聚光 + 边缘折射光
    auto& spot = root.Add<StackPanel>();
    spot.Card(StackPanel::CardStyle::Lumen, 14.0f);
    spot.Padding(16.0f, 12.0f);
    spot.Add<Label>(L"BENTO 聚光卡 — 每帧径向渐变压力项", TextRole::BodyStrong);

    auto& charts = root.Add<Row>().Spacing(10.0f);
    charts.Add<Chart>()
        .Kind(ChartKind::Area)
        .Header(L"Area", L"n=32")
        .Values({12.f, 18.f, 14.f, 22.f, 19.f, 28.f, 24.f, 32.f, 27.f, 36.f, 30.f, 40.f,
                 34.f, 38.f, 29.f, 33.f, 26.f, 31.f, 28.f, 35.f, 32.f, 39.f, 36.f, 42.f,
                 38.f, 44.f, 40.f, 46.f, 41.f, 48.f, 43.f, 50.f})
        .PreferredSize({0.0f, 120.0f})
        .Grow();
    charts.Add<Chart>()
        .Kind(ChartKind::Heatmap)
        .Header(L"Heat", L"20×7")
        .Grid(20, 7)
        .Cell([](size_t x, size_t y) {
            return 0.35f + 0.65f * std::sin(static_cast<float>(x) * 0.4f + static_cast<float>(y));
        })
        .PreferredSize({0.0f, 120.0f})
        .Grow();

    root.Measure({static_cast<float>(kW), static_cast<float>(kH)}, theme);
    root.Arrange({0.0f, 0.0f, static_cast<float>(kW), static_cast<float>(kH)});

    ID2D1DeviceContext2* dc = renderer.BeginDraw();
    Painter painter;
    painter.BeginFrame(dc, &UiText(), 1.0f);

    // 预热（着色器/布局缓存）
    for (int i = 0; i < 30; ++i) {
        painter.FillRect({0, 0, kW, kH}, theme.bg);
        DrawControlTree(painter, theme, &root);
        if (!renderer.EndDraw()) { std::printf("[FAIL] warmup present\n"); return 1; }
        dc = renderer.BeginDraw();
        painter.BeginFrame(dc, &UiText(), 1.0f);
    }

    std::vector<double> frame_ms;
    frame_ms.reserve(kFrames);
    for (int i = 0; i < kFrames; ++i) {
        const auto start = std::chrono::steady_clock::now();
        painter.FillRect({0, 0, static_cast<float>(kW), static_cast<float>(kH)}, theme.bg);
        DrawControlTree(painter, theme, &root);
        const bool ok = renderer.EndDraw();
        const auto end = std::chrono::steady_clock::now();
        frame_ms.push_back(std::chrono::duration<double, std::milli>(end - start).count());
        if (!ok) { std::printf("[FAIL] present\n"); return 1; }
        dc = renderer.BeginDraw();
        painter.BeginFrame(dc, &UiText(), 1.0f);
    }

    renderer.Shutdown();
    const double avg = Mean(frame_ms);
    double worst = 0.0;
    for (double v : frame_ms) worst = std::max(worst, v);

    std::printf("场景：1280x800，8 按钮 + 100,000 行虚拟列表 + 聚光卡 + Area/Heatmap，全帧重绘 %d 帧\n",
                kFrames);
    std::printf("平均 %.3f ms/帧，最差 %.3f ms/帧\n", avg, worst);
    const bool pass = avg < 8.0;
    std::printf("%s perf_frame_budget (< 8 ms)\n", pass ? "[PASS]" : "[FAIL]");
    CoUninitialize();
    return pass ? 0 : 1;
}
