# LUMENUI 与 SHOWBOX 后续待办

整理日期：2026-09-06。交接对象：GPT 5.3 Codex Spark。依据两个仓库当前未提交工作区的公共头、实现和页面代码整理；文档随后记录了已完成的页面迁移和构建结果。

## 1. 范围与状态口径

当前 SHOWBOX 的这些页面已经使用 LUMEN。这里的“新 UI 写法”指进一步使用现有的属性绑定、数字提交、Form 校验、模型增量更新、布局尺寸查询和高层事件，并非再次更换 UI 框架。

- **待完善**：源码中可以定位的剩余实现或迁移工作。
- **部分采用**：已经接入新能力，只处理下面明确列出的余项。
- **待核验**：有边界疑点或缺少本轮证据，先复现，不能直接称为已确认缺陷。
- **保持／低优先级**：没有明确迁移收益，不因代码中有 `sync()`、`syncing_` 或没出现 `BindValue` 就重写。

本清单是可执行的后续交接，不是全库无遗漏审计，也不把原 [优化方案](优化.md) R01～R40 全部重新标为未完成。[早期落实审查](优化落实审查.md) 停留在较早的六项修复阶段，部分结论已经过时，不能直接据此安排新实现。

开始任何代码包前，读取两个仓库各自适用的 AGENTS.md、LUMEN 技能及对应 reference，以当前 [公共头目录](include/lumen) 为准。保留现有未提交和未跟踪文件；不能从旧 EUI 版本覆盖当前成果。本文没有授权推送、发布或清理无关改动。

## 2. 已有成果：后续不要重复开发

当前源码已有以下能力，但“有实现”不代表本文重新确认了全部边界和实机验收：

- 任务状态／交付处理、Post 返回结果、连接与模型生命期处理。
- `UpdateScope`、属性绑定、`TextBox::SyncText`、焦点及 IME 组合态观察。
- NumberBox 草稿／提交接口、可空数字绑定、Form 校验与 `CommitAll()`。
- Editable ComboBox 文本编辑器、模型及选择相关接口。
- Table 类型化数字、可空数字与选项编辑、编辑事务事件、布局尺寸查询、列状态保存／恢复接口。
- 模型行键、过滤／排序装饰器、表格增量失效与页脚准备阶段缓存。
- ListView 命令搜索、页面离开处理、紧凑布局及动画／UIA 相关实现。
- perf 中已有表格冷启动、静态、悬停、滚动、进度负载及部分长尾／分配统计。

业务侧已有成果：楼层表采用可空数字／选项绑定；钢筋设计表采用尺寸查询；打印页普通进度已按变化行更新；命令面板采用列表模型、搜索缓存及稳定键。下文只安排剩余部分。

## 3. LUMENUI 库待办

每个编号作为一个独立工作包。先做证据明确且影响应用迁移的项；扩展能力不能和页面迁移混在一次大改中。

### L01 · P1 · 校正文档与当前 API 的差异

- [ ] 核对 [应用契约](.cursor/skills/lumen/references/use.md)、[Window.h](include/lumen/Window.h)、[NumberBox.h](include/lumen/NumberBox.h)、[Table.h](include/lumen/Table.h) 及实现。
- [ ] 校正任务执行状态和结果交付丢弃状态的描述；核对 NumberBox 非法失焦、Table 整型精确编辑、编辑事务及 ComboBox 编辑能力，删除已经失效的限制说明，保留真实限制。
- [ ] 补齐 `CommitAll`、草稿与外部同步、可空数字、UpdateScope、模型键和列状态使用方式。不要仅列 API 名称。
- [ ] 更新历史审查的状态指引；技能内容改动同步 `.cursor` 和 `.grok` 副本。

验收：每条契约有当前头／实现依据；两份技能一致、相对链接有效；纯文档不新增行为测试、不启动构建。后续页面包以修正后的契约为依据。

### L02 · P1 · Form 校验回调重入与销毁边界（先复现）

