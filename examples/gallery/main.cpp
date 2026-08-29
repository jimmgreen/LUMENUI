// gallery — fluentui 控件展示：全部控件、亮暗主题、菜单、对话框。
#include <fluentui/fluentui.h>
#include <windows.h>
#include <cwchar>
#include <string>

using namespace fui;

namespace {

Dialog g_dialog;
Label* g_status = nullptr;

std::wstring ItemText(size_t index) {
    wchar_t buffer[64];
    swprintf_s(buffer, L"列表项 %zu — 双击或回车激活", index + 1);
    return buffer;
}

std::wstring ItemGlyph(size_t index) {
    switch (index % 4) {
    case 0: return icon::kFolder;
    case 1: return icon::kMail;
    case 2: return icon::kSave;
    default: return icon::kInfo;
    }
}

void ShowConfirmDialog(Window& window) {
    g_dialog.Title(L"删除项目")
        .Message(L"此操作会将所选项目移入回收站。此操作可以撤销。")
        .SecondaryButton(L"取消", [] { g_status->Text(L"对话框：已取消"); })
        .PrimaryButton(L"删除", [] { g_status->Text(L"对话框：已删除"); });
    window.ShowDialog(g_dialog);
}

} // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    App app;
    Window window(L"FluentUI 控件展示", {900.0f, 820.0f});
    window.MinSize({620.0f, 520.0f});

    auto& root = window.Root();
    root.Padding(24.0f, 16.0f).Spacing(10.0f);

    // ---- 头部 ----
    auto& header = root.Add<StackPanel>(StackPanel::Orientation::Horizontal);
    header.Spacing(12.0f);
    header.Add<Label>(L"FluentUI", TextRole::Title);
    auto& subtitle = header.Add<Label>(L"高性能 Windows 控件库", TextRole::Body);
    subtitle.Secondary(true);

    int clicks = 0;
    g_status = &header.Add<Label>(L"");
    g_status->Secondary(true);

    // ---- 菜单 ----
    Menu menu;
    {
        MenuItem new_file;
        new_file.text = L"新建文件";
        new_file.glyph = icon::kOpenFile;
        new_file.action = [] { g_status->Text(L"菜单：新建文件"); };
        menu.AddItem(std::move(new_file));

        MenuItem open_file;
        open_file.text = L"打开文件";
        open_file.glyph = icon::kFolder;
        open_file.action = [] { g_status->Text(L"菜单：打开文件"); };
        menu.AddItem(std::move(open_file));

        MenuItem save;
        save.text = L"保存";
        save.glyph = icon::kSave;
        save.action = [] { g_status->Text(L"菜单：保存"); };
        menu.AddItem(std::move(save));

        menu.AddSeparator();

        MenuItem exit_item;
        exit_item.text = L"退出";
        exit_item.shortcut = L"Alt+F4";
        menu.AddItem(std::move(exit_item));
    }

    auto& menu_button = header.Add<Button>(L"菜单");
    menu_button.OnClick([&] {
        const Rect& bounds = menu_button.AbsoluteBounds();
        menu.Popup(window, {bounds.x, bounds.Bottom() + 4.0f});
    });

    // ---- 对话框 ----
    auto& dialog_button = header.Add<Button>(L"对话框");
    dialog_button.OnClick([&] { ShowConfirmDialog(window); });

    // ---- 主题 ----
    auto& theme_button = header.Add<Button>(L"切换主题");
    theme_button.OnClick([&] {
        window.SetTheme(window.Theme() == ThemeMode::Light ? ThemeMode::Dark : ThemeMode::Light);
    });

    // ---- 按钮 ----
    root.Add<Label>(L"按钮", TextRole::CaptionStrong).Secondary(true);
    auto& buttons = root.Add<StackPanel>(StackPanel::Orientation::Horizontal);
    buttons.Spacing(8.0f);
    auto& plain = buttons.Add<Button>(L"标准按钮");
    plain.OnClick([&] {
        ++clicks;
        g_status->Text(L"点击次数：" + std::to_wstring(clicks));
    });
    buttons.Add<Button>(L"主要操作", ButtonKind::Primary);
    buttons.Add<Button>(L"危险操作", ButtonKind::Danger);
    buttons.Add<Button>(L"透明", ButtonKind::Transparent);
    auto& icon_button = buttons.Add<Button>(L"");
    icon_button.Glyph(icon::kSettings);
    auto& disabled = buttons.Add<Button>(L"禁用状态");
    disabled.SetEnabled(false);

    // ---- 选择控件 ----
    root.Add<Label>(L"选择", TextRole::CaptionStrong).Secondary(true);
    auto& selection = root.Add<StackPanel>(StackPanel::Orientation::Horizontal);
    selection.Spacing(24.0f);
    selection.Add<CheckBox>(L"复选框 A").SetChecked(true);
    selection.Add<CheckBox>(L"复选框 B");
    selection.Add<RadioButton>(L"单选 1").SetChecked(true);
    selection.Add<RadioButton>(L"单选 2");
    selection.Add<Switch>(L"开关");

    // ---- 输入 ----
    root.Add<Label>(L"输入", TextRole::CaptionStrong).Secondary(true);
    auto& inputs = root.Add<StackPanel>(StackPanel::Orientation::Horizontal);
    inputs.Spacing(12.0f);
    inputs.Add<TextBox>().Placeholder(L"输入文字，支持复制粘贴");
    auto& combo = inputs.Add<ComboBox>();
    combo.AddItem(L"宋体");
    combo.AddItem(L"微软雅黑");
    combo.AddItem(L"Segoe UI Variable");
    combo.SetSelectedIndex(1);

    // ---- 滑块与进度 ----
    root.Add<Label>(L"滑块与进度", TextRole::CaptionStrong).Secondary(true);
    auto& meters = root.Add<StackPanel>(StackPanel::Orientation::Horizontal);
    meters.Spacing(24.0f);
    auto& slider = meters.Add<Slider>();
    slider.SetRange(0.0f, 100.0f);
    slider.SetValue(40.0f);
    auto& progress_label = meters.Add<Label>(L"40 / 100");
    progress_label.Secondary(true);
    slider.OnValueChanged([&] {
        progress_label.Text(std::to_wstring(static_cast<int>(slider.Value())) + L" / 100");
    });
    meters.Add<ProgressBar>().SetValue(0.65f);

    // ---- 列表 ----
    root.Add<Label>(L"虚拟化列表（10,000 行，仅绘制可见行）", TextRole::CaptionStrong)
        .Secondary(true);
    auto& list = root.Add<ListView>();
    list.SetItemCount(10000);
    list.ItemText(ItemText);
    list.ItemGlyph(ItemGlyph);
    list.SetSelectedIndex(2);
    list.OnActivate([&] {
        g_status->Text(L"激活：" + ItemText(static_cast<size_t>(list.SelectedIndex())));
    });

    // ---- 标签页 ----
    root.Add<Label>(L"标签页", TextRole::CaptionStrong).Secondary(true);
    auto& tabs = root.Add<TabControl>();
    auto& general_tab = tabs.AddTab(L"常规");
    general_tab.Padding(16.0f, 12.0f).Spacing(10.0f);
    general_tab.Add<CheckBox>(L"开机自动启动");
    general_tab.Add<CheckBox>(L"记住上次窗口位置").SetChecked(true);
    auto& about_tab = tabs.AddTab(L"关于");
    about_tab.Padding(16.0f, 12.0f).Spacing(10.0f);
    about_tab.Add<Label>(L"fluentui v0.1");
    about_tab.Add<Label>(L"Win32 + D3D11 + D2D + DirectComposition", TextRole::Caption)
        .Secondary(true);

    window.Show();
    return app.Run();
}
