// anim — 缓动/补间/弹簧数值断言 + 曲线 PNG；`--live` 与 gallery 同一 vsync 时钟对照。
#include "lumen/lumen.h"
#include "core/offscreen.h"
#include "core/text_service.h"
#include <objbase.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

using namespace lumen;

namespace {

int g_failures = 0;

void Check(bool condition, const char* name) {
    std::printf("%s %s\n", condition ? "[PASS]" : "[FAIL]", name);
    if (!condition) ++g_failures;
}

bool Close(float a, float b, float tol = 1.0e-4f) {
    return std::fabs(a - b) <= tol;
}

const Ease kEases[] = {
    Ease::Linear,     Ease::CssEase,     Ease::CssEaseIn, Ease::CssEaseOut,
    Ease::CssEaseInOut, Ease::Material,  Ease::InQuad,    Ease::OutQuad,
    Ease::InOutQuad,  Ease::InCubic,     Ease::OutCubic,  Ease::InOutCubic,
    Ease::InSine,     Ease::OutSine,     Ease::InOutSine, Ease::InExpo,
    Ease::OutExpo,    Ease::InOutExpo,   Ease::OutBack,   Ease::Bezier,
};

bool Monotone(Ease ease) {
    if (ease == Ease::OutBack) return true;
    float prev = EaseAt(0.0f, ease);
    for (int i = 1; i <= 24; ++i) {
        const float y = EaseAt(static_cast<float>(i) / 24.0f, ease);
        if (y + 1.0e-3f < prev) return false;
        prev = y;
    }
    return true;
}

void RunMath() {
    int endpoint_fail = 0;
    int monotone_fail = 0;
    for (Ease ease : kEases) {
        if (!Close(EaseAt(0.0f, ease), 0.0f, 1.0e-5f) ||
            !Close(EaseAt(1.0f, ease), 1.0f, 1.0e-5f)) {
            ++endpoint_fail;
        }
        if (!Monotone(ease)) ++monotone_fail;
    }
    Check(endpoint_fail == 0, "ease endpoints");
    Check(monotone_fail == 0, "ease monotone");

    Check(Close(EaseAt(0.3f, Ease::Linear), 0.3f), "linear sample");
    Check(Close(EaseAt(0.5f, Ease::InCubic), 0.125f), "in-cubic 0.5");
    Check(Close(EaseAt(0.5f, Ease::InOutCubic), 0.5f), "in-out-cubic 0.5");
    Check(Close(EaseAt(0.25f, Ease::InOutCubic), 0.0625f), "in-out-cubic 0.25");
    Check(Close(EaseAt(0.5f, Ease::CssEaseInOut), 0.5f, 0.02f), "css ease-in-out mid");
    Check(Close(EaseAt(0.37f, CubicBezier::Linear()), 0.37f, 0.002f), "bezier identity");
    Check(Close(EaseAt(0.5f, BezierFor(Ease::CssEaseInOut)), EaseAt(0.5f, Ease::CssEaseInOut),
                1.0e-5f),
          "bezier-for matches named");

    float exp = 0.0f;
    EaseTo(exp, 1.0f, 1.0f / 12.0f, 12.0f);
    Check(Close(exp, 1.0f - std::exp(-1.0f), 1.0e-4f), "exp ease-to");

    Tween tween;
    tween.Play(10.0f, 40.0f, 0.3f, Ease::InOutCubic);
    Check(Close(tween.Value(), 10.0f), "tween start");
    tween.Tick(0.15f);
    Check(Close(tween.T(), 0.5f) && Close(tween.Value(), 25.0f), "tween mid cubic");
    Check(tween.Tick(1.0f) == false && Close(tween.Value(), 40.0f), "tween end");

    Tween zero;
    zero.Play(1.0f, 9.0f, 0.0f, Ease::Linear);
    Check(!zero.running && Close(zero.Value(), 9.0f), "tween zero duration snaps");

    Tween jitter;
    jitter.Play(0.0f, 1.0f, 0.3f, Ease::Material);
    const float dts[] = {0.008f, 0.024f, 0.016f, 0.033f, 0.012f, 0.020f,
                         0.016f, 0.040f,  0.016f, 0.05f,  0.1f};
    bool alive = true;
    for (float dt : dts) {
        alive = jitter.Tick(dt);
        if (!alive) break;
    }
    while (alive) alive = jitter.Tick(0.016f);
    Check(!jitter.running && Close(jitter.Value(), 1.0f, 1.0e-5f), "tween jitter settles");

    Tween bezier_tw;
    bezier_tw.PlayBezier(0.0f, 1.0f, 0.2f, CubicBezier::Material());
    bezier_tw.Tick(0.1f);
    Check(Close(bezier_tw.Value(), EaseAt(0.5f, Ease::Material), 1.0e-4f), "play bezier");

    SpringMotion critical;
    critical.Snap(0.0f);
    bool moving = true;
    float peak = 0.0f;
    bool finite = true;
    for (int i = 0; i < 240 && moving; ++i) {
        moving = critical.Tick(1.0f, 0.016f, Spring::Critical());
        peak = std::max(peak, critical.value);
        if (std::isnan(critical.value)) finite = false;
    }
    Check(finite, "spring finite");
    Check(!moving && Close(critical.value, 1.0f, 1.0e-3f), "critical spring rest");
    Check(peak <= 1.01f, "critical spring no bounce");

    SpringMotion snappy;
    snappy.Snap(0.0f);
    moving = true;
    for (int i = 0; i < 240 && moving; ++i) {
        moving = snappy.Tick(1.0f, 0.016f, Spring::Snappy());
    }
    Check(!moving && Close(snappy.value, 1.0f, 2.0e-3f), "snappy spring rest");

    SpringMotion filament;
    filament.value = 1.0f;
    filament.velocity = 8.0f;
    float filament_peak = 1.0f;
    moving = true;
    for (int i = 0; i < 240 && moving; ++i) {
        moving = filament.Tick(1.0f, 0.016f, Spring::Snappy());
        filament_peak = std::max(filament_peak, filament.value);
    }
    Check(filament_peak > 1.04f && filament_peak < 1.45f, "spotlight spring overshoot");
    Check(!moving && Close(filament.value, 1.0f, 0.01f), "spotlight spring settle");

    Tween pulse;
    pulse.Play(0.0f, 1.0f, 0.32f, Ease::CssEaseOut);
    Check(pulse.Tick(0.16f), "press pulse still running at mid");
    Check(pulse.Value() > 0.4f, "press pulse mid value");
    Check(pulse.Tick(0.20f) == false && Close(pulse.Value(), 1.0f), "press pulse ends");
    Check(pulse.running == false, "press pulse not idle-spinning");
}

void DrawCurve(Painter& painter, const Rect& plot, Ease ease, Color color) {
    painter.StrokeRoundedRect(plot, 4.0f, Color{1, 1, 1, 0.12f});
    Point prev{};
    for (int i = 0; i <= 64; ++i) {
        const float t = static_cast<float>(i) / 64.0f;
        const float y = Clamp(EaseAt(t, ease), -0.15f, 1.15f);
        const Point p{plot.x + t * plot.w,
                      plot.y + plot.h - (y + 0.15f) / 1.3f * plot.h};
        if (i > 0) painter.DrawLine(prev, p, color, 1.4f);
        prev = p;
    }
}

void RenderSheet(const wchar_t* path) {
    constexpr float kW = 960.0f;
    constexpr float kH = 820.0f;
    OffscreenRenderer renderer;
    if (!renderer.Init(static_cast<int>(kW), static_cast<int>(kH))) {
        Check(false, "renderer init");
        return;
    }
    const Theme theme = MakeTheme();
    ID2D1DeviceContext2* dc = renderer.BeginDraw();
    Painter painter;
    painter.BeginFrame(dc, &UiText(), 1.0f);
    painter.FillRect({0, 0, kW, kH}, theme.bg);
    painter.DrawText(L"LUMEN Animate", {24, 16, 400, 28}, TextRole::Title, theme.text);

    struct Row {
        const wchar_t* name;
        Ease ease;
    };
    const Row rows[] = {
        {L"Linear", Ease::Linear},
        {L"CSS ease-in-out", Ease::CssEaseInOut},
        {L"InOutCubic", Ease::InOutCubic},
        {L"OutCubic", Ease::OutCubic},
        {L"Material", Ease::Material},
        {L"OutBack", Ease::OutBack},
    };

    float y = 56.0f;
    for (const Row& row : rows) {
        painter.DrawText(row.name, {24, y, 180, 20}, TextRole::Caption, theme.text_secondary);
        const Rect plot{220.0f, y, 700.0f, 88.0f};
        DrawCurve(painter, plot, row.ease, theme.accent);
        y += 104.0f;
    }

    painter.DrawText(L"InOutCubic samples", {24, y, 220, 20}, TextRole::Caption,
                     theme.text_secondary);
    const Rect strip{220.0f, y, 700.0f, 28.0f};
    painter.FillRoundedRect(strip, 8.0f, theme.fill_hover);
    float pill0_x = 0.0f, pill1_x = 0.0f, pill_mid_x = 0.0f;
    for (int i = 0; i <= 4; ++i) {
        const float t = static_cast<float>(i) / 4.0f;
        const float u = EaseAt(t, Ease::InOutCubic);
        const float pill_w = 28.0f;
        const float x = strip.x + 6.0f + u * (strip.w - pill_w - 12.0f);
        painter.FillRoundedRect({x, strip.y + 4.0f, pill_w, 20.0f}, 6.0f, theme.accent);
        if (i == 0) pill0_x = x + pill_w * 0.5f;
        if (i == 2) pill_mid_x = x + pill_w * 0.5f;
        if (i == 4) pill1_x = x + pill_w * 0.5f;
    }

    painter.EndFrame();
    Check(renderer.EndDraw(), "enddraw");

    Color bg{};
    Check(renderer.ReadPixel(4, 4, bg) && bg.r < 0.05f && bg.g < 0.05f && bg.b < 0.05f,
          "sheet bg black");

    Color left{}, right{};
    const int sy = static_cast<int>(strip.y + 14.0f);
    Check(renderer.ReadPixel(static_cast<int>(pill0_x), sy, left) && left.r > 0.7f,
          "sample t=0 left");
    Check(renderer.ReadPixel(static_cast<int>(pill1_x), sy, right) && right.r > 0.7f,
          "sample t=1 right");
    Check(std::fabs(pill_mid_x - (strip.x + strip.w * 0.5f)) < 8.0f, "sample t=0.5 center");

    Check(renderer.SavePNG(path), "save png");
    renderer.Shutdown();
}

class AnimTrack : public Control {
public:
    enum class Kind { Tween, Spring };

