# V 系列状态与未收尾事项

更新时间：2026-09-21。本文是 V 系列当前状态和后续工作的唯一入口。
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

1. **独立窗口回附后的运行时异常**：当前动态修复版与冻结原布局都能在
   `--terminal-render-smoke` 的 `Native detached-window drag merges into a tab`
   之后以 `0xC0000409` 退出。终端输出、滚动和缩放阶段在此前已经通过；缺少
   dump 与调用栈，尚未确认是窗口销毁顺序、回附后的悬空引用还是测试夹具问题。
2. **真实 AI Provider 内存场景**：合成三轮 Markdown 的布局重建失控已修复，
   动态 Release 内核峰值中位数由 1619.61 MiB 降至 481.86 MiB；朋友报告的
   真实三轮对话崩溃仍缺少 Provider、内容形态、日志和 dump，不能宣称完全覆盖。
3. **内存组成与长时斜率**：动态/静态空载 Private Bytes 约 233/234 MiB，当前
   没有持续泄漏证据。仍需测 2/4/8 Pane、反复打开/关闭 Tab 与 AI 侧栏、真实
   Provider/附件、scrollback 上限和隐藏页面保留，确认每项增量及回收边界。

## 需要真实环境或 Owner 验收

- 真实 SSH/SFTP 的连接、重连、传输取消和身份切换；自动化夹具不能替代远端环境。
- IME preedit、物理鼠标拖放、Windows 11 Snap Layouts、跨显示器 DPI/缩放与
  Acrylic/WCA Glass 失焦效果。
- MSI/portable 发布矩阵及受本机 Windows Installer 环境影响的 ICE 检查。
- 高速终端滚动、复杂 shell 全屏程序和长时工作流的主观延迟验收。

## 已批准但尚未扩展的设计边界

- 独立窗口目前承载一个工作区布局树；独立窗口内部多 Tab、任意原生窗口之间
  直接投放到具体 Pane，以及独立窗口几何/最大化状态持久化仍未实现。
- Profile 专属 Tab 图标仍是独立设计项；通用终端图标继续作为当前回退。
- 0.5.1 月相与潮汐视觉正在重新选型；先通过 Web 候选确认，再修改 QML。

## 文档维护规则

- 本文只记录当前状态和仍需行动的项目；完成项从这里移除并写入 `CHANGELOG.md`。
- Program/Scope 保存版本意图，Acceptance 保存当时证据，ADR 保存长期技术决策；
  不再为每次小修复新增一份顶层状态文档。
- 生成的截图、CSV、日志、冻结二进制和证据 ZIP 放在 `build/`，不进入版本控制。
- 新专题优先更新现有 Program/Acceptance；只有形成长期约束时才新增 ADR。
