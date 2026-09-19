# 更新日志

面向使用者的显著变更，升级注意事项优先。版本遵循语义化：0.x 阶段次版本号代表新能力，修订号代表修复。

## 未发布

暂无。

## v0.4.1 — 2026-09-19

### 修复

- 为 Segmented、Pagination、Stepper 补齐键盘焦点环，改善这些控件及 Rating 的自身禁用态反馈。
- RadioButton 的基础轮廓不再依赖光效强度，关闭光效时仍清晰可见。
- Pagination 统一首尾箭头、当前页和省略号的光标、悬停及点击可用性；禁用时忽略导航键。

### 性能与验证

- 三角形填充复用单位几何并恢复调用方变换，降低连续面积图绘制中的几何重建开销。
- 中长折线按 256 点批量提交，保留两点短线的直接提交路径；不改变连接、端帽与虚线相位。
- 增加多缩放比例、光效强度、禁用/焦点状态和绘制像素一致性回归，以及三角形与折线性能基准。
- 发布前独立 Release 构建、视觉/动画/API/表格回归通过；本次典型场景平均 1.466 ms/帧、最差 1.892 ms/帧，300 帧无 ≥8 ms 帧。
- 历史同机 A/B 中三角形优化使典型场景均值由 1.6128 ms 降至 1.4628 ms；折线收益主要体现在中长路径的 CPU Draw 阶段，不代表所有输入或整帧均提速。详见 [三角形报告](docs/mcp-performance-triangle.md) 与 [折线报告](docs/mcp-performance-polyline.md)。

### 已知限制

- 祖先禁用的统一视觉传播及其余控件可用性待办见 [审查记录](docs/mcp-control-usability-review.md)。
- 真实鼠标手感、IME、读屏器与实机 DPI 切换仍待人工验收；离屏回归不替代实机检查。

## v0.4.0 — 2026-09-19

主题：**表格过滤与分页**、**结构化日志视图**、**实例文字规格**及**输入与生命周期修复**。

### 新增

- Table 增加文本、数值范围、选项与布尔列过滤的元数据、状态及通知接口；过滤由业务侧连接模型或远端请求，表格不隐式改变数据集。
- 新增 `PagedModel` 本地分页装饰器，支持页大小、页码、源索引映射和数据变化后的页码收缩；增加独立表格筛选/分页示例及 Gallery 页面。
- LogView 支持结构化时间、级别、来源、消息和追踪信息，提供搜索、级别筛选/计数、跟随状态通知与完整日志复制；Gallery 增加日志演示。
- 扩展多种控件的实例 `TextRole` 配置；Table 增加列对齐和表头角色，TabControl 支持普通/选中角色，TokenBox 支持独立标签角色。

### 修复与工程改进

- 修复密码框通过 UIA 值接口暴露明文、控件移动后弱引用/绑定失效、Computed 移动后订阅指向旧对象等问题。
- 加强隐藏/禁用祖先焦点链的键盘派发检查；命令快捷键和帧回调连接随所属对象生命周期失效。
- 改善文本框 DirectWrite 回退路径的光标定位与命中一致性，调整 IME 和窗口输入处理。
- 修复冻结列分组头边界、剪贴板写入失败处理、Settings 浮点保存精度及树控件子项边界检查。
- 补齐 Gallery 任务表三列排序，并覆盖排序、筛选重置、空结果和新增任务的组件回归。
- 将固定的 LumaText x64 预编译依赖纳入仓库，本地与 CI 直接复用并校验 SHA256；统一复制依赖许可证。
- `build.bat` 支持指定独立构建目录，避免复用旧机器缓存；清理测试临时产物，忽略本机配置和临时验证证据。

### 升级注意

- 公共类新增字段与接口，请重新编译依赖本库的应用，不混用旧版对象文件或静态库。
- `PagedModel` 是本地分页，不负责远端数据获取；Table 列过滤需要业务侧订阅并更新模型。
- `Computed` 移动赋值已删除；移动构造后的源对象保留可读共享值，但不再持有依赖订阅。
- 新文字规格可能影响自然尺寸，升级后检查紧凑布局、列宽和输入体验。

