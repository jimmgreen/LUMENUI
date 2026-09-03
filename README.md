# LUMEN

高性能 Windows 原生**黑白光感（Monochrome Luminescent）**UI 控件库。纯 C++20，基于 Win32 + Direct3D 11 + Direct2D + DirectWrite + DirectComposition，无第三方依赖（可选接入 LumaText 文字栅格化），仅支持 Windows 10 1903+ / Windows 11。

## 设计语言

纯黑底（`#000000`）上以亮白发光体建立层次：阶梯式外发光、鼠标跟随聚光卡片（600px 光斑 + 400px 边缘折射光环）、顶部镜面高光、暗网格与顶部环境辉光背景。全部光感 token（`glow_sm/md/lg`、`spotlight_fill/border`、`specular_line`、`ambient_flare`）受 `Window::GlowIntensity(0..1)` 全局缩放。

## 特性

- **调用简单**：控件是带属性和回调的普通对象；`Row`/`Column`/`Grid`/`Grow`/`ScrollViewer` 负责排版，几行代码出界面。
- **高性能**：D2D 立即模式绘制、纯色/径向/线性渐变画刷与文本布局全缓存（每帧仅突变画刷属性，零堆分配）、动画时钟只在有动画时开启（空闲零 CPU）。基准：1280×800 全帧重绘（8 按钮 + 100,000 行虚拟列表 + 聚光卡）平均 **0.17 ms/帧**。
- **按需链接**：每个控件独立编译单元打进静态库，链接器只把实际用到的控件拉进最终 exe。
- **鼠标聚光**：`Spotlight(true)` 后光斑位置随鼠标当帧跟随；悬停进出渐显；离屏（无窗口）场景直接到位、光斑居中，便于测试。
- **高 DPI**：Per-Monitor V2 感知，全部布局以 DIP 计算。

## 试用

