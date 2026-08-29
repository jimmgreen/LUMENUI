# fluentui

高性能 Windows 原生 Fluent UI 控件库。纯 C++20，基于 Win32 + Direct3D 11 + Direct2D + DirectWrite + DirectComposition，无任何第三方依赖，仅支持 Windows 10 1903+ / Windows 11。

## 特性

- **调用简单**：控件是带属性和回调的普通对象，几行代码出界面。
- **高性能**：D2D 立即模式绘制、画刷与文本布局全缓存、每帧零堆分配、脏标记按需重绘、动画时钟只在有动画时开启（空闲零 CPU）。基准：1280×800 全帧重绘（8 按钮 + 100,000 行虚拟列表）平均 **0.08 ms/帧**。
- **按需链接**：每个控件独立编译单元打进静态库，链接器只把实际用到的控件拉进最终 exe，没用到的控件不进程序。
- **Fluent 设计**：暗/亮/高对比主题、跟随系统强调色与系统亮暗偏好（`WM_SETTINGCHANGE` 实时切换）、圆角/间距/行高等几何 token。
- **高 DPI**：Per-Monitor V2 感知，全部布局以 DIP 计算。

## 构建

需要 MSVC（VS 2022+）、CMake 3.25+、Ninja：

```bat
build.bat
```

产物在 `build\`：`fluentui.lib`、`fluentui_gallery.exe`（控件展示）、`fluentui_visual_test.exe`（视觉回归）、`fluentui_perf_test.exe`（帧耗时基准）。

在自己的项目里使用：

```cmake
add_subdirectory(fluentui)
target_link_libraries(myapp PRIVATE fluentui)
```

## 快速上手

```cpp
#include <fluentui/fluentui.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
    fui::App app;
    fui::Window window(L"演示", {800.0f, 600.0f});

    auto& name = window.Root().Add<fui::TextBox>();
    name.Placeholder(L"输入名字");

    auto& ok = window.Root().Add<fui::Button>(L"确定", fui::ButtonKind::Primary);
    ok.OnClick([] { /* ... */ });

    window.Show();
    return app.Run();
}
```

完整示例见 `examples/gallery/main.cpp`（运行 `build\fluentui_gallery.exe`）。

## 控件

Button、CheckBox、RadioButton、Switch、TextBox（选区/剪贴板/占位符）、Slider、ProgressBar（确定/不定态）、ComboBox、Menu（弹出菜单）、Dialog（窗口内模态卡片）、ListView（虚拟化列表，十万行只画可见行）、TabControl、Label、StackPanel/Panel 布局容器。

图标使用系统字体 Segoe Fluent Icons 字形（`fui::icon::kSettings` 等，Win10 自动回退 Segoe MDL2 Assets），无资源文件。

## 架构

```
include/fluentui/    公共 API（App/Window/Theme/Painter/Control/各控件头文件）
src/core/            始终链接的核心：渲染设备、文本服务、绘制原语、窗口/输入/动画、布局
src/controls/        每控件一个 .cpp —— 链接粒度即控件粒度
examples/gallery/    控件展示程序
tests/visual/        离屏渲染 → PNG + 像素断言的视觉回归
tests/perf/          全帧重绘帧耗时基准
```

- **呈现通道**：D3D11 → DXGI 组合交换链（FLIP_DISCARD）→ DirectComposition；设备丢失自动检测与恢复。
- **文本**：DirectWrite 单例服务，文本布局按（格式、宽度、对齐、文本）缓存，超宽自动省略号截断。
- **动画**：单一定时器时钟（16ms），控件用指数平滑推进悬停/按下/开关等状态，全部静止后时钟自动停止。

## 测试

```bat
build\fluentui_visual_test.exe   # 输出 [PASS]/[FAIL]，生成暗/亮两主题 PNG 快照
build\fluentui_perf_test.exe     # 输出平均/最差帧耗时
```

## 路线图

- 多行 TextBox、滚动容器（ScrollViewer）、菜单子级与动画、Mica/Acrylic 材质
- 部分失效（脏矩形）重绘、DComp 动画直通
- 高对比度主题、多选 ListView、数据虚拟化回调
