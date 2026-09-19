// Focus, disabled chrome and pagination affordance regressions; single dark theme.
#pragma once
#include "lumen/lumen.h"
#include "core/offscreen.h"
#include "core/text_service.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <vector>
#include <windows.h>

namespace control_usability {
using namespace lumen;

template<class T>
struct Probe : T {
    using T::Arrange;
    using T::Draw;
    using T::Measure;
    using T::OnFocusChanged;
    using T::OnKey;
    using T::OnMouseDown;
    using T::OnMouseMove;
    using T::OnMouseLeave;
    using T::CursorAt;
    using T::FocusVisible;
};

inline double Ink(const std::vector<uint8_t>& pixels, int width, float scale, Rect r) {
    if (pixels.empty()) return 0.0;
    const int height = static_cast<int>(pixels.size() / (static_cast<size_t>(width) * 4));
    const int x0 = std::max(0, static_cast<int>(std::floor(r.x * scale)));
    const int x1 = std::min(width, static_cast<int>(std::ceil(r.Right() * scale)));
    const int y0 = std::max(0, static_cast<int>(std::floor(r.y * scale)));
    const int y1 = std::min(height, static_cast<int>(std::ceil(r.Bottom() * scale)));
    double sum = 0.0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x)
            sum += pixels[(static_cast<size_t>(y) * width + x) * 4];
    return sum;
}

