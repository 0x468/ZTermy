# AI 产品设计研究与 ztermy 重构决策（上下文 / 命令 / 指令注入 / 模式）

Status: research baseline for the ztermy V3 AI refactor; versioned with git.

> 本文中的“当前实现”是重构前/中期快照。2026-08-14 的实际实现、模式合同、
> 审批规则与差距清单以 `V3_AI_SYSTEM_REVIEW_2026-08-14.md` 为准。

> 节点信息：本文档随重构一起纳入版本管理。重构的每个阶段在
> `docs/reviews/ai-refactor-progress.md` 中追加节点（日期 + git commit + 范围 + 验证结果）。

## 1. 研究范围与方法

参考对象（按与 ztermy 的相似度排序）：

| 产品 | 类型 | 参考来源 | 关键价值 |
| --- | --- | --- | --- |
| Netcatty | Electron SSH 终端 + AI | 本地源码 `D:\Repo\tmp\Netcatty`（GPL-3.0，仅研究不复制） | 终端内命令执行、工具卡、审批流、Quick Messages |
| Warp | 闭源 AI 终端 | 官方文档（blocks-as-context、agent context、modality） | block 附加上下文、键盘流、自动上下文 |
| opencode | 开源 AI 终端/编码 Agent（TypeScript） | 克隆于 `D:\tmp\ai-reference\opencode`（commit 6c035e1, 2026-08-13） | system prompt 工程、compaction、工具自然语言描述、事件源式会话 |
| OpenAI Codex CLI | 开源终端编码 Agent（Rust） | 克隆于 `D:\tmp\ai-reference\codex` | approval policy、命令规范化、compaction 生命周期、world-state 差分注入 |
| Claude Code | 闭源编码 Agent | 官方文档 + 社区分析 | 规则/技能/上下文窗口管理理念 |

opencode 与 codex 克隆节点：`D:\tmp\ai-reference\opencode`、`D:\tmp\ai-reference\codex`（clone 日期 2026-08，depth 1）。

