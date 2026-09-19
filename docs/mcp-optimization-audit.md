# 优化记录落实情况复核

日期：2026-09-19。范围：docs/optimization-progress.md，参考 docs/performance-spikes.md 及 docs/mcp-code-review.md。基线 HEAD 为 18d8a3c，工作区含大量未提交改动，本次以实际文件为准。仅静态源码与证据可用性核查，未构建、未运行旧二进制、未进行 GUI/IME/DPI 实机检查，也未修改实现。

> 后续实施状态：本报告下文保留发现时的审计快照。用户授权后已补齐任务表三列排序、增加 10 条组件回归、独立构建并通过表格/API/动画/视觉/性能验证，归档本轮日志和截图。详见 docs/optimization-progress.md 的“2026-09-19 完善与当前验收”。排序遗漏及本轮证据缺失已处理；历史日志未恢复，实机验收与尖峰归因仍未完成。

> 后续清理状态：按用户明确选择，已清理仓库生成日志、临时测试截图及 artifacts 历史验证证据。此前归档记录作为历史事实保留，不再表示原始日志仍可访问；正式文档图片与测试源码保留。 本次删除 57 个文件，共 969,621 字节；清理后仓库（不含 .git）剩余 .log 文件为 0，319 个受保护文件哈希未变，8 张正式文档图片保留。已修正文档链接并添加针对性的忽略规则；编译产物及构建缓存未删除，未运行会重新生成测试产物的构建/测试。

## 结论

已落地多数精致化改动，但尚不能称为全部做到位。存在一项明确的功能声明与实现不一致：Gallery 项目任务表未启用列排序。实机交互、显示器 DPI 与性能尖峰仍未闭环。原优化方案不在当前文档所述快照中，因此不能证明原方案全部覆盖。

## 明确遗漏：任务表排序

- docs/optimization-progress.md:11 声称任务表支持列排序。
- examples/gallery/section_collections.cpp:169 的提示为 “Search tasks, sort by a column, or add a task for this session.”。
- 同文件 174–178 行创建 task_table，仅 Bind 和 AddColumn，没有对三列调用 Sortable(true)，后续仅刷新数据及空态。
- include/lumen/Table.h:552 的 ColumnDef.sortable 默认为 false。
- src/controls/table.cpp:251 的 AddColumnIndex 用默认 ColumnDef 创建列，没有默认启用排序。
- src/controls/table.cpp:2442 的表头点击分支要求 Sortable(header_press_col_) 为真。

因此当前这个示例的三列表头不会触发用户期待的点击排序。这是源码路径可确认的遗漏，不是仅因缺实机记录而标记待验收。建议显式启用三列排序，补充点击升序/降序、筛选后排序、添加后顺序的回归；此次未代为修改。

## 已找到实现依据的优化

| 项目 | 当前证据 | 本轮结论 |
| --- | --- | --- |
| HUD/背景默认关闭及显式开关 | examples/gallery/main.cpp:17–18、31–32、54–56 | 源码落实 |
| 普通 Sample 默认 Subtle、标题换行、层级与间距 | examples/gallery/common.h:49、62；examples/gallery/common.cpp:177–195 | 源码落实 |
| 首页设置、680 DIP 上限、保存前校验、Reset 快照 | examples/gallery/section_overview.cpp:249–301 | 源码落实；窄窗实际测量未复测 |
| 编辑后更新未保存状态 | examples/gallery/section_overview.cpp:273–282 | 四个字段都绑定了刷新回调 |
| 任务筛选、添加、空态、清空入口、36 DIP 行高 | examples/gallery/section_collections.cpp:161–201 | 源码落实；排序除外 |
| 删除确认对象明确、默认取消、按钮换行 | examples/gallery/section_overlays.cpp:29、58–66 | 源码落实；其他非删除弹窗默认主操作不构成此条反例 |
| 常态边框不依赖光强、主按钮和 ComboBox 焦点环 | src/controls/button.cpp:240–311；src/controls/checkbox.cpp:56–82；src/controls/combo_box.cpp:973–981 | 源码落实 |
| Switch 保留基类动画推进 | src/controls/switch.cpp:32–33 | 源码落实 |
| 中性灰阶、统一弹层底色 | src/core/theme.cpp:27、61–66；src/controls/dialog.cpp:351；src/controls/flyout.cpp:20 | 源码落实 |
| 长标题测量及绘制共用高度 | src/controls/dialog.cpp 的 TitleBlockH 与 Draw 路径；tests/visual/main.cpp:5871–5877 | 找到实现及对应断言，未重跑 |
| 嵌套密度避免重复套用 | src/core/control.cpp:623–632；tests/visual/main.cpp:2708–2717 | 找到实现及对应断言，未重跑 |
| 静态卡、无光边框、焦点环回归 | tests/visual/main.cpp:5824–5877 TestQuietStates | 回归代码存在，不等于本轮通过 |

## 尚未完成的验收与证据缺口

1. docs/optimization-progress.md:62、65–66 明确保留任务筛选/排序及弹窗完整交互、真实显示器 DPI 切换、鼠标/连续输入观感等未验收项。自动化曾超时，原因未分清，不能划为已完成。
2. docs/performance-spikes.md:24–27 明确历史 68.445 ms 尖峰没有复现但未定位。15 轮后续均值约 4.283 ms，不代表所有帧低于预算，也不能证明原优化提升性能。tests/perf/main.cpp:252 的通过条件仍为平均值小于 8 ms 加表格检查，不是逐帧上限；基准也不是完整窗口交互帧时间。
3. 当前 docs/optimization/ 的四张参考 PNG 均存在，但本轮只确认文件可用，没有重新导出或执行视觉对比。文档承认没有同条件修改前基线，无法作严格 A/B。
4. 本轮未找到 build/optimization* 或 build/perf-analysis*.log 对应历史日志；文档的成功记录保留为历史报告，不能据此独立复核当前源码版本。
5. build/CMakeCache.txt:339 仍指向旧机器用户目录中的项目路径；build-ime 的源路径为当前用户目录。仓库已有二进制不证明匹配当前源码。后续验证应使用已核对配置或新的独立构建目录，不直接运行旧 build 二进制作为结论。

## 后续审查记录的口径

docs/mcp-code-review.md 的初始发现不能脱离 70 行后的修复记录引用。抽查密码 UIA 掩码、Control 弱链重挂、冻结列分组头及 Settings 精度，当前代码均能找到相应修复。本文不重复全库审计，也不把抽查扩大为全部安全问题已解决。

焦点记录需重新分场景核验：src/core/input_router.cpp:50–53 当前 ClearFocus 已清空 focus_restore_，Control::Blur 在持有逻辑焦点时调用它；与文档笼统的“显式清焦后仍恢复、未修复”表述不能直接等同。另一方面 OnHwndFocus 恢复时仅检查控件自身 visible/enabled，未检查祖先；Blur 在窗口失焦后的行为也要对照公共头“未聚焦为空操作”契约。不能未经实测就宣告整项关闭或把旧探针结论原样迁移。

## 推荐收尾顺序

1. 修复 Gallery 项目任务表排序声明与实现不一致，并验证相关交互。
2. 使用与当前源码匹配的独立构建完成相关回归，保留日志及版本信息。
3. 补齐筛选/排序/弹窗、真实 DPI 切换和输入实机验收。
4. 性能按原场景、同环境对比；尖峰复现时采集调度/CPU/GPU 证据，不以单个均值宣告改善。
5. 更新原优化进度表，将“实现完成”“自动化通过”“实机验收完成”“待定位”分开记录。
