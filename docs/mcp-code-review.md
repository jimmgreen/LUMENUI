# LUMEN 全库代码审查

范围：当前工作区全部库代码（`include/lumen/`、`src/`、`tests/`、构建脚本），含未提交改动，基线 `18d8a3c`。方法：`AGENTS.md` + `skills/lumen/` 规范比对、人工审计、独立探针实测（MSVC 19.51 / VS 2026 v18.9，`%TEMP%/lumen-mcp-review-L5uqJe` 独立构建目录）、全量测试套件。审查未修改任何库实现。

## 结论摘要

| 严重度 | 主题 |
| --- | --- |
| 高 | 密码框 UIA 明文暴露；移动语义使绑定/弱引用/派生值失效；焦点安全两条 |
| 中 | ItemsModel 聚合丢删除语义；Command 快捷键裸引用；OnFrame 连接裸指针；冻结列分组头未实现（2 项测试失败）；Settings double 精度；WM_CHAR 裸指针 |
| 低 | Button 赋值残留连接、SingleInstance/ReleaseMsgWindow/EnsureMsgWindow 边界、Mask 死代码、剪贴板返回值、child_at_ 防御 |
| 卫生 | AGENTS.md 技能路径失效；根目录临时产物；build/ 陈旧缓存 |

## 高

### H1 PasswordBox 经 UIA 明文暴露

- `include/lumen/TextBox.h`（AutomationValue，约 118 行）直接返回 `text_`，未按 `password_` 脱敏；`src/core/uia.cpp` 的 ValuePattern `get_Value` 原样透传。`AutomationIsPassword()`（约 125 行）同时如实上报，讲述屏幕等 UIA 客户端读 Value 即得明文。
- 实测：`password_is_password=1 password_value_is_plaintext=1`。剪贴板路径已正确拦截（`CopyOrCut` 对 `password_` 返回），仅 UIA 通道泄漏。
- 建议：`AutomationValue()` 在 `password_` 时返回 `VisibleText()`。

### H2 移动语义使绑定、WeakRef、Computed 失效

- `Control` 移动会迁移 `bind_visible_`/`bind_enabled_` 连接（`src/core/control.cpp` StealFrom 末尾），但回调捕获的是源控件 `this`，移动后属性变化仍作用于源对象；`WeakRef`（WeakLink 挂在对象地址上）不随移动重挂，指向旧存储；把控件 move-assign 进存活控件后，既有 WeakRef 析构会触碰已释放内存。
- `include/lumen/Signal.h`（Computed，约 257 行起）的 `deps_` 连接捕获构造时的 `this`，移动后依赖断链：实测移动后 `computed_moved=2 original=6`。`Signal` 自身移动经 `alive_` 令牌是安全的，问题集中在"捕获 this 的回调 + ScopedConnection"组合随对象移动。
- 实测：`binding_moved_enabled=1`（新控件未跟随禁用）、`weak_after_move_points_to_original=1`、`weak_after_move_assignment_and_destruction_nonnull=1`（探针以故意泄漏规避崩溃）。
- 建议：要么文档明确控件/Computed 构建后禁移动并在移动路径加 `DebugTrap`，要么让连接与 WeakLink 在移动时重挂。现有测试仅覆盖 ToolTipDelay 存活（`tests/visual/main.cpp` "tooltip delay survives control move"），未覆盖绑定/WeakRef/Computed。

### H3 隐藏或祖先禁用的聚焦控件仍响应键盘；显式 Blur 后焦点被恢复

- `src/core/input_router.cpp` OnKeyDown（约 372 行）直接 `focused_->OnKey(vk)`，不校验焦点链 `visible_`/`enabled_`；`CollectFocusable` 有校验但仅用于 Tab 遍历。实测：隐藏的聚焦按钮 Enter 仍触发 `OnClick`（`hidden_button_clicks=1`）；父 Column `Enabled(false)` 后子按钮仍触发（`disabled_parent_button_clicks=1`）。
- `src/core/input_router.cpp` OnHwndFocus（约 63 行）在 WM_SETFOCUS 无条件恢复 `focus_restore_`，而 `Window::ClearFocus()` / `Control::Blur()` 的契约是"显式清除后不恢复"（`include/lumen/Window.h` 77 行附近、`skills/lumen/references/use.md`）。实测 `blur_restores_old_focus=1`。
- 建议：键盘派发前校验焦点链可用性；`Blur()`/显式 `ClearFocus()` 清除恢复目标或加"显式清除"标志。

## 中