定位：[form_field.cpp](src/controls/form_field.cpp) 的 `RefreshValid`、`ValidateAll`、`CommitAll`。目前前两者会调用应用校验回调或发布有效性变化，并在之后继续访问对象；`CommitAll` 的部分存活检查不能直接证明整个调用链安全。

- [ ] 用最小回归验证：校验规则移除字段、有效性回调移除 Form、提交回调关闭页面，以及回调再次触发校验。
- [ ] 若可复现，按库现有弱引用／重入规则修复；明确中断时返回值，不依赖应用禁止正常关闭页面。
- [ ] 核对多个数字字段中有一个非法时，是否会部分提交，以及跨字段校验看到的是草稿还是旧值；先写清现有语义，不能默认为原子事务。

验收：无失效对象访问、无限重入或错误成功返回；普通字段和交叉校验仍有效。不复现则记录证据并关闭疑点，不为假设大改。

进展：`Form::RefreshValid()` 已加入重入合并：校验回调再次请求刷新时只记录 pending，当前轮结束后最多补跑一轮，避免递归。API 回归覆盖规则回调主动重入，已通过。

### L03 · P1 · Table 增量更新进一步缩小编辑控件同步范围

定位：[table.cpp](src/controls/table.cpp) 的模型变更处理、`RefreshRows`、`SyncSlots`。脏区和页脚已有增量处理，但相关路径仍会调用整个可见控件池的同步。

- [ ] 记录单行更新触发的可见单元格 getter／编辑器同步次数。
- [ ] 对无排序、无结构变更的局部更新，只同步受影响可见行；滚动、排序、结构变化保留必要的完整同步。
- [ ] 保留正在编辑的草稿、光标、IME 和业务行身份，不能用更小脏区掩盖重复重置编辑器。

进展：`Table::RefreshRows()` 现在只在受影响数据行落入当前可见视口时同步交互槽位；离屏更新仍执行行／页脚脏标记，不再无条件调用 `SyncSlots()`。排序、过滤、折叠行和编辑器路径保留原有完整同步入口。

验收：库四项回归（API、视觉、动画、性能）均通过；单行更新工作量随受影响可见单元格增长。仍需补充专门的槽位计数测试，确认编辑中可见行和排序后行身份。

### L04 · P1 · ComboBox 模型／静态项／编辑文本切换边界（先复现）

定位：[ComboBox.h](include/lumen/ComboBox.h)、[combo_box.cpp](src/controls/combo_box.cpp) 的 `Items`、模型绑定、`ClearSelection` 及编辑器同步。

- [ ] 核对绑定模型后再赋静态 Items 的所有权和订阅契约；检查旧模型后续通知是否覆盖新项。
- [ ] 检查清空选择时索引、稳定键、显示文本、编辑器文本是否一致；覆盖可编辑／不可编辑、多选和源模型销毁。
- [ ] 按明确契约修复可复现问题，避免把已有编辑器重做一遍。

验收：无旧源误通知、悬空访问、残留选中项或重复业务提交；IME 组合中同步另列实机检查。

### L05 · P1 · Table UIA 行身份与排序／过滤后的行为（先核验）

定位：[Table.h](include/lumen/Table.h)、[table.cpp](src/controls/table.cpp)、[ItemsModel.h](include/lumen/ItemsModel.h) 及现有 UIA 回归。

- [ ] 持有单元格 provider 后排序、过滤、删除该行，检查后续读取／提交指向的业务对象。
- [ ] 明确 provider 表示坐标还是业务行；按 UIA 契约处理失效，不允许意外写入另一条业务记录。
- [ ] 复用已有 Grid／Value 测试，只补缺失序列；核对输入和 UIA 共用校验、提交和通知规则。