    AnimTrack(std::wstring title, Ease ease, float seconds)
        : title_(std::move(title)), kind_(Kind::Tween) {
        tween_.Play(0.0f, 1.0f, seconds, ease);
    }
    AnimTrack(std::wstring title, Spring spring)
        : title_(std::move(title)), kind_(Kind::Spring), spring_cfg_(spring) {
        spring_.Snap(0.0f);
        spring_target_ = 1.0f;
    }

    void Start() { Animate(); }

protected:
    Size Measure(Size available, const Theme&) override { return {available.w, 52.0f}; }

    void Draw(Painter& painter, const Theme& theme) override {
        painter.DrawText(title_, {absolute_.x, absolute_.y, absolute_.w, 18.0f}, TextRole::Caption,
                         theme.text_secondary);
        const Rect track{absolute_.x, absolute_.y + 22.0f, absolute_.w, 24.0f};
        painter.FillRoundedRect(track, 12.0f, theme.fill_hover);
        const float u = kind_ == Kind::Tween ? tween_.Value() : spring_.value;
        const float pill_w = 40.0f;
        const float x = track.x + 4.0f + Clamp(u, 0.0f, 1.0f) * (track.w - pill_w - 8.0f);
        painter.FillRoundedRect({x, track.y + 3.0f, pill_w, 18.0f}, 9.0f, theme.accent);
    }

