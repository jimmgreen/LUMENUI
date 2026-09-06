# 用 LUMEN 写应用

视觉与布局遵守 [constraints.md](constraints.md) 对应章节。下面的路径相对 LUMEN 源码根目录；复制技能到应用工程后，从所接入的库中查找公共头与示例。

## 接入

```cmake
# 源码；也可通过 FetchContent 接入并固定发布标签
add_subdirectory(path/to/LUMENUI)
lumen_add_executable(myapp main.cpp app.rc)

# 或预编译 SDK（替代上面的 add_subdirectory）
# set(lumen_DIR "C:/libs/lumen-sdk-windows-x64/lib/cmake/lumen")
# find_package(lumen CONFIG REQUIRED)
# lumen_add_executable(myapp main.cpp app.rc)
```

`lumen_add_executable` 设置 WIN32、链接 `lumen::lumen` / `lumen::main`、拷贝启用的 LumaText 运行库并配置 `/utf-8`。自行创建 target 时使用 `lumen_copy_runtime(target)`；源码项目的 `lumen` 与 `lumen::lumen` 等价。模板见 `examples/template/`。

CMake 入口由 `lumen::main` 提供 `wWinMain` → `lumen_main`；非 CMake 可用 `<lumen/wmain.h>` + `LUMEN_MAIN()`，此时不再链接 `lumen::main`。按需 include 控件头；`lumen/lumen.h` 可用于演示，静态库按控件链接由编译单元边界保证。

```cpp
#include <lumen/Button.h>
#include <lumen/Main.h>
#include <lumen/Window.h>
int lumen_main(std::span<const std::wstring_view>) {
    return lumen::Run(L"演示", [](lumen::Window& w) {
        w.Root().Add<lumen::Button>(L"你好", lumen::ButtonKind::Primary);
    });
}
```

`Window(title)` 默认 Client 帧、`Backdrop::All`、960×640；三参构造不自动使用这些外观默认值。构造会 `App::Ensure()`，宿主模式见下文。

声明式嵌套用 `Children(...)`，`Ref(ptr)` 取得控件指针。布局、Grow、Grid 和 ScrollViewer 语义见 constraints.md，使用 `Density::Compact` / `Comfortable()` / `Dense()` 调整密度。

## 绑定、事件与提交

`BindText(Property&)`、`BindChecked`、`BindSelectedIndex`、`BindValue`、`BindEnabled`、`BindVisible` 是属性绑定，返回控件引用。事件的 `BindX(fn)` 返回 `Connection`，丢弃会立即断开；持久链式事件订阅用 `OnX(fn)`，手动管理生命周期则保存 Connection/ScopedConnection。绑定的 Property 和模型对象应活过使用它们的控件；`FilteredModel` / `SortedModel` 装饰源模型。

| 控件 | 程序赋值 | 用户操作与提交 |
| --- | --- | --- |
| TextBox | `Text(x)` 同值不打断 IME、光标或撤销；换值重置但不发 TextChanged，选区变化仍可能发 SelectionChanged | 打字/粘贴/IME 提交发 TextChanged；选区变化发 SelectionChanged；Enter 提交动作走 OnSubmit |
| NumberBox | `Value(x)` 钳制、格式化，不发 ValueChanged，非有限值忽略 | 步进从当前合法草稿出发，非法草稿用最近提交值；方向键/失焦/UIA 提交有效变化，同值不重复通知；UIA 非法文本拒绝且保留原值 |
| Switch / CheckBox / ToggleButton | `Checked(x)` 静默 | 用户切换发 Toggled |
| ComboBox / Segmented | 有效 `SelectedIndex(x)` 选择变化发事件；ComboBox 多选重设还会重建选集并通知 | 选择即提交，回调同步须防循环 |
| Slider | `Value(x)` 静默 | 拖动连续发 ValueChanged |
| Form / FormField | 程序改值后 `ValidateAll()` 重验；Child/Validate 配置阶段建立字段挂钩 | 输入和数字提交自动重验；提交前仍须 `ValidateAll()`，不只看 `Valid()` |

- Form 字段示例：`Field(L"名称").Validate(validate::Required()).Add<TextBox>().BindText(name)`，提交按钮可 `BindEnabled(form.Valid())`。`validate::Rule` 的独立 struct 支持 `|` 组合，不改成 std::function 别名。
- 焦点观察用 `OnFocused([](bool){})`；IME 组合态用 `TextBox::Composing()`，不为转发焦点单独派生控件。
- `Confirm` / `Prompt` 用回调；不在 UI 线程 `future.get()` 阻塞消息泵。
- `RunAsync` 返回 TaskHandle：Cancel 协作取消，Status/Error 查询终态，异常走 OnTaskFailed，窗口销毁后结果 Dropped。跨线程回 UI 用 `Window::Post`；返回 Closed/WakeFailed 表示拒绝且不会执行。