进展：现有 Table 的 `AutomationCellValue`、`AutomationSetCellValue` 均经 `DataRowAt()` 映射，API／视觉回归已覆盖排序、过滤、稳定键和业务行身份；直接从 API 测试调用 UIA protected 入口不改变访问级别。仍需在 UIA 宿主序列中补 provider 持有后排序／删除的失效行为。

验收：语义明确且有回归证据；非法数字／不可编辑格不写回，合法修改一次提交。

### L06 · P2 · 列状态形成可迁移的应用持久化流程

现有 `ColumnSizing`、`CaptureColumns`、`RestoreColumns` 已实现，见 [Table.h](include/lumen/Table.h)。剩余工作不是新增同名 API。

- [ ] 核对列身份、顺序、宽度、显隐和冻结的保存范围。
- [ ] 在应用持久化层设计版本与稳定列 ID；核对新增／删除／重命名列、未知版本、损坏宽度的降级。
- [ ] 先接一个实际表格；通用能力确有缺口时再小范围补库。

验收：重启恢复、升级后保留可匹配列设置，坏配置不让表格消失或尺寸异常。库不直接耦合 SHOWBOX 配置文件。

### L07 · P2 · 共用混合字体与工程数字文本能力

现有 Label 与 Table 的文字处理仍未形成原 R23 所述统一 TextRun 流程。定位：[Text.h](include/lumen/Text.h)、[label.cpp](src/controls/label.cpp)、[table.cpp](src/controls/table.cpp)。

- [ ] 先整理 SHOWBOX 钢筋符号、中文、数字和上下标的实际样本，明确必须支持的最小范围。
- [ ] 先交付接口／缓存／测量绘制一致性的设计，再独立实现；不要一次重写所有文本控件。
- [ ] 优先统一钢筋表与 Label 的重复字体拆分；命中、选区和编辑若未覆盖，明确记录范围。

验收：测量与绘制一致，DPI／缩放不裁切；相关符号和数字对齐有可观察样本；Draw 分配与资源生命周期不退化。

### L08 · P2 · UI 线程诊断覆盖审计

定位：[control.cpp](src/core/control.cpp)、[ItemsModel.h](include/lumen/ItemsModel.h) 及宿主入口。已有部分控件线程检查，尚不能据此认为全部模型／视图入口都有诊断。

- [ ] 列出附着视图、修改模型、销毁模型、回 UI 投递的线程要求及检查位置。
- [ ] 对明确缺口补诊断和最小误用验证；区分未挂载数据准备和已被 UI 消费的对象。

进展：控件附着、布局、窗口消费入口已有 `AssertUiThread`／`IsUiThread` 断言；模型可在未挂载时准备数据，绑定回调在 UI 控件侧消费。当前没有足够证据支持扩大全局断言，保留为边界审计项。

验收：合法宿主流程不误报，错误线程有可定位诊断；不把 CAD 文档锁和命令上下文搬进库。

### L09 · P1 · 补齐真实交互性能与验收记录

定位：[性能测试](tests/perf/main.cpp)、[视觉／行为测试](tests/visual/main.cpp)。现有统计不能替代原 R37 的完整业务测量。

- [ ] 补钢筋多列表、打印增量进度、命令过滤、设置页展开四类工作负载。
- [ ] 分首开／预热，记录布局、Draw、Present 等待、输入到下一帧、p50/p95/p99／最差帧、分配、资源趋势和空闲帧请求；不可测项注明。
- [ ] 记录 CPU/GPU、DPI、Release、文本后端、数据规模和测试日期；保留 <8 ms/帧预算的原口径。
- [ ] 业务案例在 SHOWBOX 验证，或使用独立离屏负载。遵守用户先前要求，不往 Gallery 加工程业务页面；Gallery 仍用于库控件检查。

验收：可重复运行的负载和本轮报告；启动成功不能替代鼠标、IME、焦点或 CAD 宿主验收。

## 4. SHOWBOX 页面覆盖清单

