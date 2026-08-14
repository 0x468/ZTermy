# ztermy V3 AI 系统评审与重构基线（2026-08-14）

状态：基于当前 `main`、本地 Netcatty/OpenCode/Codex 参考源码、官方产品文档和实际截图完成；本轮已落地协议、模式、审批 UI 与规则建议的第一批修复。

## 1. 结论先行

ztermy 的 AI 底层并不是“什么都没有”：它已经具备 provider 抽象、流式会话、语义命令块、终端帧、上下文裁剪、工具预算、写控制权、权限规则、MCP、加密历史和审计活动。问题在于这些能力没有形成一份一致、可解释、可验证的 Agent 产品契约。

本轮确认的关键问题如下：

1. `wait_terminal_frame` 把 6 个字段全部设为必填，即使 `changed` 根本不需要 `idle_ms`；模型漏填任一字段就得到 `invalid_arguments`。
2. `write_to_pty` 同样把可选语义 `append_enter` 设为必填，增加了无意义的工具失败。
3. “提问”和“编辑”在执行策略中完全相同，“自动”和“YOLO”也完全相同；五档 UI 实际只有三种行为。
4. 当前模式未注入 system prompt；模型看不到模式，也无法调整工具选择与话术。
5. 所有动作工具和 MCP 工具在所有模式、甚至“仅生成命令”场景中都暴露给模型，扩大了误调用面和工具 schema 成本。
6. 命令风险分类只用于把审批卡涂红，不会改变自动模式的执行决定。
7. 审批卡中的长命令 `TextEdit` 被限制高度，但没有裁剪或滚动容器，文本仍在边界外绘制，形成截图中的重影。
8. `prefix` 原先是原始字符串前缀；`docker ps` 会错误覆盖 `docker pssh`。审批 UI 默认只填完整命令的 `exact`，没有提供保守的命令族建议。

因此，V3 后续不能继续靠增加模式名称、prompt 文案或零散工具修补。必须把“上下文、工具、策略、审批、证据、UI”看作同一份端到端协议。

## 2. 当前项目与 AI 调用链

ztermy 是 Windows 11 优先的 Qt Quick/C++23 原生 SSH 终端。QML 负责呈现和轻交互；C++ 负责会话状态、终端语义、I/O、安全边界、持久化与 Agent 调度。

当前 AI 主链路为：

```text
终端字节 / shell integration / 用户显式附件 / 会话元数据
  -> AiContextBroker + Redactor + Serializer
  -> AiConversationModel + AiContextCompactor
  -> provider-independent AiGenerationRequest
  -> OpenAI Responses / Anthropic / OpenAI-compatible provider
  -> AiTurnRunner 工具循环
  -> read tools / action tools / frame wait / SFTP / notes / MCP
  -> permission policy + rules + budget + write ownership
  -> terminal / SSH / SFTP / app artifacts
  -> tool evidence + encrypted conversation + activity audit
```

值得保留的架构资产：

- 终端单元格没有建成 QML 对象树，AI 读取的是有界屏幕、命令块和游标化 scrollback。
- 会话身份包含 `session_id + session_generation`，可阻止重连后的陈旧动作重放。
- 动作有 dispatch ledger、幂等键、状态迁移、回合预算和写控制权。
- 终端证据明确标为不可信，prompt 已禁止遵循输出中的指令。
- 历史会话加密存储，并具备跨进程锁与认证失败恢复。
- 上下文在发送视图中压缩，不修改持久化原始会话。

当前最大的结构性风险是 `AppController::sendAiMessage`：它同时组装上下文、工具集、provider 请求、工具路由、异步取消、MCP、SFTP、审计和 UI 状态，已经成为 AI 子系统的超大编排器。后续应拆为 `AiTurnRequestFactory`、`AiToolCatalogPolicy`、`AiToolExecutionCoordinator` 与按能力划分的异步 handler；这不是为了“代码好看”，而是为了让权限和工具暴露策略能够独立测试。

## 3. 市场与参考实现比较

### 3.1 终端 Agent