    bool OnAnimate(float dt) override {
        if (kind_ == Kind::Tween) {
            if (!tween_.Tick(dt)) {
                tween_.Play(tween_.to, tween_.from, tween_.duration, tween_.ease);
            }
        } else if (!spring_.Tick(spring_target_, dt, spring_cfg_)) {
            spring_target_ = spring_target_ > 0.5f ? 0.0f : 1.0f;
        }
        Invalidate();
        return true;
    }

    std::wstring title_;
    Kind kind_ = Kind::Tween;
    Tween tween_{};
    SpringMotion spring_{};
    Spring spring_cfg_ = Spring::Smooth();
    float spring_target_ = 1.0f;
};

int RunLive() {
    App app;
    Window window(L"LUMEN animate lab", {720.0f, 720.0f});
    window.MinSize({560.0f, 560.0f});
    window.Backdrop(Backdrop::All);
    window.OnClosing([] {
        App::Quit(0);
        return true;
    });

    auto& root = window.Root();
    root.Padding(24.0f, 20.0f).Spacing(8.0f);
    root.Add<Label>(L"Same 16 ms clock as gallery. Close when done.", TextRole::Caption)
        .Secondary(true);

    auto& t_linear = root.Add<AnimTrack>(L"Linear 600ms", Ease::Linear, 0.6f);
    auto& t_css = root.Add<AnimTrack>(L"CSS ease-in-out 600ms", Ease::CssEaseInOut, 0.6f);
    auto& t_cubic = root.Add<AnimTrack>(L"InOutCubic 300ms", Ease::InOutCubic, 0.3f);
    auto& t_out = root.Add<AnimTrack>(L"OutCubic 300ms", Ease::OutCubic, 0.3f);
    auto& t_mat = root.Add<AnimTrack>(L"Material 300ms", Ease::Material, 0.3f);
    auto& t_back = root.Add<AnimTrack>(L"OutBack 500ms", Ease::OutBack, 0.5f);
    auto& s_smooth = root.Add<AnimTrack>(L"Spring Smooth", Spring::Smooth());
    auto& s_snappy = root.Add<AnimTrack>(L"Spring Snappy", Spring::Snappy());
    auto& s_crit = root.Add<AnimTrack>(L"Spring Critical", Spring::Critical(220.0f));

    t_linear.Start();
    t_css.Start();
    t_cubic.Start();
    t_out.Start();
    t_mat.Start();
    t_back.Start();
    s_smooth.Start();
    s_snappy.Start();
    s_crit.Start();

    window.Show();
    return app.Run();
}

} // namespace

int main(int argc, char** argv) {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        std::printf("[FAIL] CoInitializeEx\n");
        return 1;
    }
    RunMath();
    RenderSheet(L"lumen_anim.png");
    CoUninitialize();
    std::printf("%s\n", g_failures == 0 ? "ALL PASS" : "FAILURES PRESENT");
    if (g_failures != 0) return 1;

    if (argc > 1 && std::strcmp(argv[1], "--live") == 0) return RunLive();
    return 0;
}
