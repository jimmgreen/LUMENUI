# 精致化优化实施与验证

2026-09-09，依据精致化优化方案。本文记录控件、渲染和 Typography 示例的实施与验证；原方案未随当前源码快照提供。

## 2026-09-19 完善与当前验收

本节是当前源码的复验结果；下文 2026-09-09/10 数据保留为历史记录，不作为本次运行结果。

### 本次修改

- `examples/gallery/section_collections.cpp`：Task、Status、Owner 三列显式启用 `Sortable(true)`，补齐原文声称支持但实际未接通的表头排序。
- `tests/api/table_filter_paging.cpp`：新增 10 条任务表场景断言，覆盖三列表头鼠标方法派发、升序/降序/恢复原序，以及模型筛选重置、空结果、清除筛选、新增任务后的排序。它验证组件行为，不冒充真实 Gallery 鼠标端到端验收。
- `build.bat`：支持可选第一个参数作为构建目录，并显式开启示例和测试；不删除旧缓存。直接运行仍默认 `build/`。旧目录源路径不匹配时，使用独立目录，例如：

```bat
build.bat "%TEMP%\lumen-optimization-check"
```

### 当前验证状态

| 验证项 | 本次结果 | 证据 |
| --- | --- | --- |
| 独立 Release 全量构建 | 通过，MSVC 编译警告 0；README 片段编译、命名检查通过 | 历史记录；原始证据已按用户要求清理 |
| 新 build.bat 指定目录入口 | 通过，对同一独立目录增量构建无工作项 | 历史记录；原始证据已按用户要求清理 |
| 表格过滤/分页/任务排序回归 | 33 条 PASS，退出码 0 | 历史记录；原始证据已按用户要求清理 |
| API、动画、视觉 | 均退出码 0，动画与视觉 ALL PASS | 历史记录；原始证据已按用户要求清理 |
| 性能 | 通过平均预算；均值 1.655 ms，最大 8.273 ms，1/300 帧 ≥8 ms | 历史记录；原始证据已按用户要求清理 |
| Gallery 图像 | 4 次导出成功，已检查截图可见区的文字、边界和布局 | [任务页](optimization/2026-09-19/tasks.png)、[首页](optimization/2026-09-19/overview.png)、[窄窗口](optimization/2026-09-19/narrow.png)、[弹层示例页](optimization/2026-09-19/overlays.png) |

构建采用新的系统临时目录，未使用或删除旧 `build/` 缓存。2026-09-19 后续按用户要求清理了仓库中的原始验证日志、指纹清单及汇总文件；以上数值保留为历史执行记录，不能再通过仓库原始日志独立复核。正式文档截图继续保留。工作区原有未提交改动保留，HEAD 不是完整快照指纹。

当前截图分别覆盖 100%/125%/150%/200% 离屏渲染比例，条件与下文历史命令一致；图像不包含实际打开的独立弹层。窄窗口底部字段在首屏之外，未据此宣告滚动交互已验收。

### 仍未完成，不能标为通过

- 真实显示器 DPI 切换、完整 Gallery 筛选/排序/弹窗鼠标与键盘端到端流程、连续 IME 和主观手感，仍需实机检查。本轮未重现或归因历史 computer-use 超时。
- 历史 68.445 ms 尖峰未定位。本轮仍有一帧 8.273 ms，不能声称所有帧 <8 ms；无同环境前后对照，不宣称性能改善。
- 原精致化方案未随已有记录提供，不能证明方案所有条目均覆盖。

## 已落地

- Gallery 默认关闭性能 HUD 和装饰背景；F11 切换 HUD，`--perf-hud` 启动时开启，`--debug-backdrop` 恢复背景演示。
- 普通 Sample 使用静态 Subtle 卡，品牌卡显式使用 Lumen。修正库中 Subtle 自动追光的行为；切换 CardStyle 时重置默认效果，仍可在 Card 后显式调用 Spotlight(true)。
- 页面标题默认不发光并支持换行；区块标题使用 BodyStrong，说明保持次要文本；统一 20 DIP 卡片内边距和 28 DIP 页面边距。
- 首页增加项目设置组合，使用 TextBox、ComboBox、CheckBox、Switch、FormField 和 Button。最大内容宽度 680 DIP，窄窗口可收缩；保存前 ValidateAll，错误时聚焦名称，保存和 Reset 使用会话快照。
- 集合页增加任务表：筛选、列排序、添加任务、无结果提示和清除筛选入口。数据行 36 DIP；表头文字提亮，Prepare 与 Draw 使用一致颜色，保持首次绘制零 C++ 堆分配。
- 确认弹窗使用明确的对象和动作，默认操作为取消；示例按钮区允许换行。
- 标准按钮、未选复选框、ComboBox 的常态边框不再依赖 glow_intensity；主按钮补充键盘焦点环，ComboBox 补充 FocusVisible 焦点环。
- 复选框和 Switch 的选中状态保持稳定实心标记，仅悬停或键盘聚焦时附加辉光；复选框标签使用正文亮度。Switch 动画不再因短路跳过基类推进。
- 中性化 Theme 的文本、输入悬停/按压、accent 交互色、滚动条及 success 灰阶；surface_flyout 为 #141414。Dialog 和 Flyout 统一使用 surface_flyout。
- Dialog 长标题按可用宽度换行，测量与绘制共享标题高度。
- 修复嵌套布局重复应用祖先密度：MeasureChildAt 只追加子级自身覆盖，测量与绘制的 EffectiveTheme 保持一致。

