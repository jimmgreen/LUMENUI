# 三角形填充性能优化

日期：2026-09-19。仓库基线：`c917b4a`（v0.4.0）；开始时工作区干净。本轮只优化 Painter 三角形填充，不改变公共 API 签名、图表数据、主题、布局或动画时序。

## 热点与实现

- `src/controls/chart.cpp` 的 `FillArea` 将每个面积区间拆成两个三角形。
- 原 `src/core/painter.cpp` 的 `Painter::FillTriangle` 以实际顶点调用单槽 `EnsurePath`；连续不同三角形会反复释放和创建 `ID2D1PathGeometry`、打开和关闭 sink。相邻调用顶点不同，上一帧也无法复用整组路径。
- 现在复用顶点 `(0,0)/(1,0)/(0,1)` 的单位三角形，用仿射矩阵映射到实际顶点，再与调用方 DPI/局部矩阵组合。每次提交后恢复原矩阵，纯色画刷逻辑不变。
- 沿用 Painter 原有资源清理及设备上下文切换路径。稳定设备上下文中，三角形几何只在首次使用时创建，不引入按顶点增长的缓存。
- 三点描边仍走原逻辑，避免仿射缩放改变线宽。公共头只更新说明。

## 改动文件

| 路径 | 内容 |
| --- | --- |
| `src/core/painter.cpp` | 单位三角形复用与变换恢复 |
| `include/lumen/Painter.h` | 更新填充/描边缓存行为说明 |
| `tests/perf/main.cpp` | 增加每帧 256 个变动顶点三角形、10 帧预热及 120 帧采样；热绘制 C++ 分配断言 |
| `tests/visual/main.cpp` | 新增原始 D2D 路径参考实现和像素对照 |

## 同环境 A/B 方法

1. 通过 `build.bat` 在新的系统临时目录完成 MSVC Ninja / Release 构建，示例、测试及 bundled shared LumaText 开启；不使用仓库旧 build 缓存。
2. 先增加同一份性能测试，再以未优化的库实现构建并保留 `lumen_perf_baseline.exe`。
3. 应用库优化后，在同目录构建 `lumen_perf_test.exe`。两者使用相同测试代码、配置及依赖。
4. 完成其他自动回归和 Gallery 导出后，串行执行 5 对 A/B。奇数轮 baseline→optimized，偶数轮 optimized→baseline；没有同时运行构建或其他自动测试。
5. 全场景沿用 1280×800、8 按钮、100,000 行虚拟列表、聚光卡和 Area/Heatmap，每次 300 帧。每个版本共 1,500 帧。
6. 初始 3 次 baseline 预采样不混入下列 A/B 汇总。表中总平均取 5 次等帧数均值的平均；原日志输出精度为 0.001 ms。

## 本轮结果

| 轮次 | 场景 baseline ms/帧 | 场景 optimized ms/帧 | 三角形 baseline CPU Draw ms | 三角形 optimized CPU Draw ms |
| --- | ---: | ---: | ---: | ---: |
| 1 | 1.593 | 1.468 | 0.187 | 0.104 |
| 2 | 1.619 | 1.464 | 0.192 | 0.105 |
| 3 | 1.616 | 1.461 | 0.181 | 0.105 |
| 4 | 1.648 | 1.463 | 0.186 | 0.105 |
| 5 | 1.588 | 1.458 | 0.192 | 0.100 |
| 平均 | **1.6128** | **1.4628** | **0.1876** | **0.1038** |

- 典型场景平均耗时降低约 **9.3%**（0.1500 ms/帧）；三角形批次 CPU Draw 耗时降低约 **44.7%**。
- 场景最差帧 baseline 3.994 ms，optimized 2.555 ms；双方均为 **0/1,500 帧 ≥8 ms**。这是有限样本，不是逐帧时限保证。
- 场景 EndDraw 均值由约 0.075 ms 增至约 0.105 ms，整体提交区间仍下降；不能只引用 Draw 降幅而忽略提交成本。
- DirectWrite / LumaText 的表格 Draw 分配检查均通过；三角形热 Draw 的 C++ 分配前后均为 0。该计数只覆盖测试替换的 C++ new/new[]，不覆盖 D2D/COM/驱动内部，不能用它证明旧实现没有路径分配，也不能宣称整个渲染栈零分配。

## 验证

- 新建目录全量基线构建及优化后 Release 构建均成功；构建日志未发现 MSVC 编译 warning/error。命名检查通过，README 代码片段编译成功。
- `lumen_visual_test.exe`：退出码 0，ALL PASS。
- 新增参考对照：100%/125%/150%/200% 渲染比例 × 5 种状态，涵盖平移、旋转、非等比翻转、轴对齐/圆角裁剪、Layer 透明度、半透明重叠、极薄/共线/重合顶点、透明早返回和绘制后状态恢复。本机所有对照的最大像素通道差为 **0/255**；断言容差为 2/255。
- `lumen_anim_test.exe`：退出码 0，ALL PASS。
- `lumen_api_test.exe`：退出码 0；命名和 README 检查来自构建阶段，不由运行 exe 代替。
- 5 对性能运行全部退出码 0，平均 <8 ms 预算及分配检查通过。
- Gallery Charts 页面以 150% 离屏比例导出成功；已查看导出缩略图，首屏面积图、曲线、文字及布局未见明显异常。这不是全页面或真实鼠标交互验收。
- 编辑器 error/warning 诊断为空；`git diff --check` 通过。Git 提示的 LF→CRLF 转换不是编译器警告。

## 证据存放与边界

构建目录及原始证据位于本机 `%TEMP%\lumen-perf-round-hivaRK`，未向仓库加入生成日志、临时截图或二进制：

- `baseline-build.log`、`baseline-bench-build.log`、`optimized-build.log`
- `ab-1-baseline.log` 至 `ab-5-baseline.log`，以及对应 `ab-*-optimized.log`
- `ab-binary-sha256.txt`、保留的 baseline/optimized 性能二进制
- `optimized-visual.log`、`optimized-anim.log`、`optimized-api.log`
- `gallery-charts.log`、`gallery-charts-150.png`、`gallery-preview.jpg`

临时目录可能被系统清理；本文保留结果与方法，但不承诺原始证据永久存在。源码与二进制的本轮指纹清单随验证保存到同一临时目录。

本轮数字是离屏测试的 **CPU wall submission**，不是 GPU 完成耗时、窗口 Present 延迟或真实输入帧延迟。未采集 ETW/GPU 归因数据，不据此认定 `docs/performance-spikes.md` 中历史 68.445 ms 尖峰已修复。真实显示器 DPI 切换、连续输入/IME、鼠标悬停与拖拽手感仍未进行本轮人工验收。
