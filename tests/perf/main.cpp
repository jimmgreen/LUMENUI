// perf — 帧耗时基准：典型界面（列表 + 按钮 + 文本）全帧重绘的平均耗时。
#include "lumen/lumen.h"
#include "core/offscreen.h"
#include "core/text_service.h"
#include "core/lumatext_bridge.h"
#include <objbase.h>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdlib>
#include <new>

namespace allocation_probe {
thread_local bool enabled = false;
thread_local size_t count = 0, bytes = 0;
void Record(size_t n) { if (enabled) { ++count; bytes += n; } }
}
void* operator new(size_t n) {
    if (void* p = std::malloc(n ? n : 1)) { allocation_probe::Record(n); return p; }
    throw std::bad_alloc();
}
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }
void operator delete[](void* p, size_t) noexcept { std::free(p); }

using namespace lumen;

namespace {

struct BenchRoot : StackPanel {
    using StackPanel::Measure;
    using StackPanel::Arrange;
};



struct BenchTable : Table { using Table::Measure; using Table::Arrange; using Table::Prepare; using Table::Draw; using Table::OnMouseMove; };

bool TableAllocationBench(OffscreenRenderer& renderer, Painter& painter, const Theme& theme) {
    BenchTable table;
    table.AddColumn(L"Engineering identifier and mixed text", 520);
    table.AddColumn(L"Amount", 180);
    table.AddColumn(L"Progress", 240);
    table.RowCount(10000).CellCharacterFont(L"AB", L"Segoe UI");
    table.CellText([](size_t row, size_t, std::wstring& out) {
        out = L"AB engineering symbol — long description "; out += std::to_wstring(row);
    });
    size_t number_reads = 0;
    table.BindNumber(1, [&](size_t row) { ++number_reads; return static_cast<double>(row) / 7; });
    float progress = 0;
    table.BindProgress(2, [&](size_t) { return progress; });
    table.Footer(true).Aggregate(1, ColumnAggregate::Sum);
    table.Measure({1280, 800}, theme); table.Arrange({0, 0, 1280, 800});
    bool pass = true;
    for (int phase = 0; phase < 5; ++phase) {
        const char* name[] = {"cold", "static", "hover", "scroll", "progress"};
        size_t draws = 0, draw_bytes = 0, prepares = 0;
        std::vector<double> times;
        const int frames = phase == 0 ? 1 : 60;
        times.reserve(frames);
        for (int i = 0; i < frames; ++i) {
            if (phase == 2) table.OnMouseMove({80, 60.0f + static_cast<float>(i % 16) * 28}, 0);
            if (phase == 3) table.ScrollTo(static_cast<size_t>(i * 5));
            if (phase == 4) { progress = static_cast<float>(i) / 60; table.RefreshRows(0, 1); }
            allocation_probe::count = allocation_probe::bytes = 0;
            number_reads = 0;
            allocation_probe::enabled = true;
            table.Prepare(painter, theme);
            allocation_probe::enabled = false;
            if (phase == 4 && number_reads > 100) { std::printf("[FAIL] incremental footer scanned whole model\n"); pass = false; }
            prepares += allocation_probe::count;
            allocation_probe::count = allocation_probe::bytes = 0;
            const auto start = std::chrono::steady_clock::now();
            allocation_probe::enabled = true;
            table.Draw(painter, theme);
            allocation_probe::enabled = false;
            times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
            draws += allocation_probe::count; draw_bytes += allocation_probe::bytes;
            if (!renderer.EndDraw()) return false;
            painter.BeginFrame(renderer.BeginDraw(), &UiText(), 1);
        }
        std::sort(times.begin(), times.end());
        const auto percentile = [&](double p) { return times[static_cast<size_t>(p * static_cast<double>(times.size() - 1))]; };
        std::printf("table %s frames=%d Draw p50=%.3f p95=%.3f p99=%.3f max=%.3f ms allocations=%zu bytes=%zu prepare_allocations=%zu\n",
            name[phase], frames, percentile(.5), percentile(.95), percentile(.99), times.back(), draws, draw_bytes, prepares);
        if (draws) pass = false;
    }
    std::printf("%s table Draw C++ allocation boundary\n", pass ? "[PASS]" : "[FAIL]");
    return pass;
}

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

    std::printf("table backend=DirectWrite, DPI=96, rows=10000, build=Release; counts cover C++ new/new[], not driver/COM internals\n");
    bool table_pass = TableAllocationBench(renderer, painter, theme);
#if defined(LUMEN_HAS_LUMATEXT)
    LumaTextBridge bridge;
    const bool ready = bridge.Init(UiText().Factory(), dc);
    if (ready) {
        painter.SetLumaText(&bridge);
        std::printf("table backend=LumaText, DPI=96, rows=10000, build=Release\n");
        table_pass = TableAllocationBench(renderer, painter, theme) && table_pass;
        painter.SetLumaText(nullptr);
        bridge.Shutdown();
    } else { std::printf("[FAIL] LumaText benchmark initialization\n"); table_pass = false; }
#endif
    renderer.Shutdown();
    const double avg = Mean(frame_ms);
    double worst = 0.0;
    for (double v : frame_ms) worst = std::max(worst, v);

    std::printf("场景：1280x800，8 按钮 + 100,000 行虚拟列表 + 聚光卡 + Area/Heatmap，全帧重绘 %d 帧\n",
                kFrames);
    std::printf("平均 %.3f ms/帧，最差 %.3f ms/帧\n", avg, worst);
    const bool pass = avg < 8.0 && table_pass;
    std::printf("%s perf_frame_budget (< 8 ms)\n", pass ? "[PASS]" : "[FAIL]");
    CoUninitialize();
    return pass ? 0 : 1;
}