库默认字体、44/40 DIP 默认高度和窗口最小尺寸保持原契约。导航已有稳定选中标记及弱于选中的悬停填充；本次没有为这些已有行为另加特效或重写虚拟化。

## 本次验证

2026-09-10 性能复测：15 轮共 4,500 帧平均约 4.283 ms，最大 9.933 ms，9 帧 ≥8 ms；未复现历史 68.445 ms 尖峰。阶段计时与归因边界见 [性能尖峰分析](performance-spikes.md)，不能称历史尖峰已修复。

- `build.bat` 成功，示例与测试配置开启；无 C++ 编译警告。外部 FreeType 存在既有 CMake 弃用警告。
- `lumen_visual_test.exe`：ALL PASS；新增无光边界、主按钮焦点环、静态卡默认值、长弹窗标题、密度不随嵌套层数叠乘、测量/绘制密度一致及 Esc 关闭弹窗恢复焦点的断言。
- `lumen_anim_test.exe`：ALL PASS。
- `lumen_api_test.exe`：退出码 0；构建中的命名与 README 检查成功。
- `lumen_perf_test.exe`：退出码 0。1280×800、300 次全帧重绘，平均 **7.041 ms/帧**，最差 **68.445 ms/帧**。DirectWrite / LumaText 的表格冷启动、静止、悬停、滚动和进度场景 Draw 均零 C++ 堆分配。
- 改动前同工作区基线平均 4.421 ms、最差 7.091 ms。本次通过程序的平均 <8 ms 预算，但有明显尖峰，不能声称每帧低于 8 ms，也不能声称性能已改善。帧时间波动原因仍待独立分析。
- 最终日志：`build/optimization-final-build.log`、`build/optimization-final-{visual,perf,anim,api}.log`。先前发现的测试缩放参数错误和表头预缓存颜色不一致均已修正，以上为修正后结果。

## 图像与复现

Gallery 现可直接导出当前控件树，复用 OffscreenRenderer 和与窗口一致的文字后端，不需要 computer-use。导出关闭页面过渡，捕获内容树（导航、页面、状态栏），不包含独立的自绘标题栏或窗口弹层。

```powershell
.\build\lumen_gallery.exe --screenshot=docs/optimization/overview.png
.\build\lumen_gallery.exe --small --glow=0 --capture-scale=2 --screenshot=docs/optimization/overview-small-200.png
.\build\lumen_gallery.exe collections --small --compact --capture-scale=1.25 --screenshot=docs/optimization/tasks-125.png
.\build\lumen_gallery.exe overlays --comfortable --glow=1 --capture-scale=1.5 --screenshot=docs/optimization/overlays-150.png
```

| 图像 | 条件 |
| --- | --- |
| [首页与设置](optimization/overview.png) | 1280×860 DIP，Normal，光强 0.5，100% 渲染比例 |
| [窄窗口首页](optimization/overview-small-200.png) | 960×640 DIP，Normal，光强 0，200% 渲染比例 |
| [任务列表](optimization/tasks-125.png) | 960×640 DIP，Compact，光强 0.5，125% 渲染比例 |
| [弹层示例页](optimization/overlays-150.png) | 1280×860 DIP，Comfortable，光强 1，150% 渲染比例 |

四张导出均成功，已检查文字、边界、分组及布局。过程中发现并修复了设置区宽度失效、文字后端不一致造成省略号、密度重复叠乘和导出过渡中间帧的问题。小窗口允许内容滚动，截图之外的字段不算已完成交互验收。

## 尚未完成的验收

### 2026-09-10 实机补充

- 实际操作 Gallery：空名称出现必填错误，保存失败后焦点返回输入框；中英混排显示正常。
- Tab、Enter、Space 完成 ComboBox 选择、复选框及开关切换、保存；Reset 恢复本次会话保存的名称、可见性和开关快照。
- 使用真实拼音输入法输入 n/i，候选框跟随光标，空格提交“你”，Esc 取消后续组合，已提交文字保留。
- 窗口从 1280×860 缩至 960×640，滚动后设置区全部字段与操作可达。
- 发现并修复编辑后仍显示旧保存/错误提示的问题：四个字段的用户变更更新未保存状态。重新构建成功，实机复测复选框编辑显示 Unsaved changes，Reset 恢复数据及提示。日志为 `build/optimization-interaction-build.log`。
- 任务页输入、后续导航及窗口激活连续出现 computer-use 超时，重启后复现；目前未确定是应用还是自动化工具原因，筛选/排序与弹窗完整实机流程不记为通过。

- 未保留首轮修改前同条件 Gallery 图像，不能提供严格 A/B 视觉基线。
- 100% / 125% / 150% / 200% 图像覆盖的是离屏渲染比例；真实显示器 DPI 切换、完整鼠标手感、筛选/排序与弹窗流程仍需实机确认。基本 IME 和设置保存/恢复已完成上述检查。
- 自动焦点与动画断言不替代连续真实输入和高刷新率下的主观观感检查；性能尖峰仍未定位。
