# 控件一致性与普通用户易用性审查

日期：2026-09-19

## 结论与边界

有优化空间，优先级应是“看得出能不能操作、知道键盘焦点在哪、操作结果符合预期”，而非先统一圆角或增加动画。

本轮仅新增审查文档，没有修改控件实现、公开 API、事件契约或之前的性能改动，没有提交或推送。以下“已确认”指源码路径已核对，不代表已经做过实机交互复现。未运行本轮构建、自动化回归、读屏器或真实鼠标/触屏/IME 测试；此前性能回归不能替代这些验收。

范围：枚举了 `src/controls` 下 74 个实现文件，按控件家族跨类别审查。重点阅读了选择类、分页/步骤、滑块、日期时间、表单、标签页/对话框、设置卡、自动建议、空状态和忙碌反馈的相关实现及公共路由。ListView、InfoBar、FileDropZone、按钮变体读取相关路径；GridView、TreeView、NavigationView 等部分大控件以检索筛查为主。不是 74 个控件全部分支或全部交互的验收，也不将未发现问题等同于没有问题。

优先级：P1 为值得先修的可见缺口或明确行为不一致；P2 为需确认产品/兼容性策略的体验增强。两者不是安全严重性等级。

## P1：优先改善

### UX-01：可聚焦，但没有可辨识的键盘焦点

- 已确认：Segmented、Pagination、Stepper 可聚焦，但各自 Draw 没有焦点提示。选择态/当前步骤不能代替焦点态，否则用户不知道 Tab 当前停在哪里。
- 证据：`include/lumen/Segmented.h:37`、`include/lumen/Pagination.h:35`、`include/lumen/Stepper.h:38`；`src/controls/segmented.cpp:103-137`、`src/controls/pagination.cpp:137-164`、`src/controls/stepper.cpp:136-187`。
- 交叉核对：`src/core/draw_tree.cpp:11-36` 不统一补画焦点环；`src/core/control.cpp:402-409` 只是可供控件调用的绘制辅助。
- 建议：复用现有 FocusVisible/PaintFocusRing 机制，按控件语义标识整体或当前操作项；不要依赖悬停、发光或已经选中的颜色。
- 验收：连续 Tab/Shift+Tab 始终可辨识焦点；鼠标点击不无故增加键盘焦点装饰；焦点与选择态能同时辨识；关闭光效仍可见。

### UX-02：禁用态的表达不一致，也要考虑祖先禁用

- 已确认：Segmented 绘制未使用 enabled_；Rating 已填充星仍用 theme.text；Stepper 当前/已完成标记、Pagination 当前页的部分高亮不区分禁用。
- 证据：`src/controls/segmented.cpp:103-137`、`src/controls/rating.cpp:99-115`、`src/controls/stepper.cpp:136-187`、`src/controls/pagination.cpp:137-164`。
- 祖先禁用也需纳入：`src/core/control.cpp:223-230` 只改变自身状态；`src/core/draw_tree.cpp:11-36` 没有统一传播禁用视觉；多数控件依据自己的 enabled_ 绘制。因此禁用外层容器时，子控件可能继续呈现启用外观。
- 这不是“禁用控件仍能正常接收输入”的指控：`src/core/input_router.cpp:89-92` 在鼠标命中时排除禁用子树，`:110-114`、`:388-389` 对键盘检查祖先链。
- 建议：明确有效禁用态的视觉规则，复用主题的 disabled 文本/填充/边框；如需要公共有效状态查询，应独立设计，不能永久覆盖子控件自身 Enabled 值。禁用步骤条仍应表达当前/已完成进度，不能全部画成未完成。
- 验收：自身禁用和父容器禁用都能看出不可操作；重新启用父级后，原本独立禁用的孩子仍禁用；只读与禁用不混为一谈。

### UX-03：关闭光效不应抹去基本轮廓

