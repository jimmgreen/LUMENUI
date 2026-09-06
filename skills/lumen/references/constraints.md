# LUMEN 业务与实现约束

应用开发读设计语言与布局；库实现另读模块/API 和所涉及的底层章节。路径均相对 LUMEN 仓库根目录。这里的“必须/禁止”是技术契约，不要求用户为普通实现步骤另行授权。

## 设计语言

- 仅暗色纯黑单色体系，accent 恒为纯白；语义靠亮度阶梯与字形，不引入彩色主题、亮色主题、系统强调色或 `WM_SETTINGCHANGE` 主题跟随。
- 发光使用 Theme token：`glow_sm/md/lg`、`spotlight_fill/border`、`specular_line`、`ambient_flare`，统一随 `glow_intensity` 缩放；不在控件里硬编码发光白色 alpha。
- 鼠标追光仅显式开启：`Panel::CardStyle::Lumen` / `Spotlight(true)`；普通控件（包括 Expander/SettingsCard）默认不开追光。默认关闭追光不等于禁止按钮悬停辉光；悬停只增辉，按压才允许中心收缩。
- 聚光卡不得照穿交互控件：`Panel::AvoidControls` 给非命中穿透子级垫回碳底；Label/IconView 等命中穿透内容随光点亮。

## 布局

- 用 `Row`/`Column` 堆叠，`Grow(weight)` 主轴 basis 为 0；`AlignMain`/`AlignCross` 在 Grow 之后对齐。`Column` 交叉轴默认 Stretch。
- `Grid(n)` 等分列；`Grid(1, 0, 1)` 表示 1fr/auto/1fr。页面超出视口用 `ScrollViewer().Grow()`。
- 应用仅对装饰块直接 `SetBounds`；布局容器内部通过 `Panel::MeasureChildAt/ArrangeChildAt` 等 protected 辅助访问子级，不直接访问子级 `Control` 的 protected 成员。

## 模块与公共 API

- `include/lumen/` 是唯一公共 API 面；不暴露 HWND/D2D 等系统类型，保留 `Painter.h` 的 D2D 设备上下文指针例外。几何与颜色使用 `lumen::Rect/Point/Size/Color`。
- `src/core/` 承载渲染、文本、主题、窗口、输入、动画与布局；控件公共头在 include 侧，实现位于 `src/controls/`，一控件一编译单元，保证静态库按需链接。
- 控件实现的项目依赖限于自身公共头、`lumen/Panel.h`、`lumen/Painter.h`、`../core/text_service.h`，需要窗口通道时加 `../core/window_impl.h`；不新增对具体控件的反向依赖。现有组合控件的额外依赖不是扩大此边界的依据。
- 四空格、同花大括号；类型/函数 `PascalCase`、数据成员 `snake_case`、命名空间小写。C++20，`/W4 /permissive- /utf-8`，以零警告为验收要求（不假定构建已配置 `/WX`）。
- 控件属性用 `Value()` / `Value(x) -> Self&`，通过 `ControlOf<D>` / `PanelOf<D>` 保持链式类型。动作使用 `ScrollTo`、`Dismiss`、`Focus` 等动词；不添加旧式 Get/Set 属性别名或 `[[deprecated]]`。已有独立动作如 `SetBounds`、`SetLogSink` 不属于属性别名。
- 控件事件订阅：`OnX(fn) -> Self&` 使用 Subscribe；`BindX(fn) -> Connection`，析构断开，`Release()` 放手。属性绑定 `BindText(Property&)` 等是另一类接口，返回控件引用；不要套用事件连接规则。
- 公共头包含 `// Events:`、`// Keys:`、`// Layout:` 三段说明；`win_undef.h` / `wmain.h` 兼容头按现有例外。注释解释非显而易见的约束。
- `src/`（含内部头）不写中文字面量；用户可见文案来自 `App::Strings()` 或调用方。公共头可以定义默认文案，UTF-8 入口用 `U8("…")`。
- `static const Theme` 显式 `{}` 初始化；不要依赖部分成员的默认初始化覆盖整个 Theme。

## 绘制与动画

