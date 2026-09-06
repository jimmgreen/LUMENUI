# 扩展 LUMEN 与验证

库实现遵守 [constraints.md](constraints.md)。读相关公共头和最接近的实现：叶控件可参考 `include/lumen/Separator.h` / `src/controls/separator.cpp`，带属性控件参考 Label，容器使用 `PanelOf<D>`。修改既有控件只涉及受影响接入点。

## 新控件接入

- 新建 `include/lumen/YourControl.h` 和 `src/controls/your_control.cpp`；在 `include/lumen/lumen.h` 与 `CMakeLists.txt` 的现有源列表登记。
- 公共头声明 Measure/Draw 及必要的属性、事件；新控件在 `tests/api/chain_compile.cpp` 增加 `LUMEN_CHAIN(lumen::YourControl);`。
- 在对应 `examples/gallery/section_*.cpp` 通过 `Sample(...)` 展示有代表性的状态；只有新增类别时才开分区，不限制示例行数。
- 可离屏渲染且状态具有可观察差异时，在 `tests/visual/main.cpp` 增加状态块和像素断言。行为回归用能暴露故障的测试，不为每个 setter 写重复实现的测试。

## 绘制、输入与无障碍

- `Draw` 使用 `absolute_`（DIP）和 `theme.*`；绘制、动画与聚光规则见 constraints.md，窗口 overlay 复用 `WindowImpl` 通道，不由控件自行 `CreateWindow`。
- 键盘在 `OnKey`，鼠标在 `OnMouseDown/Up/Move`；坐标为控件局部 DIP。
- 可操作控件覆盖 `Focusable()`，键盘导航 `FocusVisible()` 时通过 `PaintFocusRing` 绘制焦点环；装饰控件按语义开启 `HitTransparent()`。
- 覆盖 `AutomationType`，可见名走 `AccessibleName`，按实际交互实现需要的 UIA pattern。
- 改动赋值/事件/绑定时，同时核对 [use.md](use.md) 的事件与提交契约。

## 验证

仅库代码或影响构建产物的改动执行以下回归；纯文档改动检查链接、技能元数据及涉及的 API/源码事实即可。

`build.bat` 配置 vcvars64 并以 Ninja / Release 构建。若手动调用 CMake，先在同一进程环境加载 vcvars64，确认配置和构建成功，不使用旧 exe 充当本次结果。`LUMEN_BUILD_EXAMPLES` / `LUMEN_BUILD_TESTS` 在顶层默认开启、子项目默认关闭；缓存关闭时需显式开启所需目标。

构建成功后运行 `build/` 下四个程序：

| 程序 | 验收 |
| --- | --- |
| `lumen_visual_test.exe` | 状态板 PNG、像素及聚光断言 ALL PASS |
| `lumen_perf_test.exe` | 1280×800 典型界面（含聚光卡）全帧重绘 < 8 ms/帧；报告本次测量，不把历史耗时当基线 |
| `lumen_anim_test.exe` | 缓动/补间/弹簧数值断言 ALL PASS，并生成曲线 PNG；`--live` 仅用于实机对照 |
| `lumen_api_test.exe` | 链式 setter 编译回归；命名检查和 README 代码块编译发生在构建该目标时，并非运行 exe 时 |

影响输入、布局或呈现时额外运行 `lumen_gallery.exe`，检查受影响的悬停辉光、聚光跟随、按压缩放、焦点环、Tab、DPI、窗口缩放和光效强度。真实鼠标手感、IME 等未完成检查显式列为待人工确认，不能以启动成功替代。

宿主/设备/字体生命周期改动须确认 visual 中 `TestHostCycle` 的 12 轮建窗、泵消息、Close、Shutdown 无 WARP、无 WM_QUIT，加载引用释放且 owner 正确。仅在需要复现独立宿主或 DLL 卸载问题时，在 `%TEMP%` 添加针对性 console/DLL 冒烟（至少 12 轮），不为已有覆盖重复建测试程序。

检查通过后不反复扩大测试；有失败或环境阻塞时报告具体原因，不把未运行项写成通过。
