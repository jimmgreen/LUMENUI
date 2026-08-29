# fluentui 仓库规范

## 模块结构

- `include/fluentui/` 是唯一的公共 API 面。公共头不暴露 D2D/HWND 等系统类型（`Painter.h` 中的 D2D 设备上下文指针除外），几何与颜色使用 `fui::Rect/Point/Size/Color`。
- `src/core/` 承载渲染、文本、主题、窗口、输入、动画与布局；`src/controls/` 每个控件一对 `.h/.cpp`（公共头在 include 侧，控件实现文件保持一控件一编译单元，保证静态库按控件链接）。
- 控件实现只允许依赖：自己的公共头、`fluentui/Panel.h`、`fluentui/Painter.h`、`../core/text_service.h`，需要窗口内部通道时加 `../core/window_impl.h`。不得反向依赖具体控件。
- 渲染、文本、Shell 等阻塞操作不得进入绘制路径；绘制路径每帧零堆分配。

## 代码风格

- 四空格缩进、同花大括号；类型与函数 `PascalCase`，数据成员 `snake_case`，命名空间小写（`fui`）。
- `/W4 /permissive- /utf-8`、C++20；新警告即缺陷，构建必须零警告。
- 注释只写非显而易见的约束（如 DXGI 组合交换链必须 PREMULTIPLIED、Present 先于 Commit），不写"这行做了什么"。
- 控件状态动画统一走 `Control::EaseTo` 指数平滑 + `Animate()` 请求时钟；离屏（无窗口）场景状态 setter 必须直接到位。

## 构建与测试

- `build.bat` 全量构建（vcvars64 + Ninja + Release）。改完代码必须重跑两个测试：
  - `fluentui_visual_test.exe`：暗/亮主题控件状态板 PNG + 像素断言，要求 ALL PASS。
  - `fluentui_perf_test.exe`：1280×800 典型界面全帧重绘，预算 < 8 ms/帧（当前基线约 0.08 ms）。
- 影响输入/布局/呈现的改动，额外运行 `fluentui_gallery.exe` 做实机检查：悬停、按下、焦点环、Tab 导航、DPI 缩放、窗口缩放、暗亮切换。
- 无法自动化的检查（真实鼠标手感、IME 输入）必须显式记录为待人工确认项。

## 已知关键约束

- `CreateSwapChainForComposition`：AlphaMode 必须 PREMULTIPLIED、Scaling 必须 STRETCH；呈现顺序必须 `swapchain->Present()` 再 `DComp Commit()`，缺 Present 就是白屏。
- `ResizeBuffers` 前必须释放 D2D 目标位图（`dc_->SetTarget(nullptr)` + reset），同尺寸直接短路。
- 布局容器通过 `Panel::MeasureChildAt/ArrangeChildAt` 等 protected 辅助操作子级（友元代访问），不要在子类直接摸 `Control` 的 protected 成员。