### 验证与已知限制

- 2026-09-19 独立 Release 构建通过，表格回归 33 条 PASS，API、动画、视觉与性能验证通过；此前原始证据已按用户要求清理，数值保留为历史执行记录。
- 当次性能均值 1.655 ms、最差 8.273 ms，1/300 帧 ≥8 ms；不代表逐帧达标，也不证明历史尖峰已修复。
- 真实显示器 DPI、完整 Gallery 鼠标/键盘/IME 流程、业务宿主验证仍有待验收项。离屏截图与组件断言不替代实机验收。
- 发布前检查见 [v0.4.0 发布准备记录](docs/mcp-release-v0.4.0.md)。正式 Gallery 与 SDK 附件由 GitHub Actions 独立构建并执行隐私模式扫描；该发布工作流不运行测试套件。

## v0.3.0 — 2026-09-10

主题：**控件排版与视觉层次统一**、**Gallery 真实场景示例**及**宿主合成与圆角能力**。

### 新增

- Button、DropDownButton、ToggleButton、TextBox、ComboBox、RadioButton 支持显式 `Role`；NumberBox 保留 Numeric 默认值并尊重显式角色。按钮尺寸与强调种类共用文字映射，输入占位符默认跟随内容角色。
- `WindowSpec.composeToFrame` 支持把子窗内容直接合成到外壳；仅在 `parent == frameTarget` 且子窗铺满外壳客户区时使用，并独占外壳顶层 DComp 槽。`cornerRadius` 指定 Client 内容圆角半径（DIP），同步处理透明角命中与最大化/恢复。
- Gallery 增加项目设置、任务筛选与排序、确认弹窗和 Typography 对照；支持 `--screenshot`、`--small`、密度、光强及截图比例参数。
- 性能测试增加 P50/P95/P99、超预算帧数和绘制/EndDraw 阶段诊断，保留原计时范围与平均值验收条件。

### 调整与修复

- 主题灰阶统一为中性灰，弹层使用独立表面色；普通示例卡静态呈现，页面标题与分组重新分层。Gallery 默认关闭性能 HUD 和装饰背景，F11 / `--perf-hud` 与 `--debug-backdrop` 可显式开启。
- 修复嵌套布局重复应用祖先密度、TabControl 自然尺寸受隐藏页面影响、长对话框标题测量与换行不一致。
- 修复白色主按钮焦点环辨识、关闭光效时部分选择控件的边界反馈，以及表头准备与绘制颜色不一致。
- 显式 Esc 窗口快捷键优先于普通编辑取消；IME 组字与活动弹层仍优先处理，未绑定时保留编辑器原有行为。
- 暗部径向渐变采用更密集的平滑采样，支持时使用浮点精度渐变，保留兼容回退。
- 移除维护脚本中绑定个人电脑的绝对路径；文档中的业务专属描述泛化，增加本地凭据与私钥文件忽略规则。
- Release 附件由 GitHub 独立构建；上传前扫描 ZIP 内的用户目录路径和常见凭据标记，匹配时阻止发布，避免分发带有本机路径的静态库。

### 升级注意

- `CardStyle::Subtle` 默认不再追光；需要该效果时，在 `Card(...)` 后显式调用 `Spotlight(true)`，或使用 `CardStyle::Lumen`。
- 文字角色与密度修复可能改变现有表单的文字宽度和自然高度，升级时检查紧凑布局。新增公共字段后应重新编译依赖本库的应用。
- 宿主使用 `composeToFrame` 时负责外壳布局、边框及合成槽归属；独立窗口与默认嵌入方式保持原有默认值。

### 验证与已知限制