- 已确认：RadioButton 未选中圆圈边框 alpha 乘以 theme.glow_intensity；值为 0 时边框完全透明，填充又为 theme.bg，在同色背景上失去圆圈提示。
- 证据：`src/controls/radio_button.cpp:45-50`。其已选中禁用边框也有同类耦合，见 `:33-44`。
- 同类一致性候选：`src/controls/toggle_button.cpp:102-121`、`src/controls/drop_down_button.cpp:144-157`、`src/controls/split_button.cpp:65-72` 的部分普通边框也乘光效强度；这些控件仍有填充/内沿，不等同于整个按钮消失。
- 对照：普通 Button 的标准边框采用主题 control_stroke，见 `src/controls/button.cpp:241-248`。
- 建议：基本轮廓与装饰发光分离。先修 RadioButton，再对按钮家族统一核对；不强行给本来就无边框的 Transparent/Subtle 添加边框。
- 验收：光效强度 0/默认、亮/暗主题、启用/禁用、选中/未选中下，仍能识别基本控件及状态。

### UX-04：分页器会给出“可以点，但点了没反应”的提示

- 已确认：CursorAt 对省略号与边界箭头仍返回 Hand；绘制上一页/下一页箭头只考虑整个控件 enabled_，未区分当前是否已经到首尾，且边界箭头仍可获得悬停背景。
- 证据：`src/controls/pagination.cpp:130-164`；Navigate 在 `:29-35` 钳制页码并忽略不变的结果。
- 建议：首尾箭头采用局部禁用外观/光标，省略号作为非操作内容；当前页可保持位置提示，但不要暗示会触发新的导航。
- 验收：第一页、最后一页、仅一页、大量页码含省略号；不可执行的目标不出现手形或可点击悬停反馈，不发出多余翻页事件。

### UX-05：双滑块的反向键盘进入顺序不对称

- 已确认：RangeSlider 的 Tab 分支支持下限与上限间切换，但每次获得焦点都把 active_ 重置为 Lower，不区分正向或反向进入。
- 证据：`src/controls/range_slider.cpp:115-141`、`:169-173`；外层 Shift+Tab 遍历见 `src/core/input_router.cpp:395-405`。
- 源码推导的复现场景：在它后面的控件上按 Shift+Tab，进入下限；再按 Shift+Tab 直接退出，上限被逆序遍历跳过。此场景尚未实机执行。
- 建议：保留双拇指的 Tab 模型，但使正向从下限进入、反向从上限进入；不要仅为该控件在公共路由中增加不受约束的类型特判。
- 验收：前控件 → 下限 → 上限 → 后控件及完全反向序列；鼠标选中拇指、方向键、Home/End、范围边界与上下限重合也保持正确。

### UX-06：复合控件的动作配置存在两处可见不一致

1. BusyOverlay：构造时取消按钮隐藏；OnCancel 订阅后会显示并 Relayout，BindCancel 只连接信号，不更新按钮。保持连接有效时可能出现“Esc 可取消，但没有可见取消按钮”。证据：`src/controls/busy_overlay.cpp:21-27`、`:45-53`、`:69-74`。范围限定：常用 `Window::ShowBusy(text, callback)` 使用 OnCancel，见 `src/core/overlay_host.cpp:203-207`，不受这个特定分支影响。修复时还要考虑连接断开后的按钮状态，不能只补连接时显示。
2. EmptyState：重复调用 Action 会更新按钮文字，却通过 Button::OnClick 累加处理器；InfoBar::Action 则替换 action_cb_。因此同一“动作配置”用法在两种反馈控件中可能一次点击执行旧、新两个动作。证据：`src/controls/empty_state.cpp:59-65`、`include/lumen/Button.h:68-72`，对照 `src/controls/info_bar.cpp:31-42`。建议明确 Action 为配置替换语义并补回归，不要改变普通 OnClick 多订阅契约。

验收：分别使用 OnCancel/BindCancel，保持/断开连接；EmptyState 先配置 A 再配置 B 后点击，按明确后的配置契约只执行当前动作。均尚未运行复现用例。