源码入口：[SHOWBOX Ui](../SHOWBOX-LUMEN/src/Ui)。下表逐项覆盖本轮发现的 32 个 `*Page(s).cpp`、两个 LPF pane 和 Palette 页面文件；`SlabSettingsPages.cpp` 内含三个页面。表中“手动同步”是迁移候选，不是已确认错误。

| 页面文件 | 当前状态与剩余工作 | 工作包 |
| --- | --- | --- |
| [BeamElevationPage.cpp](../SHOWBOX-LUMEN/src/Ui/BeamElevationPage.cpp) | **已迁移**为 NumberBox，使用三位小数和提交事件；仍需在 AutoCAD 中实测正负号、单位和命令流程 | S01（代码完成，宿主未实测） |
| [FloorHeightTablePage.cpp](../SHOWBOX-LUMEN/src/Ui/FloorHeightTablePage.cpp) | **部分采用**可空数字、选项和编辑提交；行高已迁移为整数 NumberBox；标准层数和楼层名称解析仍保留业务路径 | S02（行高完成） |
| [RebarDesignPage.cpp](../SHOWBOX-LUMEN/src/Ui/RebarDesignPage.cpp) | **部分采用**尺寸查询和局部刷新；继续检查状态同步、选择身份和适用的 Reset | S03 |
| [RebarTablePage.cpp](../SHOWBOX-LUMEN/src/Ui/RebarTablePage.cpp) | **已完成尺寸迁移**：已建立表格时使用列宽／自然高度查询，未建立时保留初始化估算；编辑／选择状态仍需宿主回归 | S03（尺寸完成） |
| [PlotBatchPage.cpp](../SHOWBOX-LUMEN/src/Ui/PlotBatchPage.cpp) | **部分采用**复合路径字段和逐行更新；逐行模型写回已用 `UpdateScope` 合并通知，设置抽屉的起始序号与四个边距已改为 NumberBox | S04（部分完成） |
| [Palette/PalettePage.cpp](../SHOWBOX-LUMEN/src/Ui/Palette/PalettePage.cpp) | **部分采用**ListView、搜索缓存、稳定键及焦点／IME 事件；主要剩键盘、切页恢复、实机回归 | S05 |
| [AnnotationEditPage.cpp](../SHOWBOX-LUMEN/src/Ui/AnnotationEditPage.cpp) | 手动同步；筛选可绑定字段与一次性业务提交 | S06 |
| [AnnotationSettingsPage.cpp](../SHOWBOX-LUMEN/src/Ui/AnnotationSettingsPage.cpp) | 手动同步；数字草稿、默认值及批量回填 | S06 |
| [DetailPage.cpp](../SHOWBOX-LUMEN/src/Ui/DetailPage.cpp) | 手动同步；按字段类型接绑定／提交 | S06 |
| [MaterialHatchPage.cpp](../SHOWBOX-LUMEN/src/Ui/MaterialHatchPage.cpp) | 手动同步；保留材料／填充联动语义 | S06 |
| [RetainingWallPage.cpp](../SHOWBOX-LUMEN/src/Ui/RetainingWallPage.cpp) | 已用 Form 布局，仍手动同步；补字段校验和提交边界 | S06 |
| [RoundedRectSettingsPage.cpp](../SHOWBOX-LUMEN/src/Ui/RoundedRectSettingsPage.cpp) | 已用 Form 字段，仍手动同步；适合作为小型迁移样本 | S06 |
| [ZbcpSettingsPage.cpp](../SHOWBOX-LUMEN/src/Ui/ZbcpSettingsPage.cpp) | 已用 Form 字段；补绑定与提交，保留业务应用时机 | S06 |
| [LPFSettingsPage.cpp](../SHOWBOX-LUMEN/src/Ui/LPFSettingsPage.cpp) | 手动状态回填；与两个子 pane 分开改、联合回归 | S07 |
| [LPFLongiSettingsPane.cpp](../SHOWBOX-LUMEN/src/Ui/LPFLongiSettingsPane.cpp) | 数字控件辅助函数和手动同步；迁移提交与批量回填 | S07 |
| [LPFStirrupSettingsPane.cpp](../SHOWBOX-LUMEN/src/Ui/LPFStirrupSettingsPane.cpp) | 同上，保留配筋约束和派生值联动 | S07 |
| [SwColumnSettingsPage.cpp](../SHOWBOX-LUMEN/src/Ui/SwColumnSettingsPage.cpp) | 大量跨字段同步／转换；独立拆包，不机械替换枚举或业务字符串解析 | S07 |
| [SwColumnCheckSettingsPage.cpp](../SHOWBOX-LUMEN/src/Ui/SwColumnCheckSettingsPage.cpp) | 手动同步；数字校验与默认值回填 | S07 |
| [SlabSettingsPages.cpp](../SHOWBOX-LUMEN/src/Ui/SlabSettingsPages.cpp) | SlabRecognitionPage、SlabRebarParamPage、SlabRebarLayerPage 已使用多种 NumberBox，但仍大量手动同步；逐个页面迁移 | S07 |
| [PlotDirectoryFillPage.cpp](../SHOWBOX-LUMEN/src/Ui/PlotDirectoryFillPage.cpp) | **已迁移**文字高度、序号起始为整数 NumberBox；文字图层仍为 TextBox | S08（数字字段完成） |
| [PlotFrameNumberPage.cpp](../SHOWBOX-LUMEN/src/Ui/PlotFrameNumberPage.cpp) | **已迁移起始序号**为正整数 NumberBox；图号格式仍保留 TextBox 和业务格式规则 | S08（序号完成） |
| [PlotFrameInfoPage.cpp](../SHOWBOX-LUMEN/src/Ui/PlotFrameInfoPage.cpp) | 多个 TextBox 与 sync；核对回填是否覆盖正在编辑的文本，保留拾取／识别命令 | S08 |
| [CalcDwgImportPage.cpp](../SHOWBOX-LUMEN/src/Ui/CalcDwgImportPage.cpp) | 手动同步；模型与表单分开；正则楼层 token 的 stoi 属业务解析，不全局替换 | S08 |
| [CommandShortcutPage.cpp](../SHOWBOX-LUMEN/src/Ui/CommandShortcutPage.cpp) | 已绑定表格模型；手动表单同步和 Reset 按触发场景评估 | S09 |
| [NumericKeyCommandPage.cpp](../SHOWBOX-LUMEN/src/Ui/NumericKeyCommandPage.cpp) | 手动同步；保留快捷键冲突检测与命令分派 | S09 |
| [PlotPaperSizePage.cpp](../SHOWBOX-LUMEN/src/Ui/PlotPaperSizePage.cpp) | 已绑定模型；检查局部编辑与 Reset；角度归一化等业务规则保留 | S09 |
| [QuickSelectPropertyPage.cpp](../SHOWBOX-LUMEN/src/Ui/QuickSelectPropertyPage.cpp) | 已绑定模型；筛选值解析可能含业务文本，先确认再用数字控件 | S09 |
| [SequenceSystemPage.cpp](../SHOWBOX-LUMEN/src/Ui/SequenceSystemPage.cpp) | 已绑定表格；仍手动同步，检查可编辑 ComboBox、行身份、离页草稿与 Reset | S09 |
| [FindReplacePage.cpp](../SHOWBOX-LUMEN/src/Ui/FindReplacePage.cpp) | 文本／选项与会话同步；可考虑绑定，优先保护输入、搜索范围及替换动作语义 | S10 |
| [ImeSettingsPage.cpp](../SHOWBOX-LUMEN/src/Ui/ImeSettingsPage.cpp) | 小型设置与手动 sync；低优先级，真实 IME 行为必须人工核验 | S10 |
| [StaircaseMarkSettingsPage.cpp](../SHOWBOX-LUMEN/src/Ui/StaircaseMarkSettingsPage.cpp) | 少量文本／勾选状态；低优先级，不为形式统一引入复杂状态层 | S10 |
| [CavityMarkerPage.cpp](../SHOWBOX-LUMEN/src/Ui/CavityMarkerPage.cpp) | 复用 LayerColorSettingsPageBase；先检查公共基类，避免单页重复改造 | S10 |
| [HjqySettingsPage.cpp](../SHOWBOX-LUMEN/src/Ui/HjqySettingsPage.cpp) | 同上 | S10 |
| [PlotFrameApplyPage.cpp](../SHOWBOX-LUMEN/src/Ui/PlotFrameApplyPage.cpp) | 简单说明／勾选／开始按钮；目前无充分理由强制迁移，保留并回归 | S11 |
| [PlotSmartRecogPage.cpp](../SHOWBOX-LUMEN/src/Ui/PlotSmartRecogPage.cpp) | 已绑定结果模型；整批识别结果 Reset 可以合理，先确认是否存在高频局部更新 | S11 |

