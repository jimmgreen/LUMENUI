# LUMEN 移植为 Rust 原生 UI 的可行性报告

评估日期：2026-09-08。基于当前工作区，HEAD 为 `02baeda`；窗口、渲染器和 visual 测试等文件存在未提交修改，因此本文描述的是当前文件状态，不是该提交的纯净快照。评估方式为源码抽样、构建配置与公共 API 检查、官方互操作文档核实；未构建 Rust 原型，未运行性能或实机测试。

## 1. 结论

**可以移植。最适合的目标是保留 LUMEN 的 Windows 自绘路线，以 Rust 实现控件、布局、状态和窗口管理，继续使用 Win32 / D3D11 / Direct2D / DirectWrite / DirectComposition。** 没有发现语言或底层 API 层面的根本障碍，但这是一项 UI 框架迁移工程，不能靠语法转换完成。

如果需求只是让 Rust 应用使用现有 LUMEN，先做安全 Rust 封装的成本明显更低；如果目标是库本身由 Rust 维护，则应重写内核，绑定层最多作为过渡与行为对照。跨平台需要另立范围，不能作为换语言的自然结果。

“原生”需要区分：

| 目标 | 可行性 | 含义 |
| --- | --- | --- |
| Rust 应用调用 LUMEN | 高 | Rust API + C++ 内核，仍是 Windows 原生应用 |
| Rust 实现 LUMEN 内核 | 高，工程量较大 | 不依赖 LUMEN C++ 实现，仍调用系统 API |
| 系统标准控件外观 | 不属于直接移植 | 当前控件以自绘为主，不是每个按钮一个系统 BUTTON 窗口 |
| Windows/macOS/Linux 通用 UI | 有条件可行，成本高 | 要新建平台、渲染、文本、输入与无障碍适配层 |

这里的“Rust 实现”不等于所有依赖均由 Rust 编写：Windows 系统 API 本身仍是外部接口。若要求项目不携带 C/C++ 第三方组件，首版关闭可选 LumaText，使用系统 DirectWrite；开启 LumaText 的版本应明确为混合依赖。

## 2. 仓库证据与规模

按当前目录中的 `.h/.cpp/.py` 物理行数统计，包含注释和空行，不含示例、构建产物和外部依赖：

| 范围 | 文件数 | 行数 |
| --- | ---: | ---: |
| 公共头 include/lumen | 101 | 10,691 |
| 核心 src/core | 45 | 16,319 |
| 控件 src/controls | 74 | 22,071 |
| tests | 7 | 6,803 |

库头文件与实现合计约 **4.9 万行**。74 个控件实现文件不等于精确的公开控件类型数量；不能把文件数直接换算为移植工期。

关键依据：

| 证据 | 对迁移的影响 |
| --- | --- |
| [CMakeLists.txt](../CMakeLists.txt)：C++20 静态库，链接 d3d11、d2d1、dwrite、dxgi、dcomp、imm32、OLE、UIA 等 | 当前明确绑定 Windows；Rust 版可沿用这条系统技术栈 |
| [Core.h](../include/lumen/Core.h)、[Theme.h](../include/lumen/Theme.h)、[Animate.h](../include/lumen/Animate.h) | 几何、主题 token、动画数学可按行为移植 |
| [Panel.h](../include/lumen/Panel.h)：父容器以 unique_ptr 持有子控件，Add 返回引用 | 必须重新设计 Rust 的对象所有权与访问方式 |
| [Control.h](../include/lumen/Control.h)、[Signal.h](../include/lumen/Signal.h) | 虚函数、弱引用、连接自动断开和回调修改订阅都是迁移契约 |
| [Painter.h](../include/lumen/Painter.h)、[renderer.h](../src/core/renderer.h) | 已有绘制集中入口，但 Painter 仍暴露 D2D 指针，并不是现成的平台无关后端 |
| [ime_bridge.cpp](../src/core/ime_bridge.cpp)、[text_box.cpp](../src/controls/text_box.cpp) | 输入法桥接使用 IMM；编辑器还直接调用系统 IME，平台依赖并未全部收敛到 core |
| [uia.cpp](../src/core/uia.cpp)、[window_impl.cpp](../src/core/window_impl.cpp) | UIA 与 OLE 有真实 COM 生命周期；不能只重写绘制和鼠标事件 |
| [Dispatcher.h](../include/lumen/Dispatcher.h) | 已有跨线程投递和关闭后丢弃契约，可用作 Rust dispatcher 的行为规格 |
| [扩展与验证规范](../skills/lumen/references/extend.md) | 已有视觉、性能、动画、API、宿主周期验证入口，可转化为迁移验收资产 |