## Table 契约

先 `Bind(VectorModel<T>&)` 再 `Column(title, &T::mem)`。嵌套列类型为 `ColumnDef`；`SelectedIndex()` 是排序/展开后的视图行，数据行用 `SelectedDataIndex()`（ListView 同理）。

- 数字成员默认文本显示、数值排序；精度用 `ColumnPrecision`，自定义数字访问用 `BindNumber`。进度条必须显式 `.Progress(get)`。
- 三参 BindNumber 或类型化数字成员默认可编辑；两参 BindNumber 只读。`CellEditable(谓词)` 限定逐格编辑。Enter/Tab 严格解析：非法保持编辑器供修正，失焦非法回退不写回；Tab/Shift+Tab 按视图序跳到下一可编辑格。
- 排序激活时模型变动实时重排；单元格编辑期间延后到编辑结束，避免输入行移动。未排序的少量 `At(i,v)` 更新只失效可见受影响行与页脚；页脚数字聚合用数值访问快路径并遵守列精度，不固定 HUD dirty 数量。
- 类型化数字成员按目标类型校验：整数拒绝小数和越界值，浮点成员拒绝溢出；Enter/Tab 拒绝后保留草稿。当前数字通道仍基于 double，不承诺超过 2^53 的整数精确编辑。
- 基础模型先销毁时，FilteredModel/SortedModel 变为空模型并通知视图；Table 清除类型访问闭包、取消编辑并清空行。模型与视图的变更/销毁仍在 UI 线程进行。
- 布局查询用 `ColumnPixelWidth` / `ColumnsPixelWidth` / `NaturalHeight`，不复制表头、滚动条和弹性宽度常量。

## 浮层与宿主

Toast / Dialog / Drawer 已有亚克力与海拔，不叠加自制半透明黑遮罩。Primary 按钮白底，Danger 用单色警示；追光需按 constraints.md 显式开启。

`Window::ShowPopup(content, anchor, width, closed)` 借用未挂载的控件树，阻塞至收起；调用期间内容须存活。每个 UI 线程只允许一个会话，重入请求被忽略，不排队。锚点跟随主窗布局/移动，外点、Esc、`ClosePopup()`、锚点失效或主窗隐藏/销毁会收起。正常收起后在 UI 线程调用 `closed`；owner 销毁时不调用。返回后内容解除窗口绑定；超高内容的滚动和复杂编辑器 IME 不属于当前已验收能力。

AutoCAD .arx / 插件 DLL：

- 第一个窗口前 `App::HostMode(true)`，`WindowSpec.owner` 传宿主 HWND；嵌入子窗按需要设置 `matchDpiHwnd`。卸载前所有 Window 析构，确认 `App::CanShutdown()` 后 `App::Shutdown()`；UIA 引用或后台任务未结束时不得卸载模块。
- RunAsync 的结果状态不等于线程已经退出；`RunningTasks()` / `CanShutdown()` 会等待系统线程退出（含闭包和 TLS 析构）再回收。后台函数及其线程局部对象不得无限阻塞，不能绕过门禁直接卸载。
- 宿主自行泵消息，不创建 `App app` 或调用 `App::Run` / `lumen::Run`；跨线程使用 Window::Post。
- 窗口失焦自动清焦点并在重新聚焦时恢复；`Window::ClearFocus()` / `Control::Blur()` 显式清除后不恢复。原生消息用 OnNativeMessage/BindNativeMessage，回调内不泵消息。
- `App::AddFont(bytes | path)` 返回族名，供 `Label::FontFamily` / `RichLabel::Font` 使用。`App::LumaTextLibrary(path)` 在首窗创建前设置且显式路径优先，也可将 DLL 放在 .arx 旁。
- 宿主主窗的模态禁用与恢复由调用方管理。

## 验证与排障

应用代码改动须构建成功并验证受影响的界面/交互；窗口变化检查相关 DPI、Tab 焦点与已开启的聚光。纯文档改动仅核对链接和接口；不触发整库回归。不能自动验证的真实鼠标手感、IME 或宿主操作列为待人工确认。

用 `LUMEN_LOG=path` 或 `SetLogSink` 查看诊断；Gallery F12 可 `DumpTree`。文字质量异常时核对 LumaText 是否启用、DLL 路径/依赖及日志，不把所有问题都归因于漏拷 DLL；库允许回退 DirectWrite。