公共组件补充：[LayerColorSettingsPage.h](../SHOWBOX-LUMEN/src/Ui/LayerColorSettingsPage.h)、[PathField.h](../SHOWBOX-LUMEN/src/Ui/PathField.h) 应优先在实际复用点完善。宿主层生命周期与 CAD 上下文另行核对，不能因存在原生消息、关闭监听或 `Post` 就判定为旧写法。

## 5. SHOWBOX 工作包与验收

### 所有迁移包的共同要求

- 保持中文文案、默认值、单位、空值语义、配置键、保存／取消／应用时机及 CAD 命令行为。
- UI 保持现有单色设计；业务颜色选择仍表达 CAD 数据。不得再次改变命令面板字号：侧栏项维持 Caption 12，分组标题维持 BodyStrong 14。
- 按当前 API 选择 Property／Bind、SyncText、NumberBox／提交事件及 Form；不要强迫简单页面全部改成 Form。
- 只有绑定已完整覆盖反馈链时才删除同步标记；模型发布、分组选择和 CAD 回填的防重入标记可能仍有必要。
- 程序回填不额外保存或执行命令；普通同步不能打断 IME／光标；事件 Connection 按现有契约持有。
- 交付列出：改了哪些字段、保留了哪些业务解析、编译结果、实际完成的回归、尚未实测的操作。