- 本次发布验证记录见 `docs/release-v0.3.0.md`；历史优化与性能诊断见 `docs/optimization-progress.md` 和 `docs/performance-spikes.md`。
- 离屏图像与自动化断言不替代真实显示器 DPI 切换、完整鼠标手感与宿主业务应用测试。此前偶发性能尖峰未被确认为已修复。

## v0.2.0 — 2026-09-06

主题：**原生宿主嵌入**（AutoCAD .arx / MFC / Win32 外壳）与**宿主模式呈现强化**。本次起 Gallery 与 SDK 由 GitHub Actions 在 `windows-latest` 上编译发布（打 `v*` tag 自动出包）。

### 新增：原生嵌入 API

- `WindowSpec.parent`：非空时直接创建 `WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN | WS_TABSTOP` 子窗，DPI 优先匹配父窗（无需再靠 `matchDpiHwnd` 对齐感知上下文）；`NativeHandle()` 始终返回该子窗。嵌入态跳过系统边框修正与 DWM 圆角/暗色 chrome——尺寸、阴影与关闭归外壳负责。
- `WindowSpec.frameTarget`：指定原生外壳后，Client 标题栏命中（`WM_NCHITTEST`）与边框缩放判定路由到外壳；标题栏按钮按外壳最大化状态绘制；`AdjustFrameRect` 用外壳实际外框减客户区的余量（兼容自绘无边框外壳）；`MinSize` 经外壳 `WM_GETMINMAXINFO` 转发给子窗执行。
- `App::HasActiveCallbacks()`（`noexcept`）：原生/UIA/OLE 回调或阻塞菜单、弹层、拖动会话在途的只读快照。宿主在 UI 线程开始破坏性清理前轮询；`false` 不是跨线程卸载锁——仍须阻止新任务、关闭窗口，再用 `App::CanShutdown()` 最终验收。
- 嵌入外壳契约（详见 `skills/lumen/references/use.md` 宿主章节）：
  - 外壳 `WM_SIZE` 将子窗填满客户区；`WM_NCHITTEST` 可转发子窗复用 Client 标题栏；`WM_DPICHANGED` 按建议位置更新外壳后可转发，子窗仅更新 DPI 与布局。
  - MFC 外壳 `PreTranslateMessage` 对子窗及后代返回 `FALSE`，让 LUMEN 处理 Tab/Enter/Esc 与 IME；子窗提供 `WM_GETDLGCODE` 全键盘标志。
  - 子窗 `Close` 先走 `OnClosing`（可否决），通过后异步发送外壳 `WM_CLOSE`；外壳取消时可先转发子窗 `WM_CLOSE` 执行否决逻辑；外壳自己的关闭协议不再回传子窗。窗口析构必须由创建线程完成。
  - Resize 对外壳使用异步 `SetWindowPos`；外壳与子窗避免相互同步等待。

### 变更：宿主模式呈现与弹层归属

- 宿主模式（`App::HostMode(true)`）帧呈现改为 `Present(0, DXGI_PRESENT_DO_NOT_WAIT)`；`DXGI_ERROR_WAS_STILL_DRAWING` 保留 retain 位图与待呈现状态，下次提交以全量拷贝覆盖先前脏区，不再当作设备丢失或立即重试。独立 App 仍随 `Present(1,0)` 垂直同步，行为不变。
- 主窗、菜单与弹层合并为一次性计时唤醒，默认约 60Hz；窗口隐藏或最小化时停止计帧，渲染器析构与关停时摘除计时器。宿主仍无需为 lumen 增加渲染线程。
- 菜单与独立弹层的原生 owner 改挂根外壳（`GA_ROOT`），保持 `WS_EX_NOACTIVATE` 不抢激活；菜单按 owner 的 DPI 上下文创建，混合 DPI 多显示器下弹层缩放正确。
- `WindowSpec.matchDpiHwnd` 语义收窄为「调用方自行 `SetParent` 的兼容路径」；原生嵌入请改用 `parent`（可选加 `frameTarget`）。

### 变更：关停门禁更严格（升级注意）

