// visual — 视觉回归：离屏渲染控件状态板 → PNG + 像素断言。
#include "fluentui/fluentui.h"
#include "core/offscreen.h"
#include "core/text_service.h"
#include <objbase.h>
#include <cmath>
#include <cstdio>
#include <string>

using namespace fui;

namespace {

int g_failures = 0;

void Check(bool condition, const char* name) {
    std::printf("%s %s\n", condition ? "[PASS]" : "[FAIL]", name);
    if (!condition) ++g_failures;
}

bool CloseTo(Color a, Color b, float tol = 0.06f) {
    return std::fabs(a.r - b.r) <= tol && std::fabs(a.g - b.g) <= tol &&
           std::fabs(a.b - b.b) <= tol && std::fabs(a.a - b.a) <= tol;
}

// 测试子类：暴露 protected 布局接口与状态注入。
struct TestRoot : StackPanel {
    using StackPanel::Measure;
    using StackPanel::Arrange;
};



// 构建控件状态板；返回引用的坐标用于像素断言。
struct Scene {
    TestRoot root;
    Button* primary = nullptr;
    CheckBox* checked_box = nullptr;
    Switch* on_switch = nullptr;
    ListView* list = nullptr;

    void Build() {
        root.Padding(16.0f, 12.0f).Spacing(8.0f);
        auto& row1 = root.Add<StackPanel>(StackPanel::Orientation::Horizontal);
        row1.Spacing(8.0f);
        row1.Add<Button>(L"标准");
        row1.Add<Button>(L"主要", ButtonKind::Primary);
        primary = &row1.Add<Button>(L"危险", ButtonKind::Danger);
        row1.Add<Button>(L"透明", ButtonKind::Transparent);
        auto& disabled = row1.Add<Button>(L"禁用");
        disabled.SetEnabled(false);

        auto& row2 = root.Add<StackPanel>(StackPanel::Orientation::Horizontal);
        row2.Spacing(16.0f);
        row2.Add<CheckBox>(L"未选");
        checked_box = &row2.Add<CheckBox>(L"已选");
        checked_box->SetChecked(true);
        row2.Add<RadioButton>(L"单选甲").SetChecked(true);
        row2.Add<RadioButton>(L"单选乙");
        on_switch = &row2.Add<Switch>(L"开关");
        on_switch->SetChecked(true);
        row2.Add<Switch>(L"关");

        auto& row3 = root.Add<StackPanel>(StackPanel::Orientation::Horizontal);
        row3.Spacing(12.0f);
        row3.Add<TextBox>().Placeholder(L"占位符");
        auto& combo = row3.Add<ComboBox>();
        combo.AddItem(L"选项一");
        combo.AddItem(L"选项二");
        combo.SetSelectedIndex(0);

        auto& row4 = root.Add<StackPanel>(StackPanel::Orientation::Horizontal);
        row4.Spacing(12.0f);
        row4.Add<Slider>().SetValue(0.4f);
        row4.Add<ProgressBar>().SetValue(0.7f);
        row4.Add<ProgressBar>().SetIndeterminate(true);

        list = &root.Add<ListView>();
        list->SetItemCount(50);
        list->ItemText([](size_t i) { return L"项目 " + std::to_wstring(i); });
        list->SetSelectedIndex(1);

        auto& tabs = root.Add<TabControl>();
        tabs.AddTab(L"标签一").Add<Label>(L"内容一");
        tabs.AddTab(L"标签二").Add<Label>(L"内容二");

        root.Add<Label>(L"静态文本 Body / 二级文本", TextRole::Body).Secondary(true);
    }
};

void RenderScene(bool dark, const wchar_t* path) {
    OffscreenRenderer renderer;
    if (!renderer.Init(1000, 760)) {
        Check(false, dark ? "dark renderer init" : "light renderer init");
        return;
    }
    const Theme theme = MakeTheme(dark, Color::Hex(0x0078D4));
    Scene scene;
    scene.Build();
    scene.root.Measure({1000.0f, 760.0f}, theme);
    scene.root.Arrange({0.0f, 0.0f, 1000.0f, 760.0f});

    ID2D1DeviceContext2* dc = renderer.BeginDraw();
    Painter painter;
    painter.BeginFrame(dc, &UiText(), 1.0f);
    painter.FillRect({0, 0, 1000, 760}, theme.bg);
    DrawControlTree(painter, theme, &scene.root);
    painter.EndFrame();
    Check(renderer.EndDraw(), dark ? "dark enddraw" : "light enddraw");

    Color corner{};
    Check(renderer.ReadPixel(3, 3, corner) && CloseTo(corner, theme.bg),
          dark ? "dark bg pixel" : "light bg pixel");

    // 主按钮（危险红）中心 ≈ danger
    if (scene.primary) {
        const Rect b = scene.primary->AbsoluteBounds();
        Color c{};
        renderer.ReadPixel(static_cast<int>(b.x + b.w * 0.5f), static_cast<int>(b.y + b.h * 0.5f), c);
        Check(CloseTo(c, theme.danger), dark ? "dark danger button" : "light danger button");
    }
    // 勾选框左下角（避开勾选图标）≈ accent
    if (scene.checked_box) {
        const Rect b = scene.checked_box->AbsoluteBounds();
        Color c{};
        renderer.ReadPixel(static_cast<int>(b.x + 2.0f), static_cast<int>(b.y + b.h - 6.0f), c);
        Check(CloseTo(c, theme.accent), dark ? "dark checked box" : "light checked box");
    }
    // 开关（开）轨道左半 ≈ accent（右半被滑块占据）
    if (scene.on_switch) {
        const Rect b = scene.on_switch->AbsoluteBounds();
        Color c{};
        renderer.ReadPixel(static_cast<int>(b.x + 10.0f), static_cast<int>(b.y + b.h * 0.5f), c);
        Check(CloseTo(c, theme.accent), dark ? "dark switch on" : "light switch on");
    }
    // 列表选中行底色 ≈ selection 叠加在 card 上（右侧避开文本）
    if (scene.list) {
        const Rect b = scene.list->AbsoluteBounds();
        Color c{};
        renderer.ReadPixel(static_cast<int>(b.x + b.w - 40.0f), static_cast<int>(b.y + 45.0f), c);
        const float a = theme.selection.a;
        Color expected{theme.selection.r * a + theme.card.r * (1.0f - a),
                       theme.selection.g * a + theme.card.g * (1.0f - a),
                       theme.selection.b * a + theme.card.b * (1.0f - a), 1.0f};
        Check(CloseTo(c, expected), dark ? "dark list selection" : "light list selection");
    }

    Check(renderer.SavePNG(path), dark ? "save dark png" : "save light png");
    renderer.Shutdown();
}

} // namespace

int main() {
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        std::printf("[FAIL] CoInitializeEx\n");
        return 1;
    }
    RenderScene(true, L"fluentui_visual_dark.png");
    RenderScene(false, L"fluentui_visual_light.png");
    CoUninitialize();
    std::printf("%s\n", g_failures == 0 ? "ALL PASS" : "FAILURES PRESENT");
    return g_failures == 0 ? 0 : 1;
}