| 包 | 执行范围与边界 | 验收重点 |
| --- | --- | --- |
| S01 | **代码完成**：梁标高已改用 NumberBox，保留三位小数、步长和开始命令时机 | 已通过 SHOWBOX 2027 Debug 编译／ARX 链接；仍需 AutoCAD 中验证输入未失焦直接执行、非法文本、取消、外部回填和业务参数 |
| S02 | **部分完成**：行高已改为 `NumberBox(Integer + Range 200..5000)`；已接入的新表格编辑保留，标准层数与模型 Reset 仍待核验 | 已通过 SHOWBOX 2027 Debug 编译／ARX 链接；仍需验证标准层生成、空层高、选项提交、Enter/Tab、排序后编辑正确行 |
| S03 | **部分完成**：RebarTable 已接入 `ColumnsPixelWidth`／`NaturalHeight`，RebarDesign 已有同类实现；钢筋表更新、编辑和选择仍待宿主回归 | 已通过 SHOWBOX 2027 Debug 编译／ARX 链接；仍需验证列宽／隐藏／DPI、选择身份、刷新草稿和业务字段 |
| S04 | **部分完成**：逐行模型写回已用 `UpdateScope` 合并通知，设置数字字段已迁移；后续可再缩小快照转换范围，不重写任务状态流程 | 已通过 SHOWBOX 2027 Debug 编译／ARX 链接；仍需验证高频进度、任务增删、失败／取消／完成和关闭重开 |
| S05 | 命令面板只补明确缺口；保留已有 ListView、搜索键和字体设置 | 中文组合输入、过滤后上下键／Enter、焦点、分组切换恢复、命令只执行一次 |
| S06 | 每次一页；建议 RoundedRect → Zbcp → RetainingWall，再做注释／详图／材料 | 默认值／配置回填、校验、提交次数、取消语义；复杂 Form 依赖 L02 结论 |
| S07 | 每次一个 LPF pane 或一个 Slab／SwColumn 页面；先列字段与依赖关系 | 多字段回填合并、上下界／派生值一致、不循环保存、不改钢筋业务算法 |
| S08 | **部分完成**：PlotFrameNumber 起始序号、PlotDirectoryFill 文字高度／序号起始已改为 NumberBox；其余打印／导入页面仍按字段逐项迁移 | 已通过 SHOWBOX 2027 Debug 编译／ARX 链接；仍需验证编号／楼层格式、拾取回填和未提交草稿保护 |
| S09 | 每次一个表格配置页；只把真正局部修改由 Reset 改成增量，按需加稳定键 | 排序／过滤／删除后编辑与选择正确，整批载入可继续 Reset，快捷键规则不变 |
| S10 | 优先共享基类，再做有明确收益的小页；可记录“维持现状” | 共用图层／颜色回填和保存正确；查找替换与 IME 行为无回归 |
| S11 | 只核验简单向导与整批识别结果页，有复现证据再改 | 启动命令一次、整批结果更新正确、无无意义重构 |

