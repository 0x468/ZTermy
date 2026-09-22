# V 系列状态与未收尾事项

更新时间：2026-09-22。本文是 V 系列当前状态和后续工作的唯一入口。
各版本 Scope、Acceptance、Program 与 Review 文档保留当时的需求、证据和决策，
其中未勾选项目不自动代表当前产品仍未实现；出现冲突时以本文、`CHANGELOG.md`
和当前源码为准。

## 已完成的系列

| 系列 | 对应版本 | 当前状态 | 历史入口 |
|---|---|---|---|
| V1 | 0.1.x | 已验收并归档 | [V1_SCOPE.md](V1_SCOPE.md)、[V1_ACCEPTANCE.md](V1_ACCEPTANCE.md) |
| V2 | 0.2.0–0.2.14 | 基线与后续里程碑已完成，历史验收保留 | [V2_SCOPE.md](V2_SCOPE.md)、[PRE_V3_PROGRAM.md](PRE_V3_PROGRAM.md) |
| V3 | 0.3.0–0.3.11 | 自有 Provider 助手功能线完成；外部 Agent 永久排除 | [V3_AI_PROGRAM.md](V3_AI_PROGRAM.md)、[AI_ARCHITECTURE.md](AI_ARCHITECTURE.md) |
| V4 | 0.4.1–0.4.3 | 终端选择、输入协议、链接与键盘工作流已交付 | [V4_TERMINAL_INTERACTION_PROGRAM.md](V4_TERMINAL_INTERACTION_PROGRAM.md) |
| V5 | 0.5.0–0.5.1 | 主题、窗口、Tab/Pane/Session 与会话生命周期已交付；活动专项关闭 | [V5_EXPERIENCE_CONVERGENCE.md](V5_EXPERIENCE_CONVERGENCE.md)、[V5_TAB_PANE_SESSION_ACCEPTANCE.md](testing/V5_TAB_PANE_SESSION_ACCEPTANCE.md) |

## 当前必须处理

当前没有阻塞主线的已知 P0/P1。独立窗口回附后的 `0xC0000409` 已确认来自运行时
检查在 QML 销毁窗口后继续读取悬空引用；销毁安全修复后动态/静态完整
`--terminal-render-smoke` 均退出 0。内存第三阶段也已覆盖 1/2/4/8 Pane、8 Tab、
重复关闭/重开和 20,000 行输出；对象回到基线，60 秒后提交内存回落，未观察到
重复增长斜率。完整数据见 [内存调查](memory-investigation-2026-09-21.md)。

2026-09-22 Owner 确认真机 SSH/SFTP、IME、物理拖放、Snap Layouts、DPI/材质、
高速滚动及长期日常工作流已经持续使用且未发现问题；不再把这些重复列为待验收项。
MSI/portable/ICE 仍按每次正式发布的打包门禁执行，不作为日常开发遗留事项。

## 暂缓，等待外部证据

- **真实 AI Provider 内存场景**：合成三轮 Markdown 的布局重建失控已修复，
  动态 Release 内核峰值中位数由 1619.61 MiB 降至 481.86 MiB。朋友报告的真实
  三轮对话崩溃仍缺少 Provider、内容形态、日志和 dump；Owner 已决定暂缓，
  取得原始证据后再恢复，不据此提前增加缓存清理或截断策略。

## 已批准但尚未扩展的设计边界

- 独立窗口目前承载一个工作区布局树；独立窗口内部多 Tab、任意原生窗口之间
  直接投放到具体 Pane，以及独立窗口几何/最大化状态持久化仍未实现。
- Profile 专属 Tab 图标仍是独立设计项；通用终端图标继续作为当前回退。

## 文档维护规则

- 本文只记录当前状态和仍需行动的项目；完成项从这里移除并写入 `CHANGELOG.md`。
- Program/Scope 保存版本意图，Acceptance 保存当时证据，ADR 保存长期技术决策；
  不再为每次小修复新增一份顶层状态文档。
- 生成的截图、CSV、日志、冻结二进制和证据 ZIP 放在 `build/`，不进入版本控制。
- 新专题优先更新现有 Program/Acceptance；只有形成长期约束时才新增 ADR。