- 绘制路径每帧零堆分配；不进入阻塞操作（文件/Shell 访问、图片解码等）。文本与绘制走现有服务，渐变画刷走 Painter 缓存，每帧只 Set 突变，不每帧创建。
- 径向渐变外停靠点使用同 RGB、alpha 0，避免 premultiplied 淡到黑产生黑边。
- 聚光统一用 `Control::Spotlight`、`SpotlightCenter()` 和 `Painter::DrawSpotlight`；窗口输入路由当帧更新 `mouse_local_`，控件不另存聚光坐标、不用动画时钟平滑位置。仅进出渐显平滑 `spotlight_t_`；覆盖 `OnAnimate` 时保留基类推进。
- 指数趋近用 `Control::EaseTo`，有时程位移用 `lumen::Tween` + `Ease`/`CubicBezier`，物理跟手用 `lumen::SpringMotion`（`include/lumen/Animate.h`）。计算在栈上，离屏 setter 将动画 `Snap` 到位。
- 动画时钟在 Paint 中推进并随 `Present(1,0)` 垂直同步，不以 `WM_TIMER` 驱动视觉动画。持续动画仅在悬停、聚焦或显式播放时运行，不常驻 60fps 空转。
- `CreateSwapChainForComposition` 必须 PREMULTIPLIED / STRETCH / FLIP_SEQUENTIAL。帧呈现先 `Present/Present1` 再 `DComp Commit`；脏区帧只将 retain 的脏矩形 `CopyFromBitmap` 到后缓冲，再 `Present1(pDirtyRects)`，保留未更新像素。
- `ResizeBuffers` 前 `dc_->SetTarget(nullptr)` 并释放目标位图；同尺寸短路。`Renderer::Init` 先释放上一套设备链，无 HWND 或空客户区返回 false，避免构造期重复建设设备链后退到 WARP。

## 宿主、字体与生命周期

- 新增进程级调用时在代码中检查 `App::HostMode()`。宿主模式不改进程 DPI、不 `EnableMouseInPointer`、最后一个窗口关闭不 `PostQuitMessage`；窗口通过 `DpiContextScope` 的 PMv2 线程上下文创建。进程设置、全局钩子及 COM 初始化需明确宿主分支和资源归属。
- 窗口类与图标等模块资源用 `LumenModule()`（`app_host.h`），不用 `GetModuleHandleW(nullptr)` 取宿主 exe。类注册走 `EnsureLumenClass`，同名陈旧类先注销；所有窗口销毁且 `App::CanShutdown()` 确认 UIA/后台任务释放后，`App::Shutdown()` 注销全部类并 `UiText().Reset()`，恢复未 Ensure 状态。
- `WindowSpec.owner` 为所有者 HWND（公共类型 `void*`）；`matchDpiHwnd` 用于 `SetParent` 嵌入时匹配 DPI 上下文。模态禁用宿主主窗由调用方负责。
- `WM_KILLFOCUS` 清逻辑焦点并保留恢复目标；`WM_SETFOCUS` 恢复。公开 `Window::ClearFocus()` / `Control::Blur()` 同时清掉恢复目标。
- 原生消息观察用 `OnNativeMessage` / `BindNativeMessage`，在默认处理前调用；公共签名用定宽整数与指针宽度整数，不引入 windows.h。回调不泵消息，也不用轮询 `GetFocus()` 代替消息观察。
- `OleInitialize` / `OleUninitialize` 由 `ole_initialized_` 配对；只释放本窗口成功初始化取得的引用，不多退宿主的 COM 引用。
- `lumatext.dll` 使用延迟加载，首次 `lt_*` 调用前经桥接层 `EnsureLumaTextLoaded()`：显式 `App::LumaTextLibrary` 路径优先，其次含 lumen 的模块目录，最后系统搜索。其他位置不直接调用 `lt_*`；关闭时释放库拥有的加载引用。
- 自定义字体走 `TextService::AddFont` 的 `IDWriteInMemoryFontFileLoader` + `IDWriteFontCollection1`。控件临时换族名用 `Painter::FontFamilyScope`，Measure 与 Draw 都包，不自建 `IDWriteTextFormat`。

宿主验证要求见 [extend.md](extend.md)；应用端调用顺序见 [use.md](use.md)。