- `App::CanShutdown()` / `App::Shutdown()` 现在同时等待：原生回调（窗口过程、`MenuWindow::Show`/`PopupWindow::Show`、文本拖放等阻塞会话）清零、OLE 代理对象（拖放目标/数据源等宿主仍可能持有的接口）析构、UIA 无引用、`RunAsync` 任务全部退出。v0.1.0 能通过的卸载路径如果仍挂着拖放或菜单会话，升级后会拒绝关停并抛出——先用 `HasActiveCallbacks()` 轮询、让会话退出，再验收。
- UIA 提供程序的每个 COM 入口（`QueryInterface`/引用计数/各 pattern 调用）都进入回调计数；宿主在 UIA 回调中触发卸载会被门禁拦下，不再静默半卸载。

### 修复

- 窗口类注册数组按 `kClassNames` 实际数量分配：此前 `lumen_popup` 类从不注销，模块重载会遗留陈旧类导致弹层建窗失败。
- 菜单创建的 DPI 上下文跟随 owner（此前用默认线程上下文）。
- 延迟帧重试时合并此前所有脏区，避免 retain 位图部分像素滞留旧内容。

### 构建与接入

- 新增 `LUMEN_USE_PREBUILT_LUMATEXT` 选项：跳过同级 `../lumatext` 源码探测，仅用 `find_package(LumaText)` 接入；`find_package` 分支同时接受安装导出的 `LumaText::Shared`（此前要求 `LumaText::D2D`，lumatext 安装导出并不提供），预编译包路径下运行时拷贝与 SDK 安装照常生效。
- 版本号升至 0.2.0；README 的 FetchContent 示例 `GIT_TAG` 同步为 `v0.2.0`；宿主嵌入章节补充 `parent`/`frameTarget` 与卸载门禁说明。
- SDK 升级：0.2.x 与 0.1.x 同主版本兼容（`SameMajorVersion`），SDK zip 解压替换后 `find_package(lumen CONFIG)` 原样可用。

### 发布工程

- 新增 `.github/workflows/release.yml`：推 `v*` tag 时在 GitHub `windows-latest` 上编译，产出 `lumen-gallery-windows-x64.zip`（exe + `lumatext.dll` + 许可证 + VC 运行库）与 `lumen-sdk-windows-x64.zip`（`cmake --install` 产物），发布说明由 `tools/extract_changelog.py` 从本文件按版本截取。
- CI 不编译 lumatext：从 `lumatext-deps` 预发布下载预编译产物（URL 与 SHA256 在工作流中钉定），经 `find_package(LumaText)` 链接。

### 验证

- `build.bat` 同规格（Ninja/Release）全量构建通过，`lumen_api_test` 命名检查与 README 代码块编译通过。
- `lumen_visual_test` 全部 PASS，含本次新增的原生回调计时泵断言与嵌入子窗生命周期断言（并入 `TestHostCycle` 12 轮建窗、泵消息、Close、Shutdown：无 WARP、无 `WM_QUIT`、加载引用释放、owner 正确、弹层类注销）。
- `lumen_anim_test` 缓动/补间/弹簧断言 ALL PASS；`lumen_perf_test` 1280×800 典型界面（8 按钮 + 100,000 行虚拟列表 + 聚光卡 + Area/Heatmap）本次实测平均 1.606 ms/帧、最差 2.393 ms/帧（预算 < 8 ms），Draw 零 C++ 堆分配。
- Gallery 实机启动冒烟通过（渲染、布局正常）。真实鼠标手感、IME 组合输入、AutoCAD/MFC 实机嵌入为待人工确认项。

## v0.1.0 — 2026-09-03

首个发布。

- Gallery 免编译体验包：解压运行 `lumen_gallery.exe`，自带 `lumatext.dll` 与 VC 运行库。
- Windows SDK zip：`lumen.lib` + 公共头 + `lumatext`，`find_package(lumen CONFIG)` 即可接入。