1. `include/lumen/ItemsModel.h`（DeferMutation，约 72 行）：同一 UpdateScope 内首个非 reset 事件固定聚合类型，`NotifyRemoved()` 后 `NotifyReset()` 被降级为 `changed` 通知，视图错过结构变化（`Table::PasteRow` 等 UpdateScope 内路径受影响）。建议聚合升级规则改为 reset 优先。
2. `src/core/window_impl.cpp`（Window::Bind(Command&)，约 575 行）：以 `[&command]` 裸捕获注册快捷键，无解绑 API；Command 先于 Window 析构后触发快捷键即悬垂。建议仿 `Button::RebindCommand` 的 OnDestroyed 模式。
3. `src/core/window_impl.cpp`（WindowImpl::OnFrame，约 1477 行）：返回的 Connection 持有裸 `WindowImpl*`，无存活令牌；持有者析构晚于窗口时断开触碰已释放对象。建议仿 `Signal::alive_`。
4. `include/lumen/Table.h`（GroupHeaderContentRect，约 513 行）：桩实现忽略 `frozen_width`。新增 `tests/api/table_filter_paging.cpp`（未跟踪文件）断言 x=10+frozen、窄视口收缩，构建结果 2 项 FAIL；`tests/visual/main.cpp`（约 5085 行）旧断言期望 x=10 全宽，两处验收标准互相矛盾，属未完成的中途变更。建议先统一冻结列分组头规格，再实现并同步视觉断言。
5. `src/core/settings.cpp`（Put(double)，约 90 行）：`std::to_wstring(double)` 只有 6 位有效数字，`Persist(Property<float>)` 持久化的主题/布局数值回读失真。建议改 `%.9g` 或 `std::format`。
6. `src/core/window_impl.cpp`（WM_CHAR，约 1141 行）：`focused_->ImeComposing()` 与 `focused_->OnChar(...)` 之间无 WeakRef 保护，OnChar 弹出模态菜单/销毁控件即悬垂；OnKeyDown 等链路已普遍使用 WeakRef，此处遗漏。

## 低

1. `src/controls/button.cpp`（operator=，约 55 行）：未清源对象 `cmd_destroyed_`（move 构造有清），源按钮随后析构会向已销毁 Command 发 OnDestroyed（ScopedConnection 使其为 no-op，但语义不洁）。
2. `src/core/app.cpp`（SingleInstance，约 320 行）：`CreateMutexW` 失败仍返回 true；重复调用覆盖 `g_single_mutex` 句柄泄漏。
3. `src/core/app.cpp`（ReleaseMsgWindow，约 79 行）：`DestroyWindow` 失败抛异常，位于 `App::~App`/`App::Shutdown` 关机关键路径。
4. `src/core/app.cpp`（EnsureMsgWindow，约 57 行）：不检查 `CreateWindowExW` 失败，热键/单实例静默失效。
5. `src/controls/text_box.cpp`（Mask，约 303 行）：空 if 分支死代码。
6. `src/core/clipboard.cpp`（Text 写入，约 23 行）：忽略 `SetClipboardData` 失败仍返回 true。
7. `src/controls/tree_view.cpp`（约 130 行）与 `src/controls/tree_table.cpp`（约 126 行）：`child_at_` 直接下标访问 `flat_children_[id][index]`，仅靠 `child_count_` 前置检查保护；与库内其余边界处理风格相比偏弱。

## 仓库卫生

- `AGENTS.md` 引用 `.cursor/skills/lumen/SKILL.md` 及 references，实际文件位于 `skills/lumen/`，全库无 `.cursor/`，入口指引失效。
- 根目录散落 `textbox-*.log` / `textbox-gallery.png` 等临时产物（git 未跟踪）。
- `build/CMakeCache.txt` 记录旧机器用户目录路径，`build.bat` 直接复用即失败；本次审查改用 `%TEMP%` 独立目录验证，未动该目录。

## 验证与测量（本次实机）

- 全库（含测试与 README 片段检查）Release 构建通过，`/W4 /permissive-` 零警告，bundled LumaText 预编译 shared 接入。
- 探针 7 项断言全部按上述结论复现；测试套件：`lumen_api_test` PASS（naming.py OK）、`lumen_anim_test` ALL PASS、`lumen_visual_test` ALL PASS（含 UIA 与 TestHostCycle）、`lumen_perf_test` PASS（1280×800 场景平均 1.638 ms/帧，最差 3.191 ms/帧，预算 < 8 ms）、`lumen_table_filter_paging_test` 2 FAIL（见 中-4）。
- 未运行：`lumen_gallery` 实机、鼠标/IME 手感、多显示器 DPI 实机检查；上述待人工确认，不视为通过。

## 覆盖说明

深读：Signal/UpdateScope/Control/Panel/layout、input_router、window_impl、renderer、painter 关键路径、app、clipboard/settings/timer/ime、ItemsModel 族、Table、TextBox/PasswordBox、Button/RepeatButton、PageHost、TreeTable、LogView、FormField 及各控件事件发射点扫描。浏览：其余控件头与实现抽查。未深读：menu_window、overlay_host、popup_window、uia 全量、chart、gallery、third_party。

## 修复记录与勘误（第二轮，2026-09-18）

本轮对上文发现实施修复，并在同一独立构建目录（`%TEMP%/lumen-mcp-review-L5uqJe`，未动仓库 `build/`）完成全量重建与实测。原发现保留原样作为审计记录，冲突处以本节为准。

### 已修复