不编译也能看效果：从 [Releases](https://github.com/jimmgreen/LUMENUI/releases/latest) 下载 `lumen-gallery-windows-x64.zip`，解压后运行 `lumen_gallery.exe`（Windows 10 1903+ / Windows 11，x64）。包内已带 `lumatext.dll` 与 VC 运行库。

## 构建

需要 MSVC（VS 2022+）、CMake 3.25+、Ninja：

```bat
build.bat
```

产物在 `build\`：`lumen.lib`、`lumen_gallery.exe`（控件展示）、`lumen_visual_test.exe`（视觉回归）、`lumen_perf_test.exe`（帧耗时基准）。

在自己的项目里使用。三种拿到库的方式，CMake 都只剩 `lumen_add_executable`：

```cmake
# 1) 子目录
add_subdirectory(path/to/LUMENUI)
lumen_add_executable(myapp main.cpp app.rc)

# 2) FetchContent
include(FetchContent)
FetchContent_Declare(lumen
    GIT_REPOSITORY https://github.com/jimmgreen/LUMENUI.git
    GIT_TAG HEAD)
FetchContent_MakeAvailable(lumen)
lumen_add_executable(myapp main.cpp app.rc)

# 3) 安装后 find_package
find_package(lumen CONFIG REQUIRED)
lumen_add_executable(myapp main.cpp app.rc)
```

`lumen_add_executable` 包掉 `WIN32`、链 `lumen::lumen lumen::main`、拷贝 LumaText DLL、`/utf-8`。vcpkg overlay 见 `ports/lumen/`。LumaText 缺失时 configure 会 `WARNING`；需要强制失败则 `-DLUMEN_REQUIRE_LUMATEXT=ON`。

可拷贝起步模板见 `examples/template/`（独立 `cmake -S . -B build`，带图标）。非 CMake / VS 工程可 `#include <lumen/wmain.h>` 后写 `LUMEN_MAIN()`，不要再链 `lumen::main`。

产品代码按需包含控件头，以便 `/OPT:REF` 裁掉未用到的控件。推荐三套最小头：

- 窗口壳：`App.h` / `Window.h` / `NavigationView.h` / `PageHost.h` / `ScrollViewer.h`
- 表单：`FormField.h` / `TextBox.h` / `ComboBox.h` / `Button.h` / `Dialog.h`
- 列表：`ListView.h` / `Table.h` / `ItemsModel.h`

入口（链 `lumen::main`）：

```cpp
#include <lumen/lumen.h>
#include <lumen/Main.h>
int lumen_main(std::span<const std::wstring_view>) {
    return lumen::Run(L"演示", [](lumen::Window& w) {
        w.Root().Add<lumen::Button>(L"你好", lumen::ButtonKind::Primary);
    });
}
```

`#include <lumen/lumen.h>` 会拉齐全部控件头，适合 Gallery 与演示。

UTF-8 字符串可用 `lumen::U8("你好")` 转成 `wstring`。

一行常用 API：`ShowToast` / `Confirm` / `RunAsync(..., busy)` / `BindShortcut` / `Persist` / `Bind(model)` / `Command` / `ShowDialog(spec)` / `RememberPlacement` / `SingleInstance`。

布局三句话：`Column` 交叉轴默认 Stretch；`Grow` 吃主轴剩余；超出视口用 `ScrollViewer().Grow()`。

## 快速上手

```cpp
#include <lumen/lumen.h>
#include <lumen/Main.h>

int lumen_main(std::span<const std::wstring_view>) {
    lumen::App app;
    lumen::Window window(L"演示", {800.0f, 600.0f});
    window.Backdrop(lumen::Backdrop::All);   // 暗网格 + 顶部环境辉光

    auto& name = window.Root().Add<lumen::TextBox>();
    name.Placeholder(L"输入名字");

    auto& ok = window.Root().Add<lumen::Button>(L"确定", lumen::ButtonKind::Primary);
    ok.OnClick([] { /* ... */ });

    window.Show();
    return app.Run();
}
```

工作线程回 UI、文件对话框与窗口快捷键：

```cpp
window.Post([&window] { window.ShowToast(L"imported"); });   // 任意线程
window.IsUiThread();
window.BindShortcut(L"Ctrl+S", [&window] { /* save */ });
if (auto path = lumen::dialogs::PickFile(window, L"文本|*.txt|全部|*.*")) {
    lumen::clipboard::Text(*path);
}
window.ShowBusy(L"正在导入…", [&window] { window.CloseBusy(); });
window.SetInterval(30.0f, [&window] { /* 自动保存 */ });
window.RememberPlacement(L"Software\\MyApp\\Main");
```

`App::SingleInstance(L"MyApp")` 在已有实例时激活主窗并返回 false。`App::RegisterGlobalHotkey` 注册系统热键。托盘见 `Window::TrayIcon` / `MinimizeToTray` / `TrayMenu`。

布局用 `Row` / `Column` / `Grid`（别名 `HStack` / `VStack`），不要手写 `SetBounds`：

```cpp
auto& cols = window.Root().Add<lumen::Grid>(2).Gap(16.0f);
auto& left = cols.Add<lumen::Column>().Spacing(12.0f);
// Column 默认交叉轴 Stretch：子级已经拉满列宽。Grow 吃的是主轴剩余高度。
left.Add<lumen::TextBox>().Placeholder(L"搜索");
left.Add<lumen::ListView>().Grow();          // 吃掉剩余高度
// 父级 AlignCross(Start) 时要用 FillCross() 铺宽，不必再套一层 Row。
```

`Grid(1, 0, 1)` 是顶栏的 1fr / auto / 1fr。间距预设：`Column().Comfortable()` / `Dense()`。

声明式嵌套、共享命令与子树密度：

```cpp
window.Root().Children(
    lumen::Column().Comfortable().Children(
        lumen::FormField(L"名称").Child(lumen::TextBox().Placeholder(L"项目名")),
        lumen::Row().Children(
            lumen::Button(L"取消"),
            lumen::Button(L"确定", lumen::ButtonKind::Primary).Ref(ok))
    )
);

save.CanExecute([&] { return dirty; });
ok->Bind(save);
window.Bind(save);
toolbar.Add(save);

auto& compact = window.Root().Add<lumen::Column>();
compact.Density(lumen::Density::Compact);
```

表单 + 对话框 + 列表（窗口持有 Dialog 生命周期）：

```cpp
auto& form = window.Root().Add<lumen::Column>().Comfortable();
form.Add<lumen::FormField>(L"名称").Add<lumen::TextBox>().Placeholder(L"项目名");
form.Add<lumen::Button>(L"删除", lumen::ButtonKind::Danger, [&window] {
    window.ShowDialog({
        .title = L"删除",
        .message = L"不可恢复",
        .primary = {L"删除", [] {}},
        .secondary = {L"取消", {}},
    });
});
form.Add<lumen::ListView>()
    .ItemCount(100)
    .ItemText([](size_t i, std::wstring& s) { s = L"Item " + std::to_wstring(i); })
    .Grow();

form.Add<lumen::ListView>().Bind(inbox).Grow();
inbox.Reset({L"Standup", L"Review"});
inbox.Insert(0, L"New");
inbox.RemoveAt(0);
```

表单一行绑定：

```cpp
Property<std::wstring> name;
auto& sheet = window.Root().Add<Form>();
sheet.Field(L"名称").Validate(validate::Required()).Add<TextBox>().BindText(name);
sheet.Add<Button>(L"提交", ButtonKind::Primary).BindEnabled(sheet.Valid());
window.Confirm(L"删除", L"不可恢复", [](bool yes) { (void)yes; });
```

`FilteredModel` / `SortedModel` 装饰源模型；`Table::Bind` 读 `ItemRow.text` / `cells`。模型须活过控件。

完整示例见 `examples/gallery/main.cpp`（运行 `build\lumen_gallery.exe`）。Gallery 用 NavigationView 按用途分类浏览。

## 控件

所有配置方法与事件注册都返回控件引用，可链式声明到底；基类方法（`ToolTip`/`Enabled`/`Grow` 等）在各控件里都有转发重载，放在链中任意位置都不会截断后续调用：`combo.AddItems({L"A", L"B"}).SelectedIndex(1).Editable(true)`；`Table` 支持列式配置 `table.AddColumn(L"On", 64.0f).CheckBox(get, set).Sortable(true)`（代理可隐式转回列下标）。排序后的 Table 注意双空间：`SelectedIndex()` 是视图行、绑定回调拿到的是数据行，回调里查自家数据用 `SelectedDataIndex()`；菜单弹出用 `menu.PopupTo(control)`，分割按钮用 `split.DropdownMenu(std::move(menu))`，下拉按钮用 `dd.DropdownMenu(std::move(menu))` 一行接好。

Button（Solid Radiant / Electric Edge 流光 / Glass Specular / Halo / 白热警示，`Shimmer(true)` 开启流光边框）、RepeatButton（按住连发，默认 delay 0.40s / interval 0.05s）、CheckBox、RadioButton、Switch、TextBox（单行/多行、选区/剪贴板/撤销重做/聚焦光晕）、PasswordBox（可选明文揭示开关）、HotkeyBox（捕获 Ctrl+K 这类快捷键）、Slider、ProgressBar（确定/不定态）、ProgressRing、Sparkline（折线/柱状/面积迷你图）、Gauge（240° 径向仪表）、ComboBox（可选 Editable 筛选）、AutoSuggestBox（继承 TextBox 的建议输入）、NumberBox（数字过滤/步进/失焦钳制 + spin 区）、DatePicker/TimePicker（`std::chrono` 值 + 系统区域格式弹层）、ImageView（WIC 文件/内存解码 + 缩放/圆角）、CommandBar（自动溢出 + Toggle）、NavigationView（Auto/Expanded/Compact 单层导航）、Menu（子菜单/长列表滚动/快捷键）、Dialog（外发光模态，页脚真按钮可聚焦）、BusyOverlay（`Window::ShowBusy` 窗口忙碌遮罩）、Drawer（贴边临时抽屉）、Flyout（锚定轻弹层，点窗外轻触关闭）、TeachingTip（带箭头的引导气泡，与 Flyout 共用 overlay）、ToolTip（`Control::ToolTip` 字符串或 `std::unique_ptr<ToolTip>` 自定义内容，窗口层 overlay）、Toast（`Window::ShowToast`：图标 / 操作钮 / 时长 / 语义，悬停暂停退场）、ListView（虚拟化列表 + 覆盖滚动条 + 多选：Ctrl+点击/Shift 范围/Ctrl+A + `Bind(ItemsModel)`）、Table（虚拟化表格 + 表头 + 列宽拖动 + 表头点击排序 + 双击单元格行内编辑 + Progress/Icon 列 + `Bind(ItemsModel)`）、LogView（等宽日志、贴底跟随、Ctrl+C）、TreeView（虚拟化层级树：数据回调或 `SetFlatData` 平铺入口 + 展平可见缓存 + ExpandAll）、TreeTable（树形展开 + 多列表头，第一列是树）、Breadcrumb（面包屑导航）、TabControl、Pagination（分页器）、Label（`TextGlow(true)` 文字辉光）、RichLabel（加粗/次要/内联链接混排换行）、Badge/Chip、InfoBadge（导航/标签/图标角标）、TokenBox（Enter/逗号提交标签）、FormField（标签/必填/错误，包任意子控件）、SettingsCard/Expander（聚光卡片，可键盘操作）、GroupBox（带标题轻量分组 + 卡片聚光）、Viewbox（子级按自然尺寸排布再缩放绘制）、SplitView（可折叠侧边栏 + 主内容区）、MenuBar（窗口菜单栏）、StatusBar（窗口底栏：路径/缩放/计数）、DropDownButton（整钮弹出菜单，无主操作分隔）、EmptyState（空状态）、FileDropZone（资源管理器拖入文件）、Carousel（轮播 + 圆点）、Stepper（步骤条，已完成可回跳）、InfoBar（标题/正文/关闭 + `Action` 操作钮，语义靠字形与亮度）、HyperlinkButton、Skeleton（加载骨架屏，呼吸微光）、Rating（单色星级，部分填充/悬停预览）、Avatar（首字头像 + 在线状态点）、Separator、ColorSwatch（可作光效强度档位）、IconView、ScrollViewer（裁切视口 + 覆盖滚动条 + 焦点滚入视野）、Row/Column/Grid/Spacer、StackPanel/Panel（`CardStyle::Lumen` 聚光卡）。

图标按 Phosphor Regular 路径绘制（`lumen::icon::kSettings` / `kEdit` / `kDownload` 等约 90 个内置字形，默认 16px / weight 1.5）。自定义控件里：

```cpp
painter.DrawIcon(lumen::icon::kSearch, slot, theme.text);          // 默认 16 / 1.5
painter.DrawIcon(lumen::icon::kZap, center, theme.text, 20.0f);    // 点居中
painter.DrawIconPath(kSvgD, slot, theme.text);                     // 一次性 SVG（viewBox 256）
lumen::icon::Register(L'\uF000'[0], kSvgD);                       // 挂到字形，IconView / Button 也能画
```

未注册的码点回退 Segoe Fluent / MDL2。无资源文件。

## 架构

```
include/lumen/       公共 API（App/Window/Theme/Painter/Control/各控件头文件）
src/core/            始终链接的核心：渲染设备、文本服务、光效绘制原语、窗口/输入/动画、布局
src/controls/        每控件一个 .cpp —— 链接粒度即控件粒度
examples/gallery/    LUMEN 控件展示程序
tests/visual/        离屏渲染 → PNG + 像素断言的视觉回归
tests/perf/          全帧重绘帧耗时基准（含聚光卡压力项）
```

- **呈现通道**：D3D11 → DXGI 组合交换链（FLIP_SEQUENTIAL）→ DirectComposition；局部失效走 `Present1` 脏矩形；设备丢失自动检测与恢复。
- **文本**：可选 LumaText 栅格化（探测同级 `../lumatext` 或 `find_package(LumaText)`，运行时可用环境变量 `LUMEN_LUMATEXT` 指定 DLL），失败自动回退 DirectWrite。
- **光效原语**：`FillRoundedRectRadial` / `StrokeRoundedRectRadial`（渐变画刷按颜色对+衰减档缓存，每帧仅 SetCenter/SetRadius 突变）、`DrawGlow`（阶梯式外发光）、`DrawInnerLight`（内高光）、`StrokeRoundedRectSweep`（流光）、`DrawTextGlow`（文字辉光）、`DrawSpotlight`（聚光卡片标准组合）。
- **动画**：时钟跟 `Present(1,0)` 垂直同步，只在有动画时开启（空闲零 CPU）。`EaseTo` 指数平滑推进悬停辉光/流光/聚光渐显等状态。光斑位置不走时钟，随鼠标消息当帧更新。不要用 `WM_TIMER` 和显示器抢拍。

## 测试

```bat
build\lumen_visual_test.exe   # 输出 [PASS]/[FAIL]，生成暗色 PNG 快照（含聚光断言）
build\lumen_perf_test.exe     # 输出平均/最差帧耗时，预算 < 8 ms
```

## 排障

- `LUMEN_LOG=C:\temp\lumen.log` 或 `SetLogSink` 把诊断写到文件/回调。Warn/Error 即使没开文件也会 `OutputDebugString`。
- 常见 Warn：`D3D11 device is WARP`（无硬件 GPU）、`empty root`（`Show` 前没 `Add`）、`LumaText unavailable; using DirectWrite`（字比 Gallery 糙）。字体族回退打一条 Info。
- 高 DPI 发糊：忘了 `App` 时现在 `Window` 构造会 `App::Ensure()` 设 Per-Monitor V2；若进程 manifest 覆盖了 DPI，Debug 下会 `DebugTrap`。
- `Confirm`/`Prompt` 走回调，不要在 UI 线程上阻塞等待结果。
- Gallery 按 F12 调用 `Window::DumpTree` 看布局树。

## 路线图

- ~~多行 TextBox、菜单子级与动画~~（已交付：多行 + Undo/Redo；Menu 子菜单/滚动/快捷键；出现动画沿用原有）
- ~~部分失效（脏矩形）重绘~~（已交付：D2D 裁剪 + retain `CopyFromBitmap` + `Present1` 脏区；ListView/Table 行命令列表缓存）
- DComp 动画直通
- 模态遮罩真高斯模糊（D2D Effects）
- ~~Table 单元格编辑~~（已交付：双击 Text 单元格行内编辑，回车/失焦提交）
- ~~多选 ListView、列宽拖动与基础排序~~（已交付：ListView Ctrl/Shift/Ctrl+A 多选；Table 表头边界拖宽 + 点击循环排序，`DataRowAt` 视图→数据映射，`RowComparator` 自定义比较）
- ComboBox 大量项虚拟化、Password 揭示开关