## P2：统一规则后实施

### UX-07：单选组应更符合常见键盘使用方式

- 已确认：RadioButton 本地只处理 Space，没有组内方向键；同组互斥仅针对同一父级下、同 group 的兄弟。证据：`src/controls/radio_button.cpp:71-107`；公共键盘路由 `src/core/input_router.cpp:355-409` 未补充单选组方向键逻辑。
- 建议：组内方向键移动选择、跳过不可用项，并确定是否循环及是否采用组内单一 Tab 停靠点；保持已有组边界，不能让跨父级同名组意外互斥。
- Segmented 可考虑补 Home/End，与 TabControl 对齐；不是让所有控件使用同一套快捷键。Stepper 仅允许向已完成步骤回退是现有设计，不能为“对称”而用右键绕过业务校验。

### UX-08：表单有效性与“何时向用户显示错误”应分开

- 已确认：FormField 配置 Validate 及 EnsureHooked 会即时校验；有错误就绘制错误文本，并在测量时加入错误区高度。证据：`src/controls/form_field.cpp:60-76`、`:109-135`、`:219-225`、`:265-268`。
- 这不是当前校验契约错误：`include/lumen/FormField.h:95-106` 明确要求首次布局前 Valid 已可信。
- 建议：可选错误展示策略（即时/首次离焦/提交后），内部有效性仍即时计算；提交时展示所有错误并引导首个无效项。可选预留消息高度，避免修正输入时布局频繁移动。新策略应兼容旧默认值，不能直接改现有验证时机。
- 正面基线：NumberBox 解析失败保留草稿并记录错误，而非静默恢复旧值，见 `src/controls/number_box.cpp:155-183`；Gallery 已有提交校验并聚焦字段，见 `examples/gallery/section_overview.cpp:285-288`。
- 验收：未碰过的必填项、输入中、离焦、提交失败、修正、程序赋值后的重验、IME 组字；不得丢失用户草稿。

### UX-09：日期时间的清空入口与默认文案更容易理解

- 已确认：DatePicker/TimePicker 支持 Delete 清空，但本体只画文本和日历/时钟图标，没有独立清空入口。证据：`src/controls/date_picker.cpp:115-155`、`src/controls/time_picker.cpp:322-362`。
- 建议：为允许空值的场景提供可发现的鼠标清空入口，例如可选清空按钮或弹层动作；保留 Delete。不要无条件给必填业务增加清空按钮，也不要把重置默认值与清空混为一谈。
- 默认 placeholder 分别硬编码 Select date/Select time，见 `src/controls/date_picker.cpp:60`、`src/controls/time_picker.cpp:248`；两者已经允许应用覆盖，不是不能本地化。建议接入现有 App::Strings 机制，沿用 BusyOverlay/EmptyState 的默认文案管理方式；日期/时间格式本身继续遵循已有区域格式约定。
- 验收：空值/有值、必填/可选、中文/英文、12/24 小时；清空后绑定值、事件、占位文案一致，图标不互相抢占命中区域。

### UX-10：补足基础辅助功能语义

- 已确认：Segmented、Pagination、Stepper、DatePicker、TimePicker、RangeSlider 的类声明未提供对应 AutomationType/Patterns 覆写，继承默认 Pane / 0 模式，不等于“完全没有 UIA 节点”，但缺少相应控件的操作/值语义。
- 证据：`include/lumen/Control.h:133-144`；各对应公共头；提供程序按虚函数返回值获取模式与类型，见 `src/core/uia.cpp:134`、`:563`。RadioButton/Button 已有专门的类型，可作为库内对照。
- 建议：先明确角色、名称、当前值、可执行操作及状态通知；复合项/双滑块可能需要逻辑子节点，不能只改一个类型枚举。不得为两个值的滑块随便套用一个单值 RangeValue。
- 验收：用实际读屏器/UIA 检查工具确认名称、值、禁用/选中状态和操作，不仅判断“节点存在”；本轮未执行。