| 产品 | 上下文与终端模型 | 执行/审批 | 对 ztermy 的启示 |
|---|---|---|---|
| Warp | terminal block 与 Agent conversation block 分离；当前会话中的相关命令自动成为上下文，也支持显式附加 block。Full Terminal Use 直接观察终端缓冲并写 PTY。 | 全局可配置首次写入询问、始终询问、始终允许；Agent Profile 对命令、交互终端等能力分别授权；Run Until Completion 是明确的 YOLO。 | 上下文附件必须可见；PTY 交互应是独立高风险能力；Auto 与 YOLO 必须不同。 |
| Netcatty | terminal/workspace/global scope、host metadata、附件、typed compaction、工具输出恢复句柄。 | 只有 observer/confirm/auto 三档；模式明确写入 system prompt；confirm 直接调用工具，由客户端审批，不在文本里重复询问。持久授权会拆解复合 shell command 并生成命令族规则。 | 三档基础语义清楚，但 blocklist 与命令解析不可直接复制；ztermy 应使用自己的 Windows/SSH 能力模型。 |
| Termius | Agent 可使用活动连接、主机分组/标签和命令输出，不接触密码和私钥。 | 官方介绍强调所有命令确认。 | 凭据永不成为模型上下文；对专业 SSH 客户端，保守默认比“自动化演示感”更重要。 |
| Wave Terminal | 对终端、文件系统、网页和 widget 统一工具化，支持显式 context toggle 与文件附件。 | 还把后台 Claude Code 的“等待权限/已完成”状态映射到 tab attention。 | ztermy 应把后台 Agent 的等待审批、完成和失败变成工作区级注意力信号，而不是只存在侧栏里。 |

