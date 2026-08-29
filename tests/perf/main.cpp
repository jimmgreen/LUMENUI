// perf — 帧耗时基准：典型界面（列表 + 按钮 + 文本）全帧重绘的平均耗时。
#include "fluentui/fluentui.h"
#include "core/offscreen.h"
#include "core/text_service.h"
#include <objbase.h>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace fui;

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
    const Theme theme = MakeTheme(true, Color::Hex(0x0078D4));

    BenchRoot root;
    root.Padding(16.0f, 12.0f).Spacing(10.0f);
    auto& toolbar = root.Add<StackPanel>(StackPanel::Orientation::Horizontal);
    toolbar.Spacing(8.0f);
    for (int i = 0; i < 8; ++i) toolbar.Add<Button>(L"工具按钮");
    auto& list = root.Add<ListView>();
    list.SetItemCount(100000);
    list.ItemText([](size_t i) { return L"数据行 " + std::to_wstring(i); });
    list.ItemGlyph([](size_t i) { return (i % 3) == 0 ? icon::kFolder : std::wstring(); });
    list.SetSelectedIndex(5);

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

    std::printf("场景：1280x800，8 按钮 + 100,000 行虚拟列表，全帧重绘 %d 帧\n", kFrames);
    std::printf("平均 %.3f ms/帧，最差 %.3f ms/帧\n", avg, worst);
    const bool pass = avg < 8.0;
    std::printf("%s perf_frame_budget (< 8 ms)\n", pass ? "[PASS]" : "[FAIL]");
    CoUninitialize();
    return pass ? 0 : 1;
}
