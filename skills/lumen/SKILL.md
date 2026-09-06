---
name: lumen
description: >
  Build Windows C++20 interfaces with LUMEN, integrate the library, or add and
  modify its controls and core implementation. Use for LUMEN development or
  explicit /lumen requests; generic glow or gallery tasks alone do not apply.
---

# LUMEN

遵循当前工程的 AGENTS.md 与用户明确授权；本技能提供 LUMEN 技术约束，不扩大任务范围。用户明确指令优先于技能工作流建议；与业务约束冲突且意图不清时说明冲突，不能把实现约束误作审批要求。

公共 API 在库的 `include/lumen/`。使用或修改接口前读取对应头文件。按任务加载以下内容，可跨用法和实现两类，不要求通读所有 references：

| 任务 | 读取内容 |
| --- | --- |
| 用库写应用、接入工程 | [use.md](references/use.md) + [constraints.md](references/constraints.md) 的设计语言与布局章节 |
| 新增/修改控件、布局或核心实现 | [extend.md](references/extend.md) + [constraints.md](references/constraints.md) 的相关章节 |
| 宿主插件嵌入、字体或窗口生命周期 | 应用侧读 use.md 的宿主章节；库侧读 constraints.md 的宿主章节 |
| 文档维护 | 仅相关章节，检查链接、API 事实和分发副本，不触发代码验证 |

验证：应用改动按 use.md，库代码改动按 extend.md。不要仅因提到 LUMEN 就运行整套测试或更改模型、工具及全局配置。