### UX-11：窄布局与长文案需要统一退化策略

- 源码线索：CheckBox/RadioButton 的测量不使用 available，按文本自然宽度、固定 28 高度布局，见 `src/controls/checkbox.cpp:41-45`、`src/controls/radio_button.cpp:22-25`。Pagination::RebuildButtons 虽接收 width，却不据此调整按钮集合，见 `src/controls/pagination.cpp:38-61`。
- 定性：这是需要验证的布局适应性风险，不是本轮已观察到的溢出截图；父布局可能保留自然尺寸或滚动，不可直接断言文字必然重叠。
- 建议：为标签约定换行/省略及完整文本获取方式；分页在窄宽度减少页码或采用紧凑模式；保留普通与紧凑密度，不把全部控件硬改成同一高度。SettingsCard 已有文案换行与尾部内容下移策略，可作为参考，见 `src/controls/settings_card.cpp:19-51`。
- 验收：长中文、长英文、200% DPI、窄侧栏、较大字号、带尾部操作；可读范围与实际命中范围一致，不能仅把视觉图标放大后挤压文本。

## 已有能力应保留，不重复造轮子

- 公共输入路由已排除禁用子树、处理失效焦点链、优先处理 IME 的 Esc/Enter/Tab；不得因局部控件没写 enabled_ 判断就宣称可以越过禁用。见 `src/core/input_router.cpp:89-114`、`:355-389`。
- 焦点回调未调用 Control::OnFocusChanged，不足以证明没有焦点动画：公共 SetFocusControl 还会调用 Animate，见 `src/core/input_router.cpp:21-46`。同样，Draw 中写 focused_ 再调用 PaintFocusRing，并不自动意味着鼠标点击必定画键盘焦点环。
- ListView 已有 EmptyTitle/EmptyHint/EmptyAction；Gallery 表格已有无结果提示和 Clear filter 动作。见 `src/controls/list_view.cpp:116-153`、`examples/gallery/section_collections.cpp:179-194`。建议复用，而不是笼统报告“所有列表没有空状态”。
- TabControl 已有左右/Home/End/溢出菜单/关闭快捷键，见 `src/controls/tab_control.cpp:417-440`；Dialog 已有默认/取消命令解析及 Enter/Esc，见 `src/controls/dialog.cpp:99-111`、`:368-379`。
- 程序 setter 是否静默、步骤条是否允许前进、建议项回填是否进入撤销栈属于已有契约，不能为了表面一致性无差别改写；以 `skills/lumen/references/use.md` 和各公共头为准。

## 建议实施顺序

1. 小而明确的首批：UX-01 焦点提示、UX-03 单选轮廓、UX-04 分页边界、省略号；随后补 UX-02 中控件自身的禁用视觉。基本不需要改变公开交互契约，最直接改善“看得懂”。
2. 行为补齐：UX-05 双滑块逆序、UX-06 复合动作配置、UX-07 单选组键盘；先写能复现旧行为的用例，再改实现。涉及事件可重入、连接释放和回调销毁时应沿用现有 WeakRef/连接生命周期规则。
3. 需要公共设计的工作：祖先禁用视觉、辅助功能语义、表单错误展示、清空入口、本地化和窄布局。分小批设计/验收，不一次重构所有控件。

每一批都保留既有 API/事件/布局默认契约；如确需改变，先明确兼容策略。绘制路径不引入每帧堆分配或阻塞操作，不动此前 Painter 性能改动。

## 第一批实施结果（2026-09-19）

本节记录已落地的改动；上文 P1 各项的“现状”描述为修复前的审查结论，保留作为背景。

### 已修复