- 高-1（H1）：`include/lumen/TextBox.h` `AutomationValue()` 密码态返回掩码文本，非密码态仍返回全文。
- 高-2（H2）：`Control` 移动经 `StealFrom` 重挂弱链（`WeakLink` 增加 `rebind` 回调，`WeakRef::Reset` 装填；move-assign 把目标原有弱链并入链尾，目标析构时既有 `WeakRef` 正确清空）并重建两个绑定连接。`Computed`（`include/lumen/Signal.h`）改持 `shared_ptr<Property<T>>`，recompute 捕获值指针而非 `this`；移动构造转移依赖订阅，moved-from 不再更新但 `Get()` 仍可读（值共享），移动赋值删除。
- 高-3（H3，键盘侧）与 中-6：`src/core/input_router.cpp` 新增 `IsFocusChainUsable`（沿 `parent_` 校验 `visible_`/`enabled_`），OnKeyDown 派发前校验；`src/core/window_impl.cpp` WM_CHAR 加同样校验并以 WeakRef 兜底。
- 中-2：`Window::Bind(Command&)` 改为 `BindCommand`：`Command::OnDestroyed` 时撤销快捷键并清空绑定，注册前惰性清理失效条目（`command_shortcuts_`）。
- 中-3：`WindowImpl` 持 `frame_alive_`（`shared_ptr<SignalAlive>`），析构置空；`OnFrame` 返回的 Connection 携带令牌，回调先验证再执行。
- 中-4：`Table::GroupHeaderContentRect` 从 `absolute_.x + max(0, frozen_width)` 起算、宽度收缩不为负；窄视口（不宽于冻结带）收缩为 0，分隔线不再横穿分组头。
- 中-5：`Settings::Put(double)` 改 `swprintf` `%.9g`。
- 低-1：`Button::operator=` 清空源对象 `cmd_destroyed_`。
- 低-2/3/4：`src/core/app.cpp` SingleInstance 重复调用先关闭旧句柄、创建失败告警并返回 false；EnsureMsgWindow 失败告警；ReleaseMsgWindow 失败告警不抛。
- 低-5：删除 `src/controls/text_box.cpp` Mask 空分支死代码。
- 低-6：`src/core/clipboard.cpp` 写入在 `SetClipboardData` 失败时释放内存、关剪贴板并返回 false。
- 低-7：`src/controls/tree_view.cpp`/`tree_table.cpp` `child_at_` 增加 id/index 边界检查，越界返回空。
- 卫生：`AGENTS.md` 全部 `.cursor/`/`.grok/` 失效路径改为实际 `skills/` 布局（含文末维护源说明行），并清理行尾空格。

### 勘误

- 中-1（ItemsModel 聚合丢删除语义）为误报：`ItemsModel::NotifyRemoved()` 本就以 `DeferMutation(true, …)` 聚合为 reset，UpdateScope 内删除语义不丢；代码未改。
- 高-3 的 OnHwndFocus 无条件恢复焦点未修复：涉及 `ClearFocus`/`Blur` 契约的产品语义，待决策后另行处理；键盘派发侧已如上修复。
- 根目录 `textbox-*.log`/`textbox-gallery.png` 临时产物与 `build/` 陈旧缓存未清理：属破坏性清理，未获授权不执行。
- 2026-09-19 后续获得用户清理授权：根目录上述临时产物及生成的测试截图已清理；测试源码、正式图片、构建缓存与可执行产物仍保留。此项更新仅对应文件清理，不改变前述代码审查结论。

### 回归测试与验证

- 新增 `tests/visual/main.cpp` `TestReviewFixes`：密码 UIA 掩码、Control 移动后 WeakRef 跟随与绑定重挂、Computed 移动语义、隐藏/禁用焦点链键盘安全、UpdateScope 内 RemoveAt 后 Table 行数。
- `tests/api/table_filter_paging.cpp` 冻结列分组头断言按新语义改写（x 起算于冻结分隔线之后、窄视口宽度收缩为 0）。
- 探针断言全部翻转：`password_value_is_plaintext=0`、`binding_moved_enabled=0`、`weak_points_to_moved=1`、`computed_moved=6`（moved-from 可读）、`hidden_button_clicks=0`、`disabled_parent_button_clicks=0`、`usable_focus_clicks=1`、`weak_cleared_after_destination_destroyed=1`。
- 全量 Release 重建 `/W4` 零警告，naming.py OK；`lumen_api_test`、`lumen_table_filter_paging_test`、`lumen_anim_test` PASS，`lumen_visual_test` 连续三轮全过，`lumen_perf_test` 平均 1.63 ms/帧（预算 < 8 ms）。
- 已知环境抖动：`TestStructuredLogView` 剪贴板断言在桌面剪贴板管理器活跃时可能瞬时失败（`CopySelection` 成功但系统剪贴板随即被外部进程改写；隔离复测 8 次读数稳定，同二进制复跑即恢复）。已将断言改为单次快照/字面量对比以降低敏感度，未弱化校验。
- 仍未运行：`lumen_gallery` 实机、鼠标/IME 手感、多显示器 DPI 实机检查；上述待人工确认，不视为通过。
