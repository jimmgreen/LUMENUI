#include "common.h"
#include <cmath>
#include <string>

namespace gallery {

void BuildCharts(lumen::StackPanel& column, lumen::Window&) {
    using namespace lumen;
    PageHead(column, L"Charts",
             L"Crosshair callouts, clickable legends, value tweens, wheel-zoom and drag-pan.");

    auto& line_card = Sample(column, L"Line / Area",
                             L"Hover for crosshair. Click a legend to hide a series. Wheel zooms, "
                             L"drag pans, double-click resets. Shuffle tweens the line.");
    auto& line_row = line_card.Add<Row>().Spacing(16.0f);
    auto& line = line_row.Add<Chart>()
        .Kind(ChartKind::Line)
        .Header(L"Spline Dynamics", L"38")
        .Hint(L"Active vs baseline")
        .Values({20.f, 32.f, 38.f, 45.f, 52.f, 48.f})
        .Baseline({18.f, 24.f, 29.f, 40.f, 44.f, 42.f})
        .SeriesName(L"Active")
        .BaselineName(L"Baseline")
        .XLabels({L"Jan", L"Feb", L"Mar", L"Apr", L"May", L"Jun"})
        .PreferredSize({0.0f, 200.0f})
        .Grow();
    line_row.Add<Chart>()
        .Kind(ChartKind::Area)
        .Header(L"Curved Wave", L"48%")
        .Hint(L"Soft fill · linear flow")
        .Values({8.f, 14.f, 12.f, 22.f, 18.f, 30.f, 24.f, 36.f, 28.f, 40.f, 32.f, 38.f})
        .SeriesName(L"Wave")
        .PreferredSize({0.0f, 200.0f})
        .Grow();
    line_card.Add<Row>().Add<Button>(L"Shuffle", ButtonKind::Subtle).SizeClass(ButtonSize::Small)
        .OnClick([&line] {
            static int n = 0;
            ++n;
            if (n % 2) {
                line.Values({28.f, 18.f, 54.f, 32.f, 60.f, 36.f});
            } else {
                line.Values({20.f, 32.f, 38.f, 45.f, 52.f, 48.f});
            }
        });

    auto& bar_card = Sample(column, L"Bar / Pyramid",
                            L"Hover a bar for a callout. Wheel and drag snap to whole days; "
                            L"double-click resets.");
    auto& bar_row = bar_card.Add<Row>().Spacing(16.0f);
    bar_row.Add<Chart>()
        .Kind(ChartKind::Bar)
        .Header(L"Pill Pillars", L"422")
        .Values({22.f, 38.f, 28.f, 54.f, 44.f, 62.f, 36.f})
        .XLabels({L"Mon", L"Tue", L"Wed", L"Thu", L"Fri", L"Sat", L"Sun"})
        .PreferredSize({0.0f, 200.0f})
        .Grow();
    bar_row.Add<Chart>()
        .Kind(ChartKind::Funnel)
        .Header(L"Org Pyramid", L"4 Tiers")
        .Values({0.32f, 0.48f, 0.68f, 0.92f})
        .XLabels({L"Executive", L"Management", L"Senior Staff", L"Core Team"})
        .PreferredSize({0.0f, 200.0f})
        .Grow();

    auto& ring_card = Sample(column, L"Donut / Radar",
                             L"Click a donut legend to hide a slice. Radar hover callout.");
    auto& ring_row = ring_card.Add<Row>().Spacing(16.0f);
    ring_row.Add<Chart>()
        .Kind(ChartKind::Donut)
        .Header(L"Allocation", L"100%")
        .Slices({{L"Core", 0.42f}, {L"UI", 0.28f}, {L"Assets", 0.18f}, {L"Other", 0.12f}})
        .PreferredSize({0.0f, 210.0f})
        .Grow();
    ring_row.Add<Chart>()
        .Kind(ChartKind::Radar)
        .Header(L"Polygon Web", L"85")
        .Values({0.72f, 0.58f, 0.64f, 0.85f, 0.48f})
        .XLabels({L"Speed", L"IOPS", L"Latency", L"Scale", L"Memory"})
        .PreferredSize({0.0f, 210.0f})
        .Grow();

    auto& heat_card = Sample(column, L"Heatmap / Bullet",
                             L"Hover a cell for value callout. Bullet rows show actual / target.");
    auto& heat_row = heat_card.Add<Row>().Spacing(16.0f);
    heat_row.Add<Chart>()
        .Kind(ChartKind::Heatmap)
        .Header(L"Activity", L"753")
        .Hint(L"20 weeks × 7 days")
        .Grid(20, 7)
        .Cell([](size_t x, size_t y) {
            const float wave = 0.5f + 0.5f * std::sin(static_cast<float>(x) * 0.41f +
                                                      static_cast<float>(y) * 0.9f);
            return ((x + y * 3) % 11 == 0) ? 0.0f : wave;
        })
        .PreferredSize({0.0f, 200.0f})
        .Grow();
    heat_row.Add<Chart>()
        .Kind(ChartKind::Bullet)
        .Header(L"Bullet Target", L"3 Targets")
        .Hint(L"Rounded bars · marker")
        .Values({0.82f, 0.65f, 0.95f})
        .Targets({0.75f, 0.80f, 0.90f})
        .XLabels({L"Throughput", L"Latency", L"Uptime"})
        .PreferredSize({0.0f, 200.0f})
        .Grow();
}

}  // namespace gallery