Microsoft 的 `windows` crate 支持从 Rust 调用 Windows API，官方说明覆盖传统窗口、Direct3D 和 Composition。这支撑保留当前系统后端的判断；不代表本库涉及的每个接口组合已经在 Rust 原型中验证。[Microsoft 官方说明](https://learn.microsoft.com/en-us/windows/dev-environment/rust/rust-for-windows)

## 3. 三条实施路线

### A：安全 Rust 封装现有 C++ 内核

建议结构：`lumen` 安全 Rust API → 私有 FFI 层 → 小型 C++ 适配层 → 当前 LUMEN。

优点是复用现有控件、视觉效果、文本和宿主行为，适合尽快在 Rust 业务项目落地。缺点是仍需维护 C++ 工具链、ABI 边界和两种语言的生命周期；也不能宣称已得到 Rust 实现的 UI 内核。

建议用不透明句柄、定宽标量、指针加长度、明确的创建/释放函数建立窄 C ABI。`cxx` 也是可选互操作工具，但不能直接把当前模板 Add、CRTP 链式接口、wstring 和 std::function API 整体照搬。其内建类型有明确边界，需要另写适配层。[CXX 类型支持表](https://cxx.rs/bindings.html)

重点设计：

- 控件加入父节点后，由树持有；Rust 侧句柄不再重复释放。删除节点时使句柄失效，不能向应用暴露可长期保存的裸指针引用。
- 所有经过封装的删除路径都要维护句柄注册表；如果允许绕过桥接层修改树，则必须增加底层失效通知，否则“安全封装”不成立。
- 事件注册明确上下文指针、调用函数、销毁函数和断开时机，保证上下文只释放一次。处理源先销毁、回调中断开和关闭窗口。
- C++ 异常在适配层转为错误；Rust panic 在回调边界采用明确定义的处理策略，禁止任意跨越外部 ABI。可捕获的 unwind panic 与 panic=abort 必须分开说明。[Rust FFI 与 unwind 规则](https://doc.rust-lang.org/nomicon/ffi.html#ffi-and-unwinding)
- UTF-8/UTF-16 转换在数据变更时完成，避免逐帧重复转换。虚拟列表优先批量取可见行并复用缓存，实测跨语言回调开销。
- 首版以 Windows MSVC x64 为明确目标，核对 CRT、Debug/Release、系统库和可选 DLL。Rust 使用自己的入口启动 App，不机械链接当前强制 wWinMain 的 lumen::main。

### B：Rust 内核 + 原 Windows 后端（目标为 Rust 实现时推荐）

建议逻辑分层，初期不必全部拆成独立 crate：

```text
应用 / Rust builder API
          ↓
控件、布局、属性、事件、虚拟数据模型
          ↓
窗口树、输入路由、焦点、失效与动画调度
          ↓
Windows 后端：Win32 / D2D / DWrite / DComp / IMM / UIA / OLE
          ↓
windows crate + 受控 unsafe 边界
```

保留自绘 retained tree（持有状态的控件树）和现有布局语义，避免同时改成另一套 UI 范式。Rust API 可使用惯常的 snake_case、builder、trait、Result；保留行为而非逐字复制 C++ 命名和继承结构。

推荐窗口持有节点 arena，使用带代际编号的 `ControlId` 访问；节点存父子 ID，运行时集中管理生命周期。控件通过 trait 或受控枚举实现 Measure、Arrange、Draw 和输入逻辑。事件回调收到短生命周期的 UI 上下文和 ID，不捕获永久可变控件引用。

关键约束是：调用用户回调、COM 或可能重入的 Win32 函数时，不持有可能被再次访问的长期可变借用。采用分阶段事件派发与延后树修改，并明确哪些 setter 仍即时生效，保持现有属性提交契约。单纯把所有节点包成 `Rc<RefCell<_>>` 会把借用冲突移到运行时，不能视为问题已经解决。

窗口和控件句柄默认限制在创建它们的 UI 线程；只有经过设计的 dispatcher 可跨线程发送拥有所有权的数据和任务。COM provider 的外部调用需要按其线程模型安全转交 UI 线程，不能靠给整个树添加 Send/Sync 绕过。

### C：Rust 跨平台重构

布局、几何、主题与部分控件算法可以复用设计，但要替换或抽象窗口事件循环、呈现、文本测量与命中、IME、剪贴板、拖放、菜单、托盘和无障碍实现。Painter 的 D2D 类型及控件内 Win32 调用也要收敛。

这属于以 LUMEN 为产品和行为基础开发新的跨平台框架。首期若同时追求三平台、全控件和视觉逐像素一致，风险最高。跨平台渲染方案应单独做文字、辉光、裁剪和缓存验证后再选型。

## 4. 哪些容易，哪些最难

| 模块 | Rust 重写难度 | 主要工作 |
| --- | --- | --- |
| 几何、主题、缓动与弹簧 | 低 | 数值语义、边界和动画曲线对照 |
| Row/Column/Grid 布局 | 中 | 测量约束、Grow、margin、DPI 与失效传播 |
| Button/Label/Badge 等基础控件 | 中低 | 绘制、交互状态、焦点与语义对齐 |
| 树、事件、Property/Connection | 高 | 删除失效、重入、断开、批量更新和引用环 |
| D2D/DComp 呈现 | 高 | 设备丢失、retain、脏矩形、resize、缓存释放 |
| TextBox、文本服务、IME | 很高 | UTF-16 索引、字形簇、选区、撤销、候选框与字体回退 |
| List/Table/Tree/Grid 模型 | 高 | 虚拟化、稳定行标识、编辑、选择和可见区数据访问 |
| 菜单、弹层、焦点、拖放 | 高 | 多窗口、捕获、嵌套回调和关闭次序 |
| UIA、嵌入宿主、模块卸载 | 很高 | COM 引用、线程切换、进程状态隔离、在途回调 |

文字是优先验证项。Rust String 是 UTF-8，而当前 Windows 文本和编辑路径使用宽字符串；不能把字节偏移直接交给 DirectWrite/IME。可先以内存中的 UTF-16 文本缓冲保留 Windows 索引语义，在 Rust API 边界提供 String；另行规定光标按字形簇或字符边界移动。当前实现是否完整覆盖所有复杂文字，也需测试，不能自动认定为已满足。

渲染迁移应保留 [constraints.md](../skills/lumen/references/constraints.md) 的关键行为：纯黑单色与统一光效 token、热绘制路径零堆分配、光标位置当帧更新、动画停止后停止唤醒、正确的脏区呈现和设备恢复。语言更换不保证更快、更小或绝对零 CPU。

## 5. 工作量预估

以下是基于当前规模与风险的工程估算，非已测工期或承诺。假设工程师熟悉 Rust、C++ 和 Win32 图形；维持现有设计语言，不同时重做产品功能。1 人月按约 20 个工作日理解，各路线独立估算，不能相加作为必经流程。

| 交付范围 | 工作量估计 | 说明 |
| --- | --- | --- |
| A：绑定验证原型 | 2–4 人周 | 一个窗口、基础布局、按钮、输入、关闭与回调 |
| A：可用绑定 MVP | 2–4 人月 | 常用 10–15 类控件、事件、分发、构建与示例；不含任意 Rust 自定义绘制控件 |
| A：接近当前库能力的完整绑定 | 5–9 人月 | 复杂模型、宿主、全面 API 与回归，含前述工作 |
| B：纯 Rust Windows 垂直原型 | 1–2 人月 | 窗口、呈现、布局、基础文字输入与生命周期验证 |
| B：可用于有限真实业务的 MVP | 5–8 人月 | 常用控件、输入、虚拟列表、基础 UIA、打包；含原型 |
| B：接近当前库能力的重写 | 12–20 人月 | 全部控件族、复杂文本/模型、宿主与设备生命周期、文档与回归；含 MVP |
| C：三平台接近功能对等 | 24–40+ 人月 | 置信度低，需平台原型重新估算；不是在 B 上简单加固定比例 |

若两名熟练工程师实施 B，完整版本可暂按约 8–14 个日历月规划，关键路径和集成限制使其不能简单除以二。原型完成后重估；缺乏输入法/COM 经验、要求比现库更强的文本或无障碍能力、要求严格纯 Rust 依赖，都可能超过区间。

## 6. 建议的验证与决策门槛

**如果最终目标是 Rust 内核，先做 B 的垂直原型，不必先把 A 全量封装一遍。** 若已有 Rust 业务急需交付，则 A 可以作为独立产品路线。

第一阶段选择 Window、Column/Row、Button、TextBox 和虚拟 ListView，尽早覆盖风险，而不只展示静态按钮：

1. 建窗与绘制：沿用 D2D/DComp；测试 DPI 100%/150%/200%、窗口缩放、最小化恢复、设备资源重建。
2. 生命周期：事件里删除自身、清空父容器、关闭窗口；订阅源先析构、回调中断开；确认无悬空句柄和重复释放。
3. 输入：中文组合输入与候选框定位、日文组合、emoji/代理对、组合字符、选区、撤销、焦点恢复与 Tab 导航。
4. 性能：在同一硬件、字体后端、分辨率和场景下新测 C++ 基线，再比较 Rust；使用现有 1280×800、10 万行虚拟列表场景，沿用平均全帧 <8ms 的门槛，同时记录 p95、分配和空闲唤醒。该门槛不等于所有硬件上的保证。
5. 宿主与无障碍：若首版包含宿主场景，执行至少 12 轮建窗、关闭、Shutdown 与重载检查，核对无额外 WM_QUIT、COM/模块引用释放；验证 UIA Invoke/Value 等已承诺模式及失效节点行为。
6. 交付：干净环境可构建 Rust 示例；绑定版说明 C++ 工具链，Rust 内核版验证关闭 LumaText 后不再编译 LUMEN C++。

继续立项的条件：基础视觉和输入一致、生命周期压力用例通过、性能无不可接受回退、unsafe 边界可审查。若生命周期设计仍需随处裸指针解引用、输入法行为不稳定，或视觉热路径明显退化，应先修原型和重估工期，暂缓铺开其余控件。

## 7. 本次评估边界

已核实：当前源码结构、规模、平台链接项、典型所有权/事件/模型契约、IME 和 UIA 接入点，以及 Rust 调用 Windows 和 C++ 的官方能力依据。

未验证：Rust API 的实际编译可行性、所需 Windows 接口的完整 feature/版本组合、FFI 原型安全性、真实 IME/鼠标体验、Rust 性能与体积、完整文本无障碍覆盖、外部 LumaText 源码及发布依赖清单。本次不宣称这些项目通过。

仓库指令引用的 `.cursor/skills/lumen/` 路径当前缺失，本次读取实际存在的 `skills/lumen/`，未修改技能或同步目录。工作区原有代码修改保留；本次仅新增报告。

**建议立项定位：Windows 优先的 Rust 自绘 UI 库，保留 LUMEN 视觉与行为，先验证内核、输入和生命周期，再迁移完整控件集。**
