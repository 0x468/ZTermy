# Codex CLI（Rust）内部机制研究报告 — 为 ztermy AI 子系统重构提供参考

> 研究对象：OpenAI Codex CLI（Rust 实现），仓库位于 `D:\tmp\ai-reference\codex`
> 版本特征：本仓库为较新版本（含 unified_exec、guardian、multi-agents v2、world_state 差分注入、状态库等）。
> 性质：架构/行为研究。以下全部为机制与设计要点提炼，未复制任何源码。
> 代码路径统一省略前缀 `D:\tmp\ai-reference\codex\`，下文中所有相对路径均相对该目录。

---

## A. 系统提示词构成清单

### A.0 总体结构

Codex 的"系统提示词"不是一个单一字符串，而是**base instructions（角色/行为主提示词）+ 按 step 动态注入的 world-state 片段 + 工具 JSON Schema 列表 + 会话历史**四部分，由 `Prompt` 结构组装后发给模型：

- 组装点：`codex-rs/core/src/session/turn.rs:1297` `build_prompt()` — `Prompt { input, tools: router.model_visible_specs(), parallel_tool_calls, base_instructions, output_schema }`
- 每个 step（一次模型采样）重新组装，见 `codex-rs/core/src/session/turn.rs:1325` `run_sampling_request()`。

### A.1 Base instructions（角色 + 行为规范）

来源：**模型目录（model catalog）驱动**，而非硬编码。

- 主提示词模板文件：`codex-rs/models-manager/prompt.md`（275 行），经 `include_str!` 编译进 `codex-rs/models-manager/src/model_info.rs:17`（`BASE_INSTRUCTIONS`）。
- 每个模型 slug 在目录中携带 `ModelMessages.instructions_template`（OpenAI 服务端下发），Codex 用模型目录里的模板，本地仅保留 fallback（`local_model_messages_for_slug`，`model_info.rs:187`）。gpt-5.2-codex 模板 = 人格头 + `{{ personality }}` 占位 + BASE_INSTRUCTIONS。
- 用户配置 `base_instructions` 可整体覆盖模板（`model_info.rs:52-63`）。
- prompt.md 内容要点（角色、能力、工作方式）：人格与语气、AGENTS.md 规范、前导消息（preamble）原则、`update_plan` 规划工具使用规范、任务执行准则（用 apply_patch 改文件、不 git commit、不加版权头、不输出行内引用）、验证工作方式（按 approval 模式决定是否主动跑测试）、回复格式规范（final message 结构、文件引用语法、tone 规则）、工具使用指南（shell 优先 rg 等）。
- 注意：**sandbox 说明、approval 说明不在 base prompt 里**，而是由 world-state 的 `permissions` 段动态注入（见 A.2、A.4）。

### A.2 World-state 段（动态注入，按 step 重组）

每个 step 由 `build_world_state_for_step()`（`codex-rs/core/src/session/world_state.rs:33`）收集全部"模型可见状态"，注册为带稳定 ID 的 section。section 清单（按注入顺序）：

| # | section ID | 文件 | 内容 | 角色 |
|---|---|---|---|---|
| 1 | `model` | `context/world_state/model.rs` | 模型身份；**仅当模型变化时**输出 model_switch 指令 | developer |
| 2 | `personality` | `context/world_state/personality.rs` | 人格指令（人格是否随模型烘焙） | developer |
| 3 | `token_budget` | `context/token_budget_context.rs` | 上下文窗口/token 预算提示、guidance | developer |
| 4 | `realtime` | `context/world_state/realtime.rs` | 实时对话模式指令 | developer |
| 5 | `agents_md` | `context/world_state/agents_md.rs` | AGENTS.md 内容（变化时带"替换/移除"说明） | user |
| 6 | `permissions` | `context/world_state/permissions.rs` | **sandbox 模式 + approval 策略说明**（模板见 A.4） | developer |
| 7 | `collaboration_mode` | `context/world_state/collaboration_mode.rs` | plan/default 模式指令 | developer |
| 8 | `environments` | `context/world_state/environment.rs` | **环境上下文**：cwd、shell、current_date、timezone、network、filesystem（见 B） | user |
| 9 | `environments_instructions` | `context/environments_instructions.rs` | 环境相关指令 | developer |
| 10 | `apps_instructions` / `plugins_instructions` | 对应文件 | 是否可用 apps/plugins 的说明 | developer |
| 11 | `tools` | `context/world_state/tools.rs` | 延迟加载的工具命名空间清单（带 4KiB 渲染预算） | developer |
| 12 | `multi_agent_mode` | `context/world_state/multi_agent_mode.rs` | 多 agent 模式（Custom/Proactive/ExplicitRequestOnly） | developer |
| 13 | 扩展段 | `session/world_state.rs:266-285` | 插件/扩展贡献的 world-state 段 | 各异 |

### A.3 差分注入机制（核心设计）

- `WorldStateSection` trait（`context/world_state/mod.rs:211`）：每个 section 实现 `snapshot()`（仅存比较所需的最小数据）+ `render_diff()`（对上一快照渲染"变化"）。
- 第一步/新会话：`render_full()` 全量注入；后续 step：`render_diff()` 只注入变化段（`record_step_world_state_if_changed`，`session/mod.rs:3067`）。
- 持久化采用 **RFC 7386 merge-patch**：`WorldStateSnapshot::merge_patch_from()`（`context/world_state/mod.rs:298`）生成补丁，恢复时 `apply_merge_patch()` 重建。段快照序列化进 rollout（见 E）。
- 段渲染为 `ContextualUserFragment`：带 role（developer/user）和 marker 标签，经 `context_manager/updates.rs:19 merge_contextual_fragments()` 合并进同 role 的相邻消息。

### A.4 Sandbox/Approval 说明（权限段模板）

`PermissionsInstructions`（`codex-rs/prompts/src/permissions_instructions.rs`，模板在 `codex-rs/prompts/templates/permissions/`）：

- sandbox 三段式模板：`sandbox_mode/read_only.md`（"只读，网络访问 {{network_access}}"）、`workspace_write.md`（"可在 cwd 与 writable_roots 内编辑"）、`danger_full_access.md`；由 permission profile 推导（`sandbox_prompt_from_policy`，`permissions_instructions.rs:193`）。
- approval 策略模板：`approval_policy/on_request.md`（57 行，含命令分段规则、require_escalated 用法、prefix_rule 规范与禁用前缀清单）、`never.md`、`unless_trusted.md`、granular 指令。
- 附加段：writable_roots 列表、denied_reads（"被禁止读取的路径，不得请求提升"）、approved_command_prefixes（已批准的 prefix 规则）。
- 模型目录可覆盖（`ApprovalMessages`/模型自带 approvals/permissions 消息优先于本地模板）。

### A.5 其余动态注入

- 当前时间提醒：`context/current_time_reminder.rs` + `session/time_reminder.rs`（"It is <UTC 时间>."）
- token 预算剩余提醒：`context/token_budget_context.rs`、`session/token_budget.rs`
- rollout 预算提醒：`rollout_budget.rs`、`session/rollout_budget.rs`
- 用户指令（AGENTS.md、hook 注入、MCP 提示）也以片段形式进入 developer/user 消息。

---

## B. 上下文管理：自动收集什么、大小控制、transcript 维护

### B.1 Step 级自动收集（`capture_step_context`，`session/mod.rs:3108`）

每个采样 step 重新捕获一次（"一次 step 的模型可见状态"）：
- 环境快照：cwd、workspace_roots、shell、环境就绪状态（`TurnEnvironment`，`session/turn_context.rs:44`）
- AGENTS.md：`agents_md_manager.refresh()`（`session/mod.rs:3129`），加载 CWD 到根目录链上的 AGENTS.md
- MCP 工具 + 工具路由表（`ToolRouter`）
- 选定 capability roots、executor 能力发现、扩展数据

渲染进 prompt 的环境信息（`context/world_state/environment.rs`）：
- `<cwd>`、`<shell>`、`<current_date>`（本地时区日期）、`<timezone>`
- `<network>`：allowed/denied 域名（`context/environment_context.rs:230`）
- `<filesystem>`：workspace_roots + permission profile 的读写条目（XML 形式）
- `<subagents>`：子 agent 列表

git 信息：`SessionMetaLine.git`（commit_hash/branch/repository_url，`codex-rs/protocol/src/protocol.rs:3134`）由 `codex-git-utils` 在**会话创建时**采集并写入 rollout 首行，供工具（diff/review）使用，不直接进 base prompt。

### B.2 transcript（对话历史）如何维护

- `ContextManager`（`codex-rs/core/src/context_manager/mod.rs` + `history.rs`）持有带注释的 `ResponseItem` 序列（`record_annotated_items`），每次采样前 `clone_history().for_prompt()` 生成模型输入。
- 两条"基线"：
  - `reference_context_item`（`TurnContextItem`）：首个真实用户轮次全量注入后建立；此后只注入上下文 diff（`record_context_updates_and_set_reference_context_item`，`session/mod.rs:3753`）。
  - `world_state_baseline`（`WorldStateSnapshot`）：供 world-state 段 diff 使用。
- 所有记录同时双写：内存 history + rollout JSONL（`persist_rollout_items`，`session/mod.rs:3659`）。

### B.3 大小控制

- **截断策略**：`TruncationPolicy`（bytes/tokens，`codex-rs/protocol/src/protocol.rs:3100` 附近）；模型目录带默认值，用户可用 `tool_output_token_limit` 覆盖（`model_info.rs:38-50`，默认 10k tokens 左右）。
- **工具输出上限**：shell/unified exec 输出上限 1 MiB（见 C.2），逐项记录时按截断策略裁剪。
- **自动压缩（auto-compact）**：`compact.rs` + `compact_token_budget.rs` — 接近上下文窗口阈值时触发；模型生成摘要（summary），只保留"摘要 + 最后一个用户消息 + 初始上下文 + world-state 基线"，换新 context window（`CompactedHistoryMetadata`）；token-budget 模式跳过摘要直接开新窗口。
- **上下文窗口跟踪**：`session/context_window.rs`（token 状态、auto-compact 范围/阈值）、`session/token_budget.rs`（预算剩余注入模型）。
- 压缩失败降级：`ContextWindowExceeded` 时从最旧项开始丢（`compact.rs:309-318`，保持前缀缓存）。

---

## C. 命令执行：shell 工具细节、approval_policy 各档位判定

### C.1 工具面（tool surface）

- 工具注册中心：`tools/registry.rs`；工具以 JSON Schema 暴露给模型（`ToolSpec`，`tools/mod.rs`），`strict: false`。
- 核心工具：`shell_command`（`tools/handlers/shell.rs` + `shell/shell_command.rs`）、`exec_command` + `write_stdin`（unified exec，交互式进程会话）、`apply_patch`（`tools/handlers/apply_patch.rs`）、`update_plan`、`request_permissions`、MCP 工具、多 agent 工具。
- 参数 Schema（`tools/handlers/shell_spec.rs`）：
  - shell_command：`command`（必填，PowerShell 版描述含 Windows 示例与安全规则）、`workdir`（默认 turn cwd）、`timeout_ms`（默认 10000）、`login`、`sandbox_permissions`、`justification`、`prefix_rule`
  - exec_command：`cmd`、`workdir`、`tty`（PTY 开关）、`yield_time_ms`（默认 10000，Windows 下限 10000/上限 30000）、`max_output_tokens`（默认 10000）、`shell`、`environment_id`

### C.2 执行管线（超时、输出捕获、截断）

入口 `run_exec_like`（`tools/handlers/shell.rs:64`）→ `ToolOrchestrator`（approval → sandbox → run）→ `exec.rs`：

- 超时：`DEFAULT_EXEC_COMMAND_TIMEOUT_MS = 10_000`（`exec.rs:58`）；超时后杀进程组，退出码归一化为 124（`EXEC_TIMEOUT_EXIT_CODE`），输出前缀 "command timed out after N milliseconds"（`tools/mod.rs:136`）。
- 输出上限：`EXEC_OUTPUT_MAX_BYTES = DEFAULT_OUTPUT_BYTES_CAP = 1 MiB`（`exec.rs:76`，`codex-rs/utils/pty/src/lib.rs:12`）；stdout/stderr 各自 capped，聚合时按 1/3 给 stdout、2/3 给 stderr 的预算分配（`aggregate_output`，`exec.rs:851`）。
- 事件流：最多 10k 个 `ExecCommandOutputDelta` 事件（`exec.rs:80`）实时推给 UI，聚合仍收全量（有上限）。
- 返回给模型的格式：`format_exec_output_for_model`（`tools/mod.rs:98`）→ "Exit code / Wall time / Total output lines（截断时）/ Output:"，再按模型截断策略截断。
- unified exec（交互式）：`unified_exec/mod.rs` — `exec_command` 返回 `session_id` 让模型用 `write_stdin` 继续交互；输出用 **head/tail buffer**（`unified_exec/head_tail_buffer.rs`）保留头尾各 50%、中间省略并标记 "... N bytes omitted ..."；1 MiB 上限；最多 64 个并发进程；yield_time 语义（250–30000ms）。
- 工作目录：默认 turn cwd（`workdir` 参数覆盖）；命令通过用户 shell 包装（`derive_exec_args`，bash/zsh/powershell/cmd）。

### C.3 approval_policy 各档位判定（`exec_policy.rs`，枚举在 `codex-rs/protocol/src/protocol.rs:913`）

判定核心：`create_exec_approval_requirement_for_command()`（`exec_policy.rs:312`）→ 输出 `ExecApprovalRequirement::Skip / NeedsApproval / Forbidden`（`tools/sandboxing.rs:152`）。

命令分析（`commands_for_exec_policy`，`exec_policy.rs:844`）：把 `bash -lc` 包装降级为独立命令段（管道/&&/||/; 分隔），PowerShell 走专用解析；解析不出时保守处理。规则来源：各配置层的 `rules/*.rules` 文件（Starlark 语法，`load_exec_policy`，`exec_policy.rs:637`）+ 内置启发式（`is_safe_command` 安全命令白名单、`is_dangerous_command` 危险命令如 `rm -f`）。

| 档位（新名/旧名） | 未命中规则的命令 | 危险命令 | 命中 prompt 规则 |
|---|---|---|---|
| `unless-trusted`（untrusted） | 白名单只读命令 → Allow；其余 → Prompt | Prompt | Prompt |
| `on-request`（on-failure，默认） | 无 sandbox 限制 → Allow；受管 sandbox 内且未请求提升 → Allow（由 sandbox 兜底）；请求提升 → Prompt | Prompt | Prompt |
| `granular` | 同 on-request，但按分类（sandbox_approval/rules/skill_approval/request_permissions/mcp_elicitations）决定 Prompt 或直接 Forbidden | Prompt（若允许） | 按 rules 开关 |
| `never` | Allow（完全靠 sandbox） | **Forbidden** | **Forbidden**（拒绝原因：approval required by policy, but AskForApproval is Never） |

- 结果经 `prompt_is_rejected_by_policy`（`exec_policy.rs:216`）二次裁决：granular 里禁用的分类直接转 Forbidden。
- 审批流（`tools/approvals.rs`）：Hook（PermissionRequest hooks）→ Guardian 自动审查（approvals_reviewer=auto_review 时）→ 用户；`with_cached_approval`（`tools/sandboxing.rs:70`）按"命令+环境+cwd+sandbox_permissions"键缓存 `ApprovedForSession`，同命令不再问。
- 审批选项：yes(一次)/approve for session/approve for prefix（生成 exec-policy 前缀规则，`append_amendment_and_update`，`exec_policy.rs:439`）/deny/cancel；prefix 有禁用清单（`BANNED_PREFIX_SUGGESTIONS`，`exec_policy.rs:56`）。
- 申请提升的参数模型：`sandbox_permissions: "require_escalated"` + `justification`（面向用户的问题）+ 可选 `prefix_rule`（`shell_spec.rs:298`）。

### C.4 sandbox 实现思路

- 策略选择在 `codex-sandboxing` crate（`SandboxManager::select_initial`），core 只做适配（`sandboxing/mod.rs`、`tools/sandboxing.rs`）。
- 平台后端：Linux landlock/seccomp、macOS seatbelt、**Windows 受限令牌（unelevated）或 CreateProcessAsUser 提升（elevated）**（`exec.rs:597` `exec_windows_sandbox`、`windows_sandbox.rs`）；Windows 还支持私有桌面（`windows_sandbox_private_desktop`）。
- sandbox 失败重试：orchestrator 首次在 sandbox 内跑，`is_likely_sandbox_denied` 命中后按策略升级重试（绕过 sandbox），**不重复审批**（缓存）——`tools/orchestrator.rs:5-8` 模块注释。
- 配置入口：`sandbox_mode`（read-only / workspace-write / danger-full-access，`codex-rs/protocol/src/config_types.rs:86`）与 `permission_profile` 双轨（`config/mod.rs:3280` 校验互斥），`windows.sandbox` 控制 Windows 档位（`config/types.rs:158`）。

### C.5 apply_patch

- 解析 + 校验：`codex-apply-patch` crate 解析 freeform patch，`verify_apply_patch_args_with_mode` 对照环境文件系统校验（`tools/handlers/apply_patch.rs:404`）。
- 安全判定：`safety.rs:26 assess_patch_safety` — 所有写路径在 writable roots 内且沙箱可执行 → AutoApprove；否则 AskUser；`never` 下不可自动批准 → Reject。apply_patch 与 shell 共用同一 orchestrator（审批 + sandbox）。
- 交互：流式参数 diff 实时推送改动预览（`ApplyPatchArgumentDiffConsumer`，500ms 节流）。

---

## D. Agent 循环：工具循环、错误恢复、预算

### D.1 turn 主循环（`session/turn.rs:153 run_turn`）

1. 预采样压缩检查（`run_pre_sampling_compact`）→ 分析输入所需 MCP 服务器 → 捕获第一个 step context → 记录上下文更新（world state 基线）。
2. 进入循环：
   - 取 pending input（用户在模型运行期间提交的消息，`input_queue`）
   - 捕获 step context（`capture_step_context`）→ `record_step_world_state_if_changed` 注入世界状态 diff
   - `run_sampling_request` 采样一次（见下）
   - 采样后：若模型需要继续（needs_follow_up）或 token 达上限 → `run_auto_compact`（`turn.rs:453`）或换上下文窗口 → continue；否则跑 stop hooks → break。
3. 循环终止条件：模型本轮只输出最终消息且无工具调用。

### D.2 采样与工具执行循环（`try_run_sampling_request`，`turn.rs:2154`）

- 流式消费模型响应事件；`OutputItemDone` 时 `handle_output_item_done`（`stream_events_utils.rs:288`）→ 工具 future 压入 `FuturesOrdered` 并行执行（支持并行工具调用时）。
- 工具结果写回历史 → 下一次循环重新 `clone_history().for_prompt()` 组装输入 → 再次采样。即：**模型 ↔ 工具循环在一个 sampling request 内由事件驱动完成**，`run_sampling_request` 返回时要么模型给最终消息，要么需要 follow-up。
- 工具执行本身走 `ToolCallRuntime`（`tools/parallel.rs:41`）→ handler → orchestrator。

### D.3 错误恢复

- 流错误重试：`handle_retryable_response_stream_error`（`turn.rs:1416`），最多 `stream_max_retries` 次，指数退避（`compact.rs:328 backoff`）。
- `TurnAborted`：用户中断，直接退出。
- `ContextWindowExceeded`：触发自动压缩（`turn.rs:1393`）；压缩中再超则丢最旧项。
- `UsageLimitReached`：记录 rate limits 后退出。
- 工具级错误（ToolError::Rejected/Codex）→ 以函数调用输出形式回喂模型，由模型调整策略。

### D.4 预算

- token 预算：`session/token_budget.rs`、`context/token_budget_context.rs`（把剩余预算写进 prompt 让模型自觉控制输出）。
- rollout 预算：`rollout_budget.rs`（core）+ `session/rollout_budget.rs`（`maybe_record_reminder`，`turn.rs:306`）。
- 自动压缩窗口：`auto_compact_window`（`session/mod.rs:3686`），窗口 ID 格式 `{thread_id}:{window_number}`，随压缩推进；TokenBudgetContext 告诉模型窗口历史。
- 线程层：`codex_thread.rs` 管理线程生命周期与并发。

---

## E. 会话持久化与恢复（resume）

### E.1 rollout 文件格式

- 存储：**append-only JSONL**，路径 `~/.codex/sessions/rollout-<timestamp>-<conversation_id>.jsonl`（`codex-rs/rollout/src/recorder.rs:77-84`）。
- 写入器：后台 writer task（命令通道 AddItems/Persist/Flush/Shutdown，`recorder.rs:124`），记录 `terminal_failure`。
- 记录类型 `RolloutItem`（`codex-rs/history/src/lib.rs:94`）：`SessionMeta`（首行）、`ResponseItem`、`InterAgentCommunication`、`Compacted`（压缩摘要+窗口号）、`TurnContext`（turn 配置快照）、`WorldState`（**merge-patch 补丁**）、`SecurityRiskScore`、`EventMsg`。
- 首行 `SessionMeta`（`codex-rs/protocol/src/protocol.rs:2861`）：session_id、thread_id、forked_from_id、parent_thread_id、timestamp、cwd、model_provider、**base_instructions**（恢复时回放）、dynamic_tools、history_mode、history_base（继承另一 rollout 的前缀区间）、subagent 起始序号等；`SessionMetaLine` 附 git 信息。

### E.2 恢复（resume）

- `InitialHistory::Resumed(ResumedHistory { conversation_id, history, rollout_path })`（`history/src/lib.rs:207`）：读取 rollout 全量回放，重建 ContextManager 与 world-state 基线；`Forked` 用于派生线程（保留 fork 父指针）。
- 重建逻辑：`session/rollout_reconstruction.rs`（用 rollout 里的 ResponseItem + WorldState patch + TurnContext 重建模型输入）。
- TUI 恢复流程：`codex-rs/tui/src/session_resume.rs` — 从 rollout 首行或状态库读取 cwd/model/thread_id；`ResumeCwdMode`（session/current）决定 cwd 提示；resume picker（`tui/src/resume_picker/`）列出历史会话；归档：`session_archive_commands.rs`（`~/.codex/sessions/archive`）。
- 线程元数据另有状态库（`codex-rs/state/` 系）做索引，rollout 为事实源（`list_threads_with_db_fallback`，`recorder.rs:344`）。
- 配置的 base_instructions 变化：恢复时用 rollout 首行保存的 base_instructions（provenance 区分 model/custom）。

---

## F. 对 ztermy 重构可借鉴的 8 条设计（按价值排序）

> ztermy 是 Windows 11 原生 Qt/C++ SSH 终端。以下借鉴取其"机制"而非代码；落地时需适配 Qt/C++ 与 SSH 远程执行的现实（远端无法用本地 sandbox，需换位到"远端策略 + 本地 UI 审批"）。

### 1. 世界状态 = 具名 section + 快照 diff，只重注入变化（最高价值）
Codex 的 `WorldStateSection`（`context/world_state/mod.rs:211`）把"模型可见状态"拆成稳定 ID 的段，每段只存最小快照，每轮只渲染变化（全量仅首次），并以 RFC 7386 merge-patch 持久化。
**对 ztermy**：AI 上下文按 section 建模——`session`（SSH 会话信息）、`server`（远端 OS/shell/工作目录）、`history`（终端输出摘要）、`permissions`（本地允许的策略说明）——快照比较后增量注入，显著降低 token 成本并让模型只关注变化（cwd 变化、连接状态变化才注入）。

### 2. 审批决策与 UI 分离：纯函数 + 三态结果 + 缓存
`exec_policy.rs` 把"命令 → 判定"做成无 UI 依赖的纯函数，输出 `Skip / NeedsApproval / Forbidden`（`tools/sandboxing.rs:152`），UI 只是渲染 `ApprovalAction`（`tools/approvals.rs`）；`ApprovalStore`（`tools/sandboxing.rs:40`）按 (命令,cwd,sandbox) 键缓存"本次会话已批准"。
**对 ztermy**：QML 只负责展示审批卡片，C++ 持有策略引擎与审批缓存；同一命令重复出现不再打扰用户（例如"允许对这台主机执行 git pull"）。

### 3. 统一执行管线：超时归一化 + 输出上限 + 头尾缓冲
`exec.rs` 的单一路径（默认 10s 超时 → 退出码 124、输出前缀超时说明；1 MiB 输出上限；stdout/stderr 预算分配；返回模型的消息含 Exit code/Wall time/截断行数）。unified exec 用 head/tail 缓冲保留首尾 50%（`unified_exec/head_tail_buffer.rs`）。
**对 ztermy**：远端命令执行走同一抽象——超时（对慢速 SSH 连接建议可配置，默认放宽）、输出 cap、结构化结果（exit code、wall time、截断标记），既防模型上下文爆炸也防终端被刷屏。

### 4. "默认受限 + justification 升级"闭环，附 prefix 记忆
Codex 先在有界沙箱内执行，失败（`is_likely_sandbox_denied`）或需要越界时，模型带 `justification`（面向用户的问题）+ `prefix_rule`（可记忆的命令前缀规则）请求提升，重试不重复审批（`tools/orchestrator.rs`）。
**对 ztermy**：本地侧"命令先按安全默认执行、失败后以明确理由请求放行"的节奏可直接照搬；前缀规则（"以后这类命令不再问"）是用户粘性很高的交互。SSH 远端可映射为"本地 shell 包装 + 命令分类器 + 用户审批"。

### 5. 会话 = append-only JSONL 类型化转录 + 回放恢复
`RolloutRecorder`（后台 writer 任务）写 `rollout-<ts>-<id>.jsonl`，首行 SessionMeta（cwd、model、base_instructions），后续行是带类型的 `RolloutItem`；resume 即全量回放重建（`history/src/lib.rs`、`session/rollout_reconstruction.rs`）；fork 线程保留父指针。
**对 ztermy**：AI 助手对话与远端会话记录用版本化的 JSONL（每行类型化、可追加、可恢复），首行保存会话元数据（主机、工作目录、模型）；"继续会话"= 回放；另起分支 = 新文件 + 父引用。比单一大 JSON 更抗损坏、便于裁剪。

### 6. 提示词 = 数据文件 + 模型目录 + 动态注入三分离
base prompt 是 `prompt.md`（`models-manager/prompt.md`）经 include_str 编译，sandbox/approval 说明是独立模板目录（`prompts/templates/permissions/`），且按模型目录/配置可覆盖。
**对 ztermy**：提示词模板放资源文件（Qt 资源或 docs），不同模型/不同主机类型可切换模板集；系统提示与动态上下文彻底分离，便于版本管理。

### 7. 工具面显式化：JSON Schema + 平台特定说明 + 并行执行
每个工具是 `ToolSpec`（`shell_spec.rs`），描述里直接写死平台安全规则（Windows 下 PowerShell 示例、禁止跨 shell 拼接删除命令等，`shell_spec.rs:405`）；支持并行工具调用（`tools/parallel.rs`）。
**对 ztermy**：先定义 ztermy 的工具体面（ssh_exec、upload_file、query_state 等）为 JSON Schema；Windows 特有安全说明直接写进工具描述（模型会遵守）；并行执行池按需开启。

### 8. 上下文窗口预算的"自反馈"机制（防遗忘）
Codex 把剩余 token 预算写进 prompt（`token_budget_context.rs`），并在接近阈值时自动压缩：模型出摘要，只留"摘要 + 最后用户消息 + 世界状态基线"，开新上下文窗口（`compact.rs`）；压缩还有专门的降级路径（丢最旧项保缓存）。
**对 ztermy**：长会话 AI 助手必须内置压缩——把终端输出/对话折叠为摘要、保留最后指令与连接状态基线；模型可见的"预算剩余"提示让模型主动精简输出，避免突然断上下文。

### 附：其余值得留意的点（未进前 8，但低成本高回报）
- 工具调用的**事件流节流**：实时 delta 最多 10k 事件、审批 diff 500ms 节流，防 UI 与模型输入爆炸（`exec.rs:80`、`apply_patch.rs:58`）。
- 模型切换时注入 model_switch 指令段（`context/world_state/model.rs`），保证换模型后指令不丢失——ztermy 支持多模型时应保留。
- hooks（pre/post tool use）作为扩展点（`tools/handlers/shell.rs` 的 pre_tool_use_payload/post_tool_use_payload）——ztermy 可在执行前后接审计/脚本。

---

## 关键文件速查（均在 `D:\tmp\ai-reference\codex\codex-rs` 下）

| 主题 | 文件 |
|---|---|
| Base prompt | `models-manager/prompt.md`、`models-manager/src/model_info.rs` |
| World-state 组装 | `core/src/session/world_state.rs`、`core/src/context/world_state/mod.rs` |
| 环境上下文 | `core/src/context/world_state/environment.rs`、`core/src/context/environment_context.rs` |
| 权限/审批说明 | `prompts/src/permissions_instructions.rs`、`prompts/templates/permissions/**` |
| 上下文收集 | `core/src/session/mod.rs`（capture_step_context/record_*）、`core/src/context_manager/` |
| 工具面 | `core/src/tools/handlers/shell_spec.rs`、`core/src/tools/registry.rs` |
| 执行管线 | `core/src/exec.rs`、`core/src/unified_exec/`、`core/src/tools/runtimes/shell.rs` |
| 审批判定 | `core/src/exec_policy.rs`、`codex-rs/protocol/src/protocol.rs`（AskForApproval） |
| 审批流 | `core/src/tools/approvals.rs`、`core/src/tools/sandboxing.rs`、`core/src/tools/orchestrator.rs` |
| sandbox | `core/src/sandboxing/mod.rs`、`core/src/windows_sandbox.rs`、`core/src/tools/sandboxing.rs` |
| apply_patch | `core/src/apply_patch.rs`、`core/src/tools/handlers/apply_patch.rs`、`core/src/safety.rs` |
| Agent 循环 | `core/src/session/turn.rs`、`core/src/session/session.rs`、`core/src/session/handlers.rs` |
| 压缩/预算 | `core/src/compact.rs`、`core/src/compact_token_budget.rs`、`core/src/session/token_budget.rs` |
| 持久化 | `rollout/src/recorder.rs`、`history/src/lib.rs`、`core/src/session/rollout_reconstruction.rs` |
| 恢复 | `tui/src/session_resume.rs`、`tui/src/resume_picker/` |
| TUI 审批键位 | `tui/src/keymap.rs`（approval: y/a/p/d/Esc-n/c）、`tui/src/bottom_pane/approval_overlay.rs` |