- UX-01 焦点提示：Segmented、Pagination、Stepper 在键盘焦点可见时通过既有 `PaintFocusRing` 绘制焦点环，沿用 `FocusVisible()` 的键盘模态判定，鼠标点击不追加键盘装饰。
- UX-02 控件自身禁用视觉：Segmented 底板/指示器/文字、Pagination 当前页与页码文字、Stepper 圆点与标题、Rating 已填充星在禁用时改用 disabled 档颜色，并停止绘制悬停层与装饰辉光。禁用步骤条仍保留已完成/当前进度的区分。祖先禁用的统一视觉仍属未完成的公共设计，见 P1 UX-02 剩余部分。
- UX-03 单选基础轮廓：未选中与禁用态边框改用 `control_stroke`/`text_secondary`，不再乘 `glow_intensity`；悬停辉光仍是独立装饰。`glow=0` 时圆圈边界保持可见。
- UX-04 分页边界反馈：新增私有 `Pagination::CanNavigate`，让光标、悬停、点击与箭头着色共用同一可用性判断。首尾箭头、当前页与省略号不再显示手形或悬停填充，也不再发出导航事件；禁用分页器不再响应方向键与 Home/End。

公开 API、事件契约与既有布局尺寸未变化；唯一的头文件改动是 Pagination 新增私有成员函数声明。绘制路径未引入每帧堆分配。

### 验证（本次实际执行）

隔离构建目录 `%TEMP%\lumen-perf-round-hivaRK`，`build.bat` 成功，编译器警告 0，`naming.py OK`。

先在仅加入新断言、未改实现时运行，确认新用例红灯：焦点、禁用、单选轮廓、分页光标/悬停等断言全部失败。实现后复跑：

| 程序 | 结果 |
| --- | --- |
| `lumen_visual_test.exe` | ALL PASS，1542 项，0 失败 |
| `lumen_anim_test.exe` | ALL PASS |
| `lumen_api_test.exe` | 退出码 0，命名检查通过 |
| `lumen_perf_test.exe` | `perf_frame_budget (< 8 ms)` PASS，Draw 路径 C++ 分配 0 |
| `lumen_gallery.exe` | `--glow=0` 与 `--capture-scale=1.5` 截图均成功退出 0 |

新增 `tests/visual/control_usability.h`，在 100/125/150/200% 与 `glow=0/0.5/1` 组合下断言：焦点环出现、禁用整体变暗、禁用忽略悬停与焦点装饰、重新启用后像素与数值还原、只读评分保持正常可读、无文字单选在零光效下仍有轮廓像素，以及分页的光标、悬停像素与事件次数。参考图 `lumen_visual_usability_focus_150.png`、`lumen_visual_usability_disabled_150.png`。

### 本轮未做

祖先禁用的统一视觉传播、UX-05 双滑块逆序、UX-06 复合动作配置、UX-07 单选组方向键，以及全部 P2 项目。真实鼠标手感、IME、读屏器与实机 DPI 切换仍未人工验收；构建与离屏断言不能替代。

## 后续验收清单（本批之外尚未执行）

- 状态矩阵：默认、hover、pressed、键盘焦点、selected/checked、自身禁用、祖先禁用、read-only、空值/错误；区分“当前选择”和“当前焦点”。
- 主题矩阵：暗/亮、glow=0/默认、动画关闭；不只靠颜色或装饰光效传达必要信息。
- 布局矩阵：100/150/200% DPI、窄宽度、长中文/英文、放大字号；鼠标命中与可见目标一致。
- 操作矩阵：鼠标、Tab/Shift+Tab、方向键、Home/End、Enter/Space/Esc、IME；明确各控件适用项而非强制全部支持。
- 数据/事件：未改变值不重复提交；重设动作不执行过时动作；订阅断开和控件销毁安全；上下限/最后一页不产生无反馈假操作。
- 实施后按修改范围运行命名/构建、API、视觉/动画回归，并单独做真实键盘、弹层返回焦点和辅助功能验收。

## 本轮文档核验

报告中的源码路径与行范围依据当前工作区读取结果；仅对新增文档进行格式和引用范围检查，并检查 git diff --check。没有使用本轮未执行的测试结果背书。既有四个性能相关跟踪文件及两份性能报告保持不动。
