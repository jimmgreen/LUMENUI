# LUMEN 仓库规范

## 模块结构

- `include/lumen/` 是唯一的公共 API 面。公共头不暴露 D2D/HWND 等系统类型（`Painter.h` 中的 D2D 设备上下文指针除外），几何与颜色使用 `lumen::Rect/Point/Size/Color`。
- `src/core/` 承载渲染、文本、主题、窗口、输入、动画与布局；`src/controls/` 每个控件一对 `.h/.cpp`（公共头在 include 侧，控件实现文件保持一控件一编译单元，保证静态库按控件链接）。
- 控件实现只允许依赖：自己的公共头、`lumen/Panel.h`、`lumen/Painter.h`、`../core/text_service.h`，需要窗口内部通道时加 `../core/window_impl.h`。不得反向依赖具体控件。
- 渲染、文本、Shell 等阻塞操作不得进入绘制路径；绘制路径每帧零堆分配（渐变画刷必须走 Painter 的缓存 + 每帧 Set 突变，禁止每帧创建）。

## 设计语言（LUMEN 单色光感）

- 纯黑单色体系：仅暗色主题，accent 恒为纯白；语义区分靠亮度阶梯与字形，不引入彩色。亮色主题/系统强调色/`WM_SETTINGCHANGE` 跟随已删除，不要加回。
- 光感 token（`glow_sm/md/lg`、`spotlight_fill/border`、`specular_line`、`ambient_flare`）统一由 `glow_intensity` 缩放；新增发光相关颜色必须走 token，不得在控件里硬编码白色 alpha。
- 径向渐变外停靠点必须用"同 RGB 零透明"收尾（premultiplied 下淡出到纯黑会挂黑边）。
- 鼠标聚光统一走 `Control::Spotlight` + `SpotlightCenter()` + `Painter::DrawSpotlight`；光斑位置由 `WindowImpl::OnMouseMove` 写入 `mouse_local_` 并当帧绘制，不得经动画时钟平滑位置（会慢一拍）；进出渐显只平滑 `spotlight_t_`。控件不得自行另存鼠标坐标。
- 追光是显式开启的聚光卡（`CardStyle::Lumen` / 控件 `Spotlight(true)`）的专属，默认一律不发光（Expander/SettingsCard 也不自开）。聚光卡上光斑不得穿透交互控件：`Panel::AvoidControls` 把非命中穿透子级垫回碳底；Label/IconView 等命中穿透子级视为卡片内容随光点亮。按钮悬停只增辉，不做位移（按压才允许中心收缩）。

## 代码风格

- 四空格缩进、同花大括号；类型与函数 `PascalCase`，数据成员 `snake_case`，命名空间小写（`lumen`）。
- 公共属性用无前缀重载对：`Value()` 读、`Value(x)` 写并返回 `Self&`。动作动词：`ScrollTo`、`Dismiss`、`Focus`。不要保留旧 `Set*`/`Get*` 别名，也不要加 `[[deprecated]]`（`/W4` 当错误）。
- 事件：`OnX(fn) -> Self&`（链式、Subscribe）；`BindX(fn) -> Connection`（RAII，析构断开；`Release()` 放手）。
- 公共头顶部固定三段：`// Events:`、`// Keys:`、`// Layout:`。
- 控件链式 setter 走 `ControlOf<D>` / `PanelOf<D>`，不要再复制 `ToolTip/Grow/Margin` 样板。
- `/W4 /permissive- /utf-8`、C++20；新警告即缺陷，构建必须零警告。
- 注释只写非显而易见的约束（如 DXGI 组合交换链必须 PREMULTIPLIED、Present 先于 Commit、渐变画刷每帧只突变不重建），不写"这行做了什么"。
- 控件状态动画：指数趋近走 `Control::EaseTo`；有时程位移走 `lumen::Tween`（CSS/WAAPI 时长 + `Ease`/`CubicBezier`）；物理跟手走 `lumen::SpringMotion`。曲线与弹簧在 `include/lumen/Animate.h`，计算在栈上、绘制路径零堆。离屏 setter 必须 `Snap` 到位。持续动画只允许在悬停/聚焦/显式播放期间运行，禁止常驻 60fps 空转。聚光由基类 `OnAnimate` 自动驱动。动画时钟跟 `Present(1,0)` 垂直同步（在 Paint 里推进），不要用 `WM_TIMER` 和显示器抢拍。

## 构建与测试

- `build.bat` 全量构建（vcvars64 + Ninja + Release）。注意：直接调 `cmake` 前必须先有 vcvars 环境，否则会静默使用旧产物。改完代码必须重跑：
  - `lumen_visual_test.exe`：暗色控件状态板 PNG + 像素断言（含聚光亮度断言），要求 ALL PASS。
  - `lumen_perf_test.exe`：1280×800 典型界面（含聚光卡）全帧重绘，预算 < 8 ms/帧（当前基线约 0.17 ms）。
  - `lumen_anim_test.exe`：缓动/补间/弹簧数值断言 + 曲线 PNG，要求 ALL PASS。`--live` 对照曲线（与 gallery 同一 vsync 时钟）。
  - `lumen_api_test.exe`：控件链式 setter 编译回归（`Add<T>().Margin().Visible()...`）。
- 作为子项目时 `LUMEN_BUILD_EXAMPLES` / `LUMEN_BUILD_TESTS` 默认关。链接名 `lumen` 与 `lumen::lumen` 等价；`lumen::main` 提供 `wWinMain` → `lumen_main`。外部 exe 请调 `lumen_copy_runtime(target)` 拷贝 LumaText DLL。
- 影响输入/布局/呈现的改动，额外运行 `lumen_gallery.exe` 做实机检查：悬停辉光、聚光跟随手感、按压缩放、焦点环、Tab 导航、DPI 缩放、窗口缩放、光效强度档位。
- 无法自动化的检查（真实鼠标手感、IME 输入）必须显式记录为待人工确认项。

## 已知关键约束

- `CreateSwapChainForComposition`：AlphaMode 必须 PREMULTIPLIED、Scaling 必须 STRETCH、SwapEffect 必须 FLIP_SEQUENTIAL（Present1 脏区要保留未更新像素）；呈现顺序必须 `swapchain->Present/Present1()` 再 `DComp Commit()`，缺 Present 就是白屏。脏区帧用 `CopyFromBitmap` 只拷 retain→后缓冲的脏矩形，再 `Present1(pDirtyRects)`。
- `ResizeBuffers` 前必须释放 D2D 目标位图（`dc_->SetTarget(nullptr)` + reset），同尺寸直接短路。
- 布局：`Row`/`Column` 堆叠，`Grow(weight)` 主轴 basis 为 0（两列 `Grow(1)` 等宽），`AlignMain`/`AlignCross` 在 Grow 之后对齐；`Grid(n)` 等分列，`Grid(1, 0, 1)` 为 1fr/auto/1fr。页面超出视口用 `ScrollViewer`（`.Grow()` 吃剩余高度）。装饰块才 `SetBounds`。布局容器通过 `Panel::MeasureChildAt/ArrangeChildAt` 等 protected 辅助操作子级（友元代访问），不要在子类直接摸 `Control` 的 protected 成员。
- `static const Theme` 类的静态常量必须显式 `{}` 初始化（Theme 的 Color 成员无默认值，否则 C2737）。
