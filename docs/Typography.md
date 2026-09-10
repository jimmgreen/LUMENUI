# LUMEN 控件字号规范与审查清单

参考 Fluent 2 Typography（https://fluent2.microsoft.design/typography）及 Material 3 Typography
（https://developer.android.com/design/style/typography.html）的语义分层原则。
LUMEN 使用 Windows DIP，不把网页 px 或移动端 sp 直接当成物理像素；DPI 缩放由渲染层处理。

## 统一规格

| 用途 | 角色 | DIP | 字重 |
| --- | --- | --- | --- |
| 页面/对话框主标题 | Title | 20 | 半粗 |
| 卡片主标题 | Subtitle | 16 | 半粗 |
| 分组标题、选中导航、正文强调 | BodyStrong | 14 | 半粗 |
| 标准标签、输入、选择、操作文字 | Body | 14 | 正常 |
| 紧凑正文、辅助说明、徽章、图表轴标、表格单元格 | Caption | 12 | 正常 |
| 紧凑正文强调、表格/列表分组标题 | CaptionStrong | 12 | 半粗 |
| 数字输入 | Numeric | 14 | 正常，数字排版 |
| 日志/代码 | Mono | 12 | 正常 |
| 展示数字、辅助分类 | Display / Overline | 48 / 11 | 按原角色 |

同一表单行内的标签、值、选择说明和动作必须使用同一规格，不能将 12 DIP 输入与
14 DIP 行内按钮混搭。标准表单 14 DIP；高密度工程设置允许整组 12 DIP。
标题、说明和正文仍保留语义角色，不把全局 Body 改成 Caption。
Small 按钮默认 12 DIP，Medium/Large 默认 14 DIP；大小档位不要求每档都增大文字。
Primary/Danger 只增加字重。显式 Role 可调整文字而不改变按钮高度与点击区域。
禁用状态只调整对比度，选中状态不得改变字号。图标不按正文字号压缩。

## 本次调整

- Button / DropDownButton / ToggleButton 共用按钮文字映射，并支持 Role 覆盖。
- Small Primary/Danger 使用 CaptionStrong，和标准按钮保持相同的强调规则。
- Button 移动构造和赋值保留显式字号；Kind 改变时重新测量文字。
- FormField 默认标签改用 Body，与 Label、TextBox、CheckBox、RadioButton 一致。
- TextBox 占位符默认跟随内容角色，显式 PlaceholderRole 仍优先。
- NumberBox 保留默认 Numeric，但不再忽略显式 Role；编辑与占位符共享有效角色。
- 高密度设置页的“拾取/取消/确定”使用 12 DIP，与本页输入一致；确定按钮保留半粗。
- Gallery Overview 新增完整层级、同层级表单、标准/紧凑按钮与输入对照。

## 全部控件实现检查

下表覆盖 src/controls 的全部 74 个实现文件，列出实现及所属公共头中的角色。
可配置控件最终由实例角色决定；表中多角色表示标题/正文/辅助/状态等不同用途，
不是要求混用。组合控件继承子控件规则；无正文的图形/容器保持不变。
已核对按钮、输入、选择、导航、集合、反馈和图表类别；未把控件高度、图标尺寸或
文本矩形尺寸误改成字号。后续添加控件时应同步本表和 Gallery。