专项研究报告：
- Netcatty 内部机制（A-F 六部分 + 16 项差距清单）——本会话研究记录；
- opencode V1/V2 双架构（主循环、compaction、权限、工具、AGENTS.md 注入、事件持久化）——本会话研究记录；
- Codex CLI 架构（system prompt 构成、world-state、approval 四档、JSONL 转录）——`docs/research/CODEX_CLI_ARCHITECTURE.md`；
- Warp blocks-as-context 官方文档抓取——`D:\tmp\ai-reference\warp-docs\`。

## 2. 上下文管理（Context Management）

### 2.1 各产品的做法

**Warp（blocks-as-context）**
- 终端输出被组织为"block"（命令 + 其输出 + 退出码），block 是上下文的基本单位；
- 附加方式：block 列表上点 AI sparkle → Attach as context；键盘流 `CMD-UP` 附加上一个 block、`CMD-UP/DOWN` 选择、`CMD-DOWN` 清除；鼠标 `CMD+click` 多选；
- 区分 **pending（待发送）与 attached（已附加）** 两种上下文状态，用户可预览待发送上下文；
- Agent 会话内**自动上下文**：Agent 正在处理的 block 自动进入会话上下文（用户无需手动附加）；
- 结论：上下文是"显式 + 会话内自动"的混合，且**键盘优先**。

**Netcatty**
- system prompt 注入：作用域描述 + host 清单（hostname/label/protocol/os/user/shell/hostChain/端口转发/连接状态）+ 权限规则 + web search 开关 + 用户技能；
- 终端内容**按需读取**：`terminal_read_context` 工具（viewport/tail/head/lines 范围，默认 80 行、上限 300 行，返回 hasMoreBefore/After）——不隐式抓取；
- 选区显式附加：终端选中文本 → 悬浮 Sparkles 按钮 → 附加为附件 chip；
- 会话记忆：user goal、decisions、blockers、每会话最后命令，**压缩后重新注入**；
- token 预算：估算 → 预压缩（LLM 摘要，保护最近 N 条，保留最新用户目标）→ 413 强制压缩重试。

**opencode**
- prompt 分层：system（角色+行为约束+工具使用策略）→ 会话信息（cwd/OS/shell/工作区）→ 规则（AGENTS.md/用户指令）→ 历史消息 → 工具结果；
- **compaction**（`src/session/compaction.ts`）：预算 = model context 输入上限 - 保留输出预算（默认取 maxOutputTokens 与 20k 的较小值）；触发：total tokens >= usable；策略：保留最近 turn 的 token 预算（`preserve_recent_tokens`，默认 `max(2k, min(15k, usable*0.25))`），逐 turn 从尾部倒推估算大小，超出预算的 turn 从头部开始折叠为 **LLM 生成的摘要**（保留用户目标与最近消息）；工具输出在压缩时截断到 **2000 字符**；压缩在独立消息（compaction part）中完成并可见；
- **overflow**（`src/session/overflow.ts`）：usable = input_limit - reserved；`isOverflow` 在每次响应后检查 tokens；
- 工具描述是**自然语言文本**（`src/tool/*.txt`），动态插值 OS/shell/cwd/临时目录，包含"该工具用于什么、不用于什么、何时用专用工具"的策略说明；
- 有 `truncate.ts` 工具（截断超长工具输出）与 todo/task 工具来降低上下文消耗。

**Codex CLI**
- 自动上下文：cwd、git 状态、环境（`context/environment_context.rs`）、AGENTS.md（`agents_md.rs`）、shell 快照（`shell_snapshot.rs`）；
- **compaction** 生命周期化（`compact.rs`、`compact_token_budget.rs`）：自动（inline）/手动（token budget）两种触发，均走统一生命周期（pre/post compact hooks、`start_new_context_window`、ContextCompaction turn item 可见）；
- 有 token budget 显示（`token_budget_context.rs`）。

### 2.2 ztermy 现状与差距

现状：
- 显式附加：选区 / 最近 1/3/5 条语义命令块（OSC 133/633），上下文 chip + 预览 + 固定/移除——机制与 Warp/Netcatty 对齐；
- 自动上下文（默认关）：最近命令块 + 当前 frame，64 KiB / 1000 行 / 16k token 聚合上限；
- 工具按需读取：`read_terminal`（仅当前 frame）、`read_command_block`、`read_command_output`（游标式）——无 scrollback 分页读取；
- **无压缩**：长会话直接溢出（模型 context 窗口硬上限），无摘要、无 413 重试；
- **无会话记忆**（goal/decisions 注入）；无 per-model context window 感知。

差距（重构方向）：
1. 增加 scrollback 分页读取工具（`read_terminal_output`：tail/head/lines + has_more，对齐 Netcatty 80/300 行语义）；
2. 增加**上下文压缩**：模型 context 窗口感知（provider 目录扩展 context 上限字段）→ 达到 usable 阈值时触发 LLM 摘要压缩，保留最近用户消息与工具证据，压缩事件在 UI 可见；
3. 工具输出统一截断（2k 字符级）并带 `truncated` 标记；
4. 413/context 超限自动重试（压缩后重发）；
5. 会话记忆（goal/最近命令）注入 system prompt（低优先级）。

## 3. 命令管理（Command Management）

### 3.1 各产品的做法

**Netcatty**：命令**真实敲进 PTY**（`typedInput: true`）+ shell 专用包装器（posix/powershell/cmd/fish）+ 合成回显（用户肉眼可见命令被敲出）；busy-lock（每会话一次一条）+ 执行队列；后台长任务 jobStart/jobPoll/jobStop 轮询，256 KB 输出窗口、增量 offset、取消（Ctrl+C + signal 重试 + 30s 强制上限）。

**Warp**：block 模型，Agent 执行的命令形成 block，用户可在终端里看到；专用输入通道；写前检查 prompt 空闲。

**opencode**：`bash` 工具在独立子进程执行（非用户 PTY），输出流式捕获 + 截断（`shell.ts`）；超时与后台任务；apply_patch 做文件修改而非终端命令。

**Codex**：`shell` 工具子进程执行；**命令规范化**（`command_canonicalization.rs`：shell 语法降级、argv 解析）；**exec policy**（allow/deny 规则 + 危险命令分类 + 已知安全命令白名单）；sandbox 可选。

### 3.2 ztermy 现状与差距

现状：
- run_command 直接注入现有 PTY（`dispatchInput`），**无合成回显**——用户看不到 Agent 在终端里执行了什么；
- **无写前空闲检查**：用户半行输入时代理命令会拼接进用户命令行重建缓冲；interrupt 的 \x03 清空用户缓冲；
- "用户接管"机制（handoffToUser/resumeAgent）是死代码；
- 有写租约（单 Agent 写入）、命令跟踪（AiCommandTracker）、wait_command 生命周期等待——这部分领先；
- wait_command 结果可超 64 KiB 导致轮次悬挂（高危缺陷）。

差距（重构方向）：
1. **合成回显**：run_command 注入前写入一个可见的 agent 标记行（如 `ESC[90m[agent] $ <command>ESC[0m`），使命令执行在终端内可见；
2. **写前空闲检查**：inputHistoryBuffer 非空 → 返回 `user_input_pending`；接线 handoffToUser/resumeAgent：用户开始输入 → 代理暂停（user_has_control），用户输入完成（Enter）→ 自动恢复；
3. interrupt 的 \x03 不再清空用户输入缓冲（AI 写入路径与用户输入路径分离）；
4. wait_command 输出封顶（32 KiB + truncated 标记）并检查 completePendingTool 返回值。

## 4. 指令注入（System Prompt / Instructions）

### 4.1 各产品的做法

**opencode（default.txt）** 的分层结构（最有参考价值）：
1. 角色声明（一句）；
2. URL 策略（不臆造 URL）；
3. Tone and style：**简洁性要求 + 正反例**（"4 行以内"、"不要开场白/总结"、示例对）；
4. Proactiveness：主动与克制的平衡；
5. 代码规范遵循（不臆测库可用性、模仿既有风格、禁止注释除非要求）；
6. 任务流程（搜索工具 → 实现 → 验证 → lint）；
7. 工具使用策略（Task 工具降上下文、**并行调用**、批量 bash）；
8. 代码引用格式（file:line）；
9. 输出格式约束（少于 4 行等）。

**Codex**：系统提示词 + 动态上下文（cwd/git/AGENTS.md/shell 快照）+ 权限规则注入；`consequential_tool_message_templates.json` 为有后果的工具提供消息模板。

**Netcatty**：system prompt 注入作用域/主机/权限/技能；per-shell 说明（raw 模式、串口禁用分页）。

**Claude Code**：AGENTS.md 分层加载（项目/用户/本地）、CLAUDE.md 规则、skills、hooks——"规则从文件注入"。

### 4.2 ztermy 现状与差距

现状（`AppController::sendAiMessage` 内联构造）：
- 一段话：untrusted evidence 声明 + wait_command 用法 + commandRequest 时的代码块约束；
- 工具描述是 1-2 句 JSON description。

差距（重构方向）：
1. **分层 system prompt**（常量字符串资源，可测试）：
   - 角色与产品边界（不臆造、不泄露）；
   - 上下文边界声明（untrusted evidence、不完整证据不得声称完整）；
   - 行为约束（简洁、先读后写、用工具而非臆测、并行调用策略）；
   - 终端语义协议（wait_command/frame 等待策略）；
   - 输出格式（命令放在单一代码块、理由简短）；
   - 模式说明（当前权限模式 + 什么会被问/被拒）；
2. 工具描述升级为多行自然语言（含"何时用/何时不用/边界/返回格式"），从 JSON description 移入独立常量表；
3. system prompt 构造收敛到独立单元（`AiSystemPromptBuilder`），可单测。

## 5. 模式与权限（Modes & Permission）

### 5.1 各产品的做法

**Netcatty**：observer（只读）/ confirm（写操作弹审批）/ auto（自由执行，blocklist 除外）；审批卡 Once/Always（持久 grant）/Reject；全局命令 blocklist（正则，ReDoS 防护：拒绝嵌套量词）；Enter=批准、Esc=拒绝。

**opencode**：permission ruleset（`Permission.fromConfig`：`"*": "allow"` + 工具级 glob 规则 `allow/ask/deny`）；build/plan/general/explore 子代理各有独立 ruleset；环境文件（.env）读取默认 ask；external_directory 默认 ask + 白名单；doom_loop 默认 ask（防循环）。

**Codex**：approval_policy（untrusted/on-failure/never/always）+ sandbox 模式（read-only/workspace-write/danger-full-access）；exec policy 规则文件（allow/deny + 网络规则）；**危险命令分类**（`is_dangerous_command`）+ 已知安全命令（`is_known_safe_command`）+ banned prefix 建议列表。

### 5.2 ztermy 现状与差距

现状：
- 五档模式 read-only/ask/edit/auto/yolo + 规则（exact/prefix/glob/regex/all × once/session/profile/global）——机制比参考产品更细；
- 已知问题：edit 模式语义不一致（run_command ask 但 save_runbook/SFTP allow）；规则 regex ReDoS 面；prefix 匹配过宽；deny 无条件压过 allow。

差距（重构方向）：
1. 明确模式×工具类的判定矩阵（文档化 + 测试锁定）；
2. 规则引擎：regex 预编译缓存 + 长度/嵌套限制；prefix 匹配加词边界；capability 白名单；
3. 增加"防循环"保护（opencode doom_loop 模式：同结果重复执行 ask/停止）；
4. 命令风险分类升级（参考 codex 危险命令表），但保持 informative-only 定位。

## 6. 工具设计（Tool Design）

**Netcatty**：50+ 工具，`toolSpecs.json` + Zod schema；工具结果统一 `{ok, data|error}`；
**opencode**：~20 工具，自然语言描述（.txt 模板动态插值），工具按领域分组（文件/搜索/shell/任务/计划/网络）；`todo`、`task`（子代理）管理长任务；
**Codex**：核心 shell + apply_patch + web_search + MCP 工具；工具结果紧凑化。

ztermy 现状：23+ 工具（13 read + 6 action + wait/frame + MCP），JSON schema 参数，结果 `{ok, ...}` 风格统一——数量偏多、描述偏短；read 工具结果不脱敏不归一化（已知缺陷）。

重构方向：
1. 工具结果统一走"归一化 + 脱敏 + 截断"管线（所有工具，不只 read_script/read_note）；
2. 描述升级（见 §4）；
3. 评估工具数量是否需要收敛（当前模型对 20+ 工具的遵循质量下降）——保留但分组描述。

## 7. ztermy 重构决策汇总（P0 清单）

按"研究结论 × 已知缺陷"交叉得到，详见 `docs/reviews/ai-refactor-progress.md` 各节点：

1. 正确性：wait_command 封顶；completePendingTool 返回值检查；toolCallCompleted 限额；Retry-After；attempt 状态重置；MCP 进程/超时；存储锁与 .bak 回退；
2. 命令管理：合成回显；写前空闲检查 + 用户接管接线；AI 输入路径与用户输入分离；
3. 上下文管理：scrollback 分页读取；压缩（model context 感知）；工具输出截断；413 重试；
4. 指令注入：分层 system prompt；工具自然语言描述；
5. 权限：模式矩阵文档化；正则安全；防循环；
6. UX：审批键盘流（Enter/Esc）；工具卡信息密度；终端可见性。

## 8. 关键机制速查（来自专项研究报告，重构时直接引用）

### 8.1 Netcatty（详见本会话研究记录）

- token 估算：provider 启发式（openai chars/3.5、anthropic 3.2、gemini 3.8、兜底 4）；
- 预算：`threshold = window − maxOutput − buffer(≤15k) − 150`；窗口默认 128k；
- 压缩链：stale prune → typed 机械压缩（文本 8k / tool-result 4k、空行折叠、重复行去重）→ LLM 摘要（保护最近 10 条、切点不拆 tool-call/result 对、摘要上限 1600 tokens）→ keepRecent 兜底；压缩后 reinjection（权限模式/会话状态/目标/未决句柄）；
- 413：检测 → force 压缩（附件文本化）→ 保留已产出 tool results 重建消息 → 单次重试；
- 命令：marker 协议 `_S`/`_E:rc` + 四 shell 包装（子 shell eval 隔离、`trap ':' INT`、前导空格避历史）；合成回显 `\r\n` 归一；双层 busy-lock；job 轮询（单调 high-water offset、256KB 窗口、30s 间隔、10min 保留）；取消 INT+\x03+5s 重试+30s 强制；
- 输出：终端即时截断 24k/12k + 句柄续读协议；工具结果 >8k → handle；
- 权限：单点 toolApproval 钩子（policy 元数据 write/bypassesObserverBlock/bypassesApproval）；审批 5 分钟超时自动拒；grant 命令前缀匹配持久化；
- 会话记忆：goal(500)/decisions(15)/blockers(10)/lastCommand 正则抽取，仅压缩时注入。

### 8.2 opencode（commit 6c035e1，详见本会话研究记录）

- 主循环：step 循环，最后 assistant 消息无 pending 工具调用即终止；doom-loop 检测（同工具同输入 3 次 → ask）；
- 溢出：`usable = input_limit − reserved(默认 min(20k, maxOutput))`，`count ≥ usable` 即触发压缩；
- 压缩：专用压缩 agent（无工具）+ 固定摘要模板（Objective/Details/Work State/Next Move/Files，≤4096 tokens）+ `tail_start_id` 保留最近原文（预算 `clamp(usable*0.25, 2k, 15k)`）+ 重排注入（摘要 → 最近原文 → 新输入）；溢出压缩重放用户消息（media 降级文本占位）；
- system prompt：模型专属人设 txt（default/gpt/claude…）+ env + AGENTS.md（全局 + 项目根 findUp 首个）+ mcp/skills；读文件时按目录就近注入 `<system-reminder>`（每轮去重）；
- 工具输出：统一截断管线（2000 行 / 50KB），tail 保留 + 落盘引用 + 下一步建议；shell 保留尾部；
- 权限：规则数组后匹配者胜、默认 allow + 特例（.env 读取 ask、外部目录 ask、doom_loop ask）；always 模式用命令前缀 arity 字典（`git checkout *`）防过宽授权；V2 always 持久化 SQLite；
- 事件源式会话：LLM 事件（text/reasoning/tool/step delta）全部持久化，UI 是投影；
- 工具结果三段式：structured（UI）+ content（模型）+ output（用户原始）。

### 8.3 Codex CLI（详见 `docs/research/CODEX_CLI_ARCHITECTURE.md`）

- 系统提示：base instructions（模型目录 prompt.md）+ 13 个具名 world-state section（model/personality/token_budget/agents_md/permissions/environments…）+ 工具 JSON Schema + 会话历史；**section 快照 diff 只重注入变化**；
- 命令：默认 10s 超时（退出码 124）、1 MiB 输出 cap、head/tail 缓冲；
- approval_policy 四档：unless-trusted / on-request / granular / never；审批纯函数三态 + 会话级缓存 + prefix 规则记忆；
- 持久化：append-only JSONL（rollout-<ts>-<id>.jsonl）+ 回放恢复 + fork 保留父指针。

### 8.4 Warp（官方文档）

- block 是上下文基本单位；附加：sparkle 图标 / `CMD-UP` 附上一个 block、`CMD-UP/DOWN` 选择、`CMD-DOWN` 清除；区分 pending 与 attached；Agent 会话内自动附加正在处理的 block。

## 9. 当前终端绑定复核（2026-08-15）

主流终端 Agent 的共同边界不是让模型先枚举 UI 会话再挑选目标，而是由宿主把工具
绑定到用户当前打开的终端资源：

- Warp 的 Full Terminal Use 让 Agent 附着当前活动 PTY/buffer；Blocks as Context 将用户
  显式附加的 block 与 Agent 自己执行产生的 block 纳入对话，而不是自动吞入其它终端；
- VS Code Terminal tool 依赖当前终端的 Shell Integration 获取命令生命周期，资源选择由
  编辑器/工具宿主完成；
- OpenCode、Claude Code 与 Codex CLI 的 shell 工具同样由运行时决定 cwd/sandbox/session，
  模型参数描述任务，不承担应用窗口路由。

参考：

- <https://docs.warp.dev/agent-platform/capabilities/full-terminal-use>
- <https://docs.warp.dev/agent-platform/local-agents/agent-context/blocks-as-context>
- <https://code.visualstudio.com/docs/chat/chat-tools>
- <https://code.visualstudio.com/docs/agents/approvals>
- <https://opencode.ai/v2/docs/permissions>
- <https://docs.anthropic.com/en/docs/claude-code/cli-usage>

据此，ztermy 删除 `list_sessions`、`read_multi_session_status` 以及所有原生工具的
`session_id`/`session_generation` 模型参数。内部仍冻结 tab ID 与 reconnect generation，
这是宿主生命周期校验，不是模型能力。产品语义定为“一侧栏、一对话、一终端”；若未来
需要跨主机编排，应作为独立工作区 Agent 产品重新设计，而不是偷偷扩展当前侧栏。