inline void Run(void (*check)(bool, const char*)) {
    for (float scale : {1.0f, 1.25f, 1.5f, 2.0f}) {
        OffscreenRenderer target;
        if (!target.Init(static_cast<int>(640 * scale), static_cast<int>(320 * scale))) {
            check(false, "usability renderer init");
            return;
        }
        for (float intensity : {0.0f, 0.5f, 1.0f}) {
            std::printf("usability matrix scale=%.2f glow=%.1f\n", scale, intensity);
            const Theme theme = MakeTheme(intensity);
            Probe<Segmented> segments;
            segments.AddItem(L"Overview");
            segments.AddItem(L"Details");
            segments.SelectedIndex(1);
            Probe<Pagination> pager;
            pager.PageCount(12).Current(5);
            Probe<Stepper> steps;
            steps.AddStep(L"Setup").AddStep(L"Review").AddStep(L"Done").Current(1);
            Probe<Rating> rating;
            rating.Value(3.0);
            Probe<RadioButton> radio;
            std::array<Control*, 5> controls{&segments, &pager, &steps, &rating, &radio};
            std::array<Rect, 5> bounds{};
            auto layout = [&](auto& control, size_t row) {
                const Size size = control.Measure({600.0f, 50.0f}, theme);
                bounds[row] = {24.0f, 20.0f + 56.0f * static_cast<float>(row), size.w, size.h};
                control.Arrange(bounds[row]);
            };
            layout(segments, 0); layout(pager, 1); layout(steps, 2);
            layout(rating, 3); layout(radio, 4);
            auto focus = [&](bool value) {
                segments.OnFocusChanged(value);
                pager.OnFocusChanged(value);
                steps.OnFocusChanged(value);
            };
            auto render = [&](std::vector<uint8_t>& pixels) {
                Painter painter;
                auto* dc = target.BeginDraw();
                painter.BeginFrame(dc, &UiText(), scale);
                painter.FillRect({0, 0, 640, 320}, theme.bg);
                segments.Draw(painter, theme); pager.Draw(painter, theme);
                steps.Draw(painter, theme); rating.Draw(painter, theme); radio.Draw(painter, theme);
                painter.EndFrame();
                check(target.EndDraw() && target.ReadBack(pixels), "usability frame readback");
            };
            std::vector<uint8_t> normal, focused, disabled, disabled_hot, restored;
            render(normal);
            focus(true);
            render(focused);
            const char* focus_names[] = {"usability segmented visible keyboard focus",
                                        "usability pagination visible keyboard focus",
                                        "usability stepper visible keyboard focus"};
            for (size_t i = 0; i < 3; ++i) {
                const Rect top{bounds[i].x + 8, bounds[i].y - 4, bounds[i].w - 16, 3};
                check(Ink(focused, target.Width(), scale, top) >
                          Ink(normal, target.Width(), scale, top) + 100,
                      focus_names[i]);
            }
            if (scale == 1.5f && intensity == 0.0f)
                check(target.SavePNG(L"lumen_visual_usability_focus_150.png"), "save usability focus states");
            focus(false);
            for (Control* c : controls) c->Enabled(false);
            render(disabled);
            const char* disabled_names[] = {"usability segmented disabled is dimmer",
                "usability pagination disabled is dimmer", "usability stepper disabled is dimmer",
                "usability rating disabled is dimmer", "usability radio disabled is dimmer"};
            for (size_t i = 0; i < controls.size(); ++i)
                check(Ink(disabled, target.Width(), scale, bounds[i]) <
                          Ink(normal, target.Width(), scale, bounds[i]) * 0.95,
                      disabled_names[i]);
            // Disabled controls must not paint stale/local hover or focus chrome.
            focus(true);
            segments.OnMouseMove({20, 14}, 0); pager.OnMouseMove({20, 14}, 0);
            steps.OnMouseMove({20, 14}, 0); rating.OnMouseMove({20, 14}, 0);
            render(disabled_hot);
            check(disabled == disabled_hot, "usability disabled ignores hover and focus chrome");
            if (scale == 1.5f && intensity == 0.0f)
                check(target.SavePNG(L"lumen_visual_usability_disabled_150.png"), "save usability disabled states");
            focus(false);
            segments.OnMouseLeave(); pager.OnMouseLeave(); steps.OnMouseLeave(); rating.OnMouseLeave();
            for (Control* c : controls) c->Enabled(true);
            render(restored);
            check(normal == restored, "usability re-enable restores unchanged values and appearance");
            rating.ReadOnly(true);
            render(restored);
            check(normal == restored, "usability read-only rating keeps readable normal appearance");
            // The radio has no caption: all ink must come from its basic circle, even at glow=0.
            check(Ink(normal, target.Width(), scale, bounds[4]) > 300 * scale * scale,
                  "usability unchecked radio boundary survives zero glow");
        }
    }

    Probe<Pagination> pager;
    const Theme theme = MakeTheme(0.0f);
    auto layout = [&] {
        const Size size = pager.Measure({600, 40}, theme);
        pager.Arrange({24, 16, size.w, size.h});
        return size;
    };
    int events = 0;
    size_t seen = 0;
    pager.OnNavigate([&](size_t page) { ++events; seen = page; });
    pager.PageCount(12).Current(1);
    Size size = layout();
    const Point prev{20, 14}, current{56, 14};
    Point next{size.w - 20, 14};
    check(pager.CursorAt(prev) == CursorShape::Arrow, "usability first-page previous cursor inert");
    check(pager.CursorAt(current) == CursorShape::Arrow, "usability current-page cursor inert");
    check(pager.CursorAt(next) == CursorShape::Hand, "usability available next cursor actionable");
    pager.OnMouseDown(prev, 1); pager.OnMouseDown(current, 1); pager.OnKey(VK_LEFT);
    check(events == 0 && pager.Current() == 1, "usability boundary and current page do not notify");
    pager.OnMouseDown(next, 1);
    check(events == 1 && seen == 2 && pager.Current() == 2, "usability next page notifies exactly once");
    pager.Current(12); size = layout(); next = {size.w - 20, 14};
    check(pager.CursorAt(next) == CursorShape::Arrow && pager.CursorAt(prev) == CursorShape::Hand,
          "usability last-page arrow cursors reflect availability");
    pager.OnMouseDown(next, 1); pager.OnKey(VK_RIGHT);
    check(events == 1 && pager.Current() == 12, "usability last-page boundary and setters stay silent");
    pager.Current(5); layout();
    // Middle-page layout: prev, 1, ellipsis, 4, 5, 6, ellipsis, 12, next.
    const Point ellipsis{84, 14}, page4{112, 14};
    check(pager.CursorAt(ellipsis) == CursorShape::Arrow && pager.CursorAt(page4) == CursorShape::Hand,
          "usability ellipsis inert but adjacent page actionable");
    pager.OnMouseDown(ellipsis, 1);
    check(events == 1 && pager.Current() == 5, "usability ellipsis does not navigate");
    pager.OnMouseDown(page4, 1);
    check(events == 2 && seen == 4, "usability numbered page notifies exactly once");
    pager.Enabled(false);
    check(pager.CursorAt(prev) == CursorShape::Arrow && pager.CursorAt(page4) == CursorShape::Arrow,
          "usability disabled pagination cursors inert");
    pager.Enabled(true).PageCount(1); size = layout(); next = {size.w - 20, 14};
    check(pager.CursorAt(prev) == CursorShape::Arrow && pager.CursorAt(next) == CursorShape::Arrow,
          "usability single-page arrows inert");

    // Pixel equality catches misleading hover fills even when click callbacks are already silent.
    OffscreenRenderer target;
    if (!target.Init(640, 64)) { check(false, "usability pager renderer"); return; }
    auto capture = [&](std::vector<uint8_t>& pixels) {
        Painter painter;
        painter.BeginFrame(target.BeginDraw(), &UiText(), 1.0f);
        painter.FillRect({0, 0, 640, 64}, theme.bg);
        pager.Draw(painter, theme);
        painter.EndFrame();
        check(target.EndDraw() && target.ReadBack(pixels), "usability pager readback");
    };
    for (size_t page : {size_t{1}, size_t{12}}) {
        pager.PageCount(12).Current(page); size = layout(); pager.OnMouseLeave();
        std::vector<uint8_t> neutral, hovered;
        capture(neutral);
        pager.OnMouseMove(page == 1 ? prev : Point{size.w - 20, 14}, 0);
        capture(hovered);
        check(neutral == hovered, "usability boundary arrow has no clickable hover fill");
    }
    pager.Current(5); layout(); pager.OnMouseLeave();
    std::vector<uint8_t> neutral, hovered;
    capture(neutral); pager.OnMouseMove(ellipsis, 0); capture(hovered);
    check(neutral == hovered, "usability ellipsis has no clickable hover fill");
    pager.OnMouseMove(page4, 0); capture(hovered);
    check(neutral != hovered, "usability available page retains hover feedback");

    // Actual shared input routing: keyboard modality versus mouse modality, no new public API.
    Window window(L"usability-focus", {640, 180});
    window.Motion(MotionMode::Off);
    auto& row = window.Root().Add<Row>();
    auto& seg = row.Add<Probe<Segmented>>();
    seg.AddItem(L"A"); seg.AddItem(L"B");
    auto& pages = row.Add<Probe<Pagination>>(); pages.PageCount(3);
    auto& step = window.Root().Add<Probe<Stepper>>();
    step.AddStep(L"A").AddStep(L"B").Current(1);
    window.LayoutNow();
    window.DispatchKey(VK_TAB);
    check(window.Focused() == &seg && seg.FocusVisible(), "usability Tab enters segmented visibly");
    window.DispatchKey(VK_TAB);
    check(window.Focused() == &pages && pages.FocusVisible(), "usability Tab enters pagination visibly");
    window.DispatchKey(VK_TAB);
    check(window.Focused() == &step && step.FocusVisible(), "usability Tab enters stepper visibly");
    const Rect bounds = pages.AbsoluteBounds();
    const Point point{bounds.x + 20, bounds.y + bounds.h * 0.5f};
    window.DispatchMouseMove(point); window.DispatchMouseDown(point); window.DispatchMouseUp(point);
    check(window.Focused() == &pages && !pages.FocusVisible(), "usability mouse focus does not request keyboard ring");
    int routed = 0;
    pages.OnNavigate([&](size_t) { ++routed; });
    pages.Enabled(false);
    window.DispatchKey(VK_RIGHT);
    check(routed == 0 && pages.Current() == 1, "usability disabled pager rejects routed keyboard navigation");
}
} // namespace control_usability