| 控件实现 | 使用/默认角色 |
| --- | --- |
| `auto_suggest_box` | 继承/实例角色或无独立正文（图标尺寸不计入正文字号） |
| `avatar` | Caption |
| `badge` | Caption |
| `breadcrumb` | Body |
| `busy_overlay` | Body |
| `button` | Body, BodyStrong, Caption, CaptionStrong |
| `calendar_view` | Body, BodyStrong, Caption |
| `carousel` | 继承/实例角色或无独立正文（图标尺寸不计入正文字号） |
| `chart` | Caption, CaptionStrong, Title |
| `checkbox` | Body |
| `chip` | Caption |
| `color_picker` | 继承/实例角色或无独立正文（图标尺寸不计入正文字号） |
| `combo_box` | Body, CaptionStrong |
| `command_bar` | Body, BodyStrong |
| `date_picker` | Body |
| `dialog` | Body, Caption, Title |
| `drawer` | 继承/实例角色或无独立正文（图标尺寸不计入正文字号） |
| `drop_down_button` | 继承/实例角色或无独立正文（图标尺寸不计入正文字号） |
| `empty_state` | BodyStrong, Caption |
| `expander` | BodyStrong |
| `file_drop_zone` | BodyStrong, Caption |
| `flyout` | 继承/实例角色或无独立正文（图标尺寸不计入正文字号） |
| `form_field` | Body, Caption |
| `gauge` | Caption, Title |
| `grid_view` | Caption |
| `group_box` | CaptionStrong |
| `hotkey_box` | Body |
| `hyperlink_button` | Body |
| `icon_view` | 继承/实例角色或无独立正文（图标尺寸不计入正文字号） |
| `image_view` | Caption, CaptionStrong |
| `info_badge` | Caption |
| `info_bar` | BodyStrong, Caption |
| `label` | Body, Display, Overline, Subtitle, Title |
| `list_view` | Body, CaptionStrong |
| `log_view` | Mono |
| `menu_bar` | Body |
| `navigation_view` | Body, BodyStrong, Overline |
| `number_box` | Caption, Numeric |
| `page_host` | 继承/实例角色或无独立正文（图标尺寸不计入正文字号） |
| `pagination` | Body |
| `password_box` | 继承/实例角色或无独立正文（图标尺寸不计入正文字号） |
| `progress_bar` | 继承/实例角色或无独立正文（图标尺寸不计入正文字号） |
| `progress_ring` | 继承/实例角色或无独立正文（图标尺寸不计入正文字号） |
| `radio_button` | Body |
| `range_slider` | 继承/实例角色或无独立正文（图标尺寸不计入正文字号） |
| `rating` | 继承/实例角色或无独立正文（图标尺寸不计入正文字号） |
| `repeat_button` | 继承/实例角色或无独立正文（图标尺寸不计入正文字号） |
| `rich_label` | Body, BodyStrong |
| `scroll_viewer` | 继承/实例角色或无独立正文（图标尺寸不计入正文字号） |
| `segmented` | Caption |
| `separator` | 继承/实例角色或无独立正文（图标尺寸不计入正文字号） |
| `settings_card` | BodyStrong, Caption |
| `skeleton` | 继承/实例角色或无独立正文（图标尺寸不计入正文字号） |
| `slider` | 继承/实例角色或无独立正文（图标尺寸不计入正文字号） |
| `sparkline` | 继承/实例角色或无独立正文（图标尺寸不计入正文字号） |
| `split_button` | Body |
| `split_view` | 继承/实例角色或无独立正文（图标尺寸不计入正文字号） |
| `splitter` | 继承/实例角色或无独立正文（图标尺寸不计入正文字号） |
| `status_bar` | Caption |
| `stepper` | Caption |
| `swatch` | 继承/实例角色或无独立正文（图标尺寸不计入正文字号） |
| `switch` | Body |
| `tab_control` | Body, BodyStrong |
| `table` | Caption, CaptionStrong, Overline |
| `teaching_tip` | BodyStrong, Caption |
| `text_box` | Body, Caption |
| `time_picker` | Body, BodyStrong, CaptionStrong |
| `title_bar` | CaptionStrong, Mono |
| `toggle_button` | 继承/实例角色或无独立正文（图标尺寸不计入正文字号） |
| `token_box` | Body |
| `tool_tip` | 继承/实例角色或无独立正文（图标尺寸不计入正文字号） |
| `tree_table` | Body, Caption, CaptionStrong |
| `tree_view` | Body |
| `view_box` | 继承/实例角色或无独立正文（图标尺寸不计入正文字号） |

## 验证与限制

build.bat 后运行 visual、perf、anim、api 四项程序。API 回归覆盖按钮移动、
按钮档位与显式 Role、输入占位符继承、数字输入角色覆盖；视觉回归仍覆盖原控件状态。
Gallery 用于人工对照 100%/125%/150%/200% DPI 的文本、焦点、菜单与窗口缩放。
自动化通过不代替 AutoCAD 宿主中的真实交互检查。
