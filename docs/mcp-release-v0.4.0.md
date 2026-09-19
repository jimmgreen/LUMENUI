# v0.4.0 发布准备与验证

日期：2026-09-19。用户明确授权提交全部项目改动到 main，推送并发布正式版 v0.4.0。

## 发布范围

- Table 过滤接口、PagedModel、结构化 LogView、实例文字规格及相关 Gallery/独立示例。
- 已有控件、输入、IME、生命周期、安全修复和回归；任务排序补齐及项目清理。
- 保留正式文档截图，排除 .c2c.json、本机日志、缓存及临时产物。发布前将两份审查文档中的个人目录路径泛化。
- CMake 版本与 CHANGELOG 统一为 0.4.0。旧 v0.3.0 标签与已发布附件不改写。

## 已有验证及边界

2026-09-19 当前功能源码已在独立目录完成 Release 全量构建，编译警告 0；表格 33 条 PASS，API、动画、视觉、性能通过。四张 Gallery 离屏截图已检查。用户随后要求清理原始日志和指纹清单，故历史结果只保留文档记录。

真实 DPI、完整输入/弹层端到端与业务宿主验收仍未完成；性能历史尖峰未归因。此次版本准备不修改核心功能实现，也不把发布构建成功当成上述实机验收通过。

## 版本准备复验

- bundled LumaText 的 SHA256SUMS 逐项核对通过。
- CMake 更新为 0.4.0 后，指定同一独立构建目录重新配置/生成并增量构建成功，无须重编译；表格测试再次得到 33 条 PASS。
- CHANGELOG 的 v0.4.0 说明提取成功，差异空白检查通过。新日志只写系统临时目录，不重新污染项目目录。

## 发布机制

推送 v0.4.0 标签触发现有 Release 工作流：校验 bundled LumaText、Windows x64 编译、打包 Gallery 与 SDK、扫描压缩包内个人目录及常见凭据标记、从 CHANGELOG 提取说明后发布。该工作流关闭测试目标，测试结论来自本地验证而非发布 CI。

不上传本机静态库，避免将本机编译路径带入公开附件。提交及标签使用 GitHub noreply 身份，不重写历史。模式扫描不能保证穷尽所有敏感信息。

本文件为发布前记录，线上结果以 [Release 工作流](https://github.com/jimmgreen/LUMENUI/actions/workflows/release.yml) 和 [v0.4.0 Release](https://github.com/jimmgreen/LUMENUI/releases/tag/v0.4.0) 为准。