## 6. 执行顺序与检查边界

建议顺序：L02／L04 的最小复现 → L05、L08 → L06、L07 → L09。L03 已完成首轮低风险优化并通过四项库回归。S01、S02 行高、S03 尺寸、S04 通知与数字设置迁移、S08 数字字段已完成代码实现；S05、S06、S07、S09～S11 先按页面实际字段和复现证据继续，避免形式重构。

库代码改动按 [扩展与验证](.cursor/skills/lumen/references/extend.md) 完成构建和四个检查程序；输入／布局／呈现变化做对应 Gallery 检查，宿主／设备／字体生命周期变化核对 HostCycle 12 轮要求。只修改 SHOWBOX 时按其工程规范验证受影响目标和实际页面，不因一个应用字段改动无条件重跑整个库。

本轮已完成 LUMEN 全量构建及 API、视觉、动画、性能测试；SHOWBOX 2027 Debug 也已成功链接。此前受影响源文件或单个对象的编译成功，不等于 CAD 内验收完成。失败应区分既有问题与本次引入，不能使用旧产物证明新改动通过。

人工验收至少覆盖受影响页面的中文 IME、真实鼠标／滚轮、Tab／Enter／Esc 优先级、DPI／缩放、窗口关闭重开，以及 CAD 中的实际命令。没有做的项目明确标“未实测”，不要写成通过，也不要为了清空清单删除此限制。

## 7. 可直接交给 Spark 的任务说明

复制后填写本轮编号即可。不要一次让 Spark 同时处理整份清单。

```text
请按 .\待办-LUMENUI与SHOWBOX.md 完成本轮工作包：【填写 Lxx 或 Sxx；页面包再指定一个页面】。

先读取两个仓库适用的 AGENTS.md、LUMEN 技能和对应 reference，再核对当前公共头及源码。
保留已有未提交／未跟踪工作，不从历史版本覆盖；本轮只处理指定范围。
文档标为“先复现／待核验”的项先写最小复现，有证据再修；不复现则说明结果。
不要重做已有能力，不机械删除 syncing_ 或把所有 Reset 改掉，不改变业务算法、配置语义、字号和单色设计。
缺失信息若不实质改变结果，自行合理判断并完成；只有无法推断的关键业务语义才提问。
按适用规范完成必要验证；没有实际运行的构建、CAD 操作、IME 或鼠标检查不得报告通过。
完成后更新本待办中该项的状态、涉及文件、验证结果和未完成部分，给出简短交付说明。
不要推送、发布、修改无关配置或往 Gallery 加工程业务页面。
```