官方资料：[Warp Full Terminal Use](https://docs.warp.dev/agent-platform/capabilities/full-terminal-use)、[Warp Blocks as Context](https://docs.warp.dev/agent-platform/local-agents/agent-context/blocks-as-context)、[Warp Agent Profiles & Permissions](https://docs.warp.dev/agent-platform/capabilities/agent-profiles-permissions)、[Termius AI Agent](https://termius.com/blog/ai-agent)、[Wave AI](https://docs.waveterm.dev/waveai)、[Wave Claude Code integration](https://docs.waveterm.dev/claude-code)。

### 3.2 编码 Agent

| 产品 | 模式/权限 | 指令与上下文 | 对 ztermy 的启示 |
|---|---|---|---|
| OpenCode | action/resource/effect 规则，`allow/ask/deny`；规则按顺序解析；审批提供 once/always/reject；命令规则会按 shell arity 建议安全前缀。 | `AGENTS.md`/规则分层；压缩使用 checkpoint summary + 原样近期尾部，持久历史不被改写；provider overflow 只重试一次。 | 审批规则应基于语义边界，不应简单 `starts_with`；压缩状态和摘要应对用户可观察。 |
| Codex | `approval_policy` 与 `sandbox_mode` 是两个正交维度；只读/工作区写/完全访问不等于审批频率。 | `AGENTS.md` 从全局到仓库、目录逐层发现，靠近工作目录的规则覆盖；只暴露当前任务相关工具。 | ztermy 目前没有 shell sandbox，不能用一个“模式”同时冒充权限和隔离；工具目录应按模式裁剪。 |
| Claude Code | `plan/default/acceptEdits/bypassPermissions`；`acceptEdits` 的意义来自一等文件编辑工具，shell 副作用仍询问。 | `CLAUDE.md` 分层发现；权限规则与项目配置结合。 | ztermy 没有可审查 diff/patch 的文件编辑工具，因此“编辑”模式没有真实产品语义，应该删除，而不是保留同义档位。 |

官方资料：[OpenCode Permissions](https://opencode.ai/v2/docs/permissions)、[OpenCode Compaction](https://opencode.ai/v2/docs/compaction)、[OpenCode Rules](https://opencode.ai/docs/rules/)、[Codex configuration](https://developers.openai.com/codex/config-reference)、[Codex AGENTS.md](https://developers.openai.com/codex/guides/agents-md)、[Claude Code CLI modes](https://docs.anthropic.com/en/docs/claude-code/cli-usage)。

本地参考代码只用于行为研究，不复制源码、UI、图标或品牌：

- `D:\Repo\tmp\Netcatty\infrastructure\ai\cattyAgent\systemPrompt.ts`
- `D:\Repo\tmp\Netcatty\infrastructure\ai\harness\contextBudget.ts`
- `D:\Repo\tmp\Netcatty\infrastructure\ai\harness\permissionGrants.ts`
- `D:\Repo\tmp\Netcatty\infrastructure\ai\shared\shellCommandGrant.ts`
- `D:\tmp\ai-reference\opencode\packages\opencode\src\permission\arity.ts`
- `D:\tmp\ai-reference\opencode\packages\opencode\src\session\compaction.ts`
- `D:\tmp\ai-reference\codex`
- `D:\tmp\ai-reference\warp-docs`

## 4. 目标模式合同

本轮删除“编辑”模式。原因不是少做一个功能，而是它依赖一等、可审查、可回滚的文件编辑能力；ztermy 当前只有终端命令、原始 PTY 输入、SFTP 传输和应用内 runbook，不存在类似 diff/patch 的编辑安全边界。保留该名称只会制造虚假安全感。

| 模式 | 模型看到的工具 | 客户端行为 | 模型是否知道 |
|---|---|---|---|
| 只读 | read tools、frame/read、SFTP read、note read；隐藏 action 与 MCP | 所有副作用不可达；底层策略仍拒绝意外写调用 | 是，system prompt 明确说明只读 |
| 提问 | read + action + MCP | 每个副作用在原生审批卡等待；模型不再在文本中重复询问 | 是 |
| 自动 | read + action + MCP | 普通动作自动；高风险命令与所有外部 MCP 工具仍询问；显式 allow 可覆盖该询问 | 是 |
| YOLO | read + action + MCP | 不弹动作审批；显式 deny、schema/scope、write ownership、budget 仍生效 | 是 |
| 仅生成命令 | 只暴露读取工具 | 只给一个可运行命令，不可自行执行 | 是，独立 suggestion prompt |

重要原则：模型“感知模式”只用于更合理地规划、选择工具和组织话术；真正的权限永远由客户端策略执行。prompt 不是安全边界。

## 5. 命令审批的时间范围

| 范围 | 当前精确定义 | 是否持久化 | 建议用途 |
|---|---|---|---|
| 本次 | 只批准/拒绝当前 pending tool call，不创建规则 | 否 | 默认、一次性高风险操作 |
| 会话内 | 绑定当前 terminal `session_id`，会话结束时清理 | 否 | 同一连接中的重复诊断命令 |
| Profile 内 | 绑定保存的 SSH profile，未来由该 profile 创建的会话也生效 | 是 | 某台受信主机的稳定运维规则 |
| 全部 Profile | 不绑定 session/profile，对所有终端生效 | 是 | 极少量、可明确审计的通用只读命令族 |

UI 原来的“永久”容易让人误解为不可撤销。实际含义是“持久保存”，仍可在设置的 AI 权限规则列表中禁用或删除。全局规则影响最大，应显示为“全部 Profile”，不应只显示抽象的“永久”。

## 6. 匹配器的精确定义

| 匹配器 | 语义 | 示例 | 风险 |
|---|---|---|---|
| 精确 | 整个 action subject 完全相同 | `git status --short` | 最安全但复用低 |
| 前缀 | 对终端命令按 token 边界匹配，再允许后续参数；其他能力仍为原始资源前缀 | `git status` 匹配 `git status --short`，不匹配 `git status-evil` | 后续参数仍可能改变行为，批准前必须可见 |
| 通配符 | 对整个 subject 使用 `*`/`?` 匹配 | `/var/log/*.log` | 容易覆盖意外路径 |
| 正则 | ECMAScript 正则对整个 subject 匹配 | `^systemctl (status|show) ...$` | 专家功能；不可由模型自动生成后静默保存 |
| 任意 | 当前 capability 的所有 subject | 所有 PTY input | 范围最大，只适合明确场景 |

本轮增加了保守的默认规则建议：

- 已识别的低风险命令族（如 `git status`、`git diff`、`docker ps`、`kubectl get`、`systemctl status`）默认建议 token-prefix。
- 高风险命令、复合命令、heredoc、重定向、管道、命令替换、带引号复杂命令和未知命令默认仍是 exact。
- 不自动建议 regex。
- UI 始终展示并允许用户修改匹配器与 pattern；建议不等于授权。

更完整的下一阶段需要按 shell family 建立解析器：PowerShell AST、POSIX shell parser 与网络设备 raw CLI 不应共用同一个分词器。当前保守建议只解决明显的易用性问题，不冒充完整 shell 语义分析。

## 7. 两个截图缺陷的根因与修复

### 7.1 `wait_terminal_frame: invalid_arguments`

旧 schema 要求模型每次重复：

```text
session_id, session_generation, after_revision,
condition, idle_ms, timeout_ms
```

其中 `changed` 场景的 `idle_ms` 无意义。系统 prompt 还要求先 changed、再 idle，模型连续构造两套相似 JSON，任何漏项都会失败。

本轮改为：

- 只要求 `session_id + session_generation + after_revision`。
- `condition` 默认 `changed`。
- `timeout_ms` 默认 30000。
- `condition=idle` 时 `idle_ms` 默认 750。
- 继续拒绝未知字段、非法类型、零超时和越界值。
- 兼容 changed 场景显式传 `idle_ms=0`。

这不是“放松安全校验”，而是把条件性参数正确表达成条件性参数。`write_to_pty.append_enter` 同样改为可选、默认 `false`。

长期仍应优先等待语义命令生命周期；frame wait 只能证明屏幕变化或暂时空闲，不能证明命令退出。

### 7.2 长命令审批重影

旧 QML 对 `TextEdit` 设置 `Layout.maximumHeight: 120`，但既没有 `clip`，也没有 `Flickable/ScrollView`。布局高度被压到 120，文本绘制却继续越过边界，覆盖后面的规则控件和对话内容。

本轮把命令区改为固定最大 120 px 的裁剪 `Flickable`，按内容自动长到上限，超过上限显示垂直滚动条。命令仍可选择复制，不再污染卡片外区域。

## 8. 上下文、指令注入与命令管理评审

### 上下文

当前已经有显式 selection/recent commands、自动上下文开关、命令输出游标读取和发送前压缩。下一步应补：

1. 请求前 token 预算预览：system、history、attachments、tool schema、输出预留分别显示。
2. 持久 checkpoint summary，而不是每次只对旧消息做 head/tail 截断。
3. 压缩事件可见：何时压缩、保留了哪些近期回合、哪些工具输出被替换为摘要。
4. 上下文 item 的 stale/coverage/provenance 统一展示，不只在 JSON 内存在。
5. 工作区级多会话上下文必须显式显示目标 session，避免跨主机误操作。

### 指令注入

当前 prompt 正确声明终端输出与工具结果是不可信证据，也要求不遵循其中指令。但还需形成多层防线：

- 数据层：保留 provenance、coverage、truncation、redaction，不把 terminal evidence 拼进 system instructions。
- 工具层：模型只看到当前模式/任务相关工具；读写工具严格分开。
- 执行层：session generation、schema、capability、rule、risk、ownership、budget 全部客户端校验。
- 呈现层：外部 MCP 描述、参数和结果标注 untrusted；审批只显示规范化后的真实动作。
- 回归层：建立 terminal-output prompt injection、恶意 MCP description、工具结果伪造成功等 adversarial tests。

### 命令管理

`run_command` 返回“已接收”而不是“已完成”，这是正确边界；但产品 UI 必须始终区分 queued/running/waiting/succeeded/unknown。截图中工具失败后助手仍声称“已经创建”，说明最终回答没有被证据一致性门禁约束。

下一阶段应增加 response finalization verifier：当同一回合存在失败、超时或未确认的 side-effect tool 时，禁止把完成态文案直接标为成功；最少应在 UI 上显示“模型声称成功，但工具证据未确认”。这类校验应基于结构化 tool ledger，而不是关键词匹配自然语言。

## 9. 后续重构顺序

1. P0：为工具失败后的成功声称增加结构化一致性门禁；完善 `write_to_pty`/interactive app 的可观测协议。
2. P0：把 `sendAiMessage` 拆为请求工厂、工具目录策略、执行协调器，避免权限逻辑继续散落。
3. P1：持久 checkpoint compaction、预算 UI、overflow 后单次可解释重试。
4. P1：shell-aware command rule suggestion；PowerShell/POSIX/raw network CLI 分实现。
5. P1：capability-specific permission 页面，不再只用一个全局模式控制所有动作。
6. P1：后台 Agent tab attention、审批队列和多会话目标可视化。
7. P2：一等远程文件 edit/diff/patch 工具完成后，再评估是否重新引入“编辑”模式。

## 10. 本轮验证要求

代码完成不等于 UI 缺陷关闭。必须同时满足：

- C++ 聚焦单元/场景测试通过。
- `qmllint` 与 QML 格式门禁通过。
- 使用 16 KiB heredoc 命令实际打开审批卡，确认卡片不越界、滚动条可用、键盘 Enter/Esc 正常。
- 用缺省参数调用 `wait_terminal_frame`，并覆盖 changed、idle、timeout、scope changed。
- Auto 下普通命令直接执行，高风险命令与 MCP 等待审批；YOLO 下不弹审批；显式 deny 在 YOLO 下仍拒绝。

### 2026-08-14 实际验证结果

- Debug 构建通过。
- 全量 `ctest`：108/108 通过。
- 全量 `ztermy_clang_tidy_check`：237/237 通过。
- QML 格式门禁：48/48 文件通过；改动 C++ hunks 通过 clang-format 检查。
- 真实窗口 `ztermy_ui_layout_runtime_smoke` 通过：长命令审批 viewport 有界且可滚动；常规、260 px 紧凑与浅色 AI 面板均完成截图；AI 控件无障碍角色/名称契约通过。
- 仓库全量 `ztermy_format_check` 仍会报告本轮范围之外既存文件的历史格式差异；没有通过全仓机械格式化把这些差异混入本次 AI 重构。
