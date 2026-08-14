# ztermy V3 AI 重构进度（节点记录）

> 每个节点 = 一个可验证的阶段：日期 + git commit + 范围 + 验证结果。
> 配套研究文档：`docs/reviews/ai-product-design-research-2026-08.md`；
> Netcatty 对照：`docs/reviews/netcatty-ai-comparison-2026-08.md`；
> Codex 专项：`docs/research/CODEX_CLI_ARCHITECTURE.md`。

## 节点 N13 — NetCatty 级会话主界面（2026-08-15）

**commit**: pending

**范围**：

1. 将固定 212px 的历史卡片改为占满对话区域的会话视图；打开历史时隐藏消息与输入框，关闭后原位恢复；
2. 历史行改为整行鼠标/键盘可恢复，删除按钮保持独立命中区域，列表继续有界滚动；
3. 空会话展示最近三条对话和“查看全部”，对齐 NetCatty 的 recent/history/new 信息架构；
4. 移除“Encrypted history”这类内部实现术语，界面只表达“全部对话”；
5. 根据 NetCatty 本地实现、Warp Conversations 与 VS Code Chat Sessions 复核：会话是 Agent 交互的一级对象，New/History 位于侧栏头部，模型与执行模式位于输入区。

**验证**：QML 格式与 qmllint 通过；动态 Debug 构建和 `qml-native-window-smoke` 通过；真实 Windows 窗口验证空态最近对话、完整历史视图、关闭恢复与 320px 窄侧栏无越界。

## 节点 N12 — 快捷消息与便携 Agent Skills（2026-08-15）

**commits**:
- `8fca992`（feat(ai): add reusable quick messages）
- `15f68d8`（feat(ai): add portable user skills）

**范围**：

1. 快捷消息作为可编辑提示词模板，通过 `/slug` 插入输入框，不自动发送；
2. 用户技能遵循 Agent Skills 的目录 + `SKILL.md` 合同，后台扫描并逐项报告警告；
3. 模型默认只看有界元数据，通过 `list_skills` / `load_skill` 按需读取正文；用户显式技能以最多四个可移除 chip 固定到当前回合；
4. Skills 与快捷消息共享键盘优先的斜杠选择器，但存储、语义和请求构造保持独立；
5. 设置页提供路径、重新加载、打开目录、就绪/警告状态；ADR 0088 固化渐进披露与当前终端作用域。

**验证**：catalog/tool/AppController/QML 聚焦测试通过；翻译与真实窗口验收并入 N13 收口。

## 节点 N11 — 显式本地文本上下文（2026-08-15）

**范围**：

1. AI 附加菜单支持一次选择最多四个本地 UTF-8 文本文件；每个源文件最大 256 KiB；
2. 文件读取、UTF-8/二进制验证在工作线程完成，不阻塞 Qt Quick；
3. 结果绑定发起请求的终端 tab，切换焦点不会串入其它终端，关闭 tab 后结果安全丢弃；
4. 同一路径再次附加会刷新已有上下文；现有移除、固定、预览、脱敏和上下文预算继续生效；
5. ADR 0087 固化显式、无环境扫描、异步且有界的文件上下文合同。

**验证**：Debug 增量构建通过；translation-catalog、ai-context-broker、app-controller、
qml-native-window-smoke 4/4 通过；翻译门禁确认 1651 条全部完成。

## 节点 N10 — 原生 AI 工具绑定当前终端（2026-08-15）

**commits**: `12db6c2` + pending follow-up

**范围**：

1. 删除模型可见的 `list_sessions` 与 `read_multi_session_status`；
2. 所有 ztermy 原生读写、SFTP、笔记、frame、wait 工具移除
   `session_id`/`session_generation` 参数与结果字段；
3. AppController 在回合开始冻结所属 tab ID + reconnect generation，通过宿主上下文注入
   工具；切换焦点不重定向，关闭或重连返回 `scope_changed`；
4. 序列化上下文与系统提示不再暴露内部路由标识；
5. ADR 0086 固化“一侧栏、一对话、一终端”，并记录 Warp、VS Code、OpenCode、Claude
   Code/Codex 的宿主资源绑定共性。

**验证**：动态 Debug 构建通过；12 个关联 AI 聚焦测试与 `app-controller` 通过。全量门禁
在本节点后续提交前继续执行。

## 节点 N9 — Agent 模式合同、工具协议与长命令审批修复（2026-08-14）

**commit**: pending

**范围**：

1. `wait_terminal_frame` 改为条件性参数合同：只要求 session identity + revision，changed/idle/timeout 使用明确默认值；`write_to_pty.append_enter` 默认 false；
2. 删除没有独立语义的 Edit 模式；Read-only/Ask/Auto/YOLO 四档同时约束 tool catalog、system prompt 与客户端执行策略；
3. Auto 对高风险命令和外部 MCP 强制审批，YOLO 仍保留显式 deny、schema/scope、ownership、budget；
4. 审批长命令放入有界可滚动 viewport，修复越界绘制重影；
5. 终端命令 prefix 改为 token 边界匹配，并为一组保守的低风险命令族提供默认前缀建议；复杂/高风险/未知命令仍默认 exact；
6. 评审记录：`docs/reviews/V3_AI_SYSTEM_REVIEW_2026-08-14.md`；架构决策：ADR 0085。

**验证**：Debug 构建通过；全量 `ctest` 108/108 通过；`ztermy_clang_tidy_check` 237/237 通过；48 个 QML 文件通过格式门禁；真实窗口 `ztermy_ui_layout_runtime_smoke` 通过，并产出常规/紧凑/浅色 AI 面板、长命令审批与无障碍契约工件。改动 C++ hunks 通过 clang-format 检查；仓库全量格式目标仍会报告本节点之外既存文件的历史格式差异，本节点未用全仓机械格式化制造无关 churn。

## 节点 N1 — 研究基线 + P0 正确性修复（2026-08）

**commit**: `c66a3e1`（fix(ai): close correctness gaps found in product research）

## 节点 N2 — 指令注入重构：分层 system prompt + 读取策略教学（2026-08）

**commit**: `09d6d31`（feat(ai): layered system prompt and reading-strategy guidance）

**范围**（对应研究 §4/§8.2 opencode 分层、Netcatty guidelines）：
1. 新建 `AiSystemPromptBuilder`（src/application/ai/）：身份/证据边界/读取策略/命令协议/工具策略/输出格式六段式；
   读取策略明确：长输出用 read_command_output 游标续读（has_more 循环）；read_terminal 仅当前屏；
   禁止 cat/type/Get-Content 打印文件内容到终端再读；user_input_pending 时不要立即重试；
2. read_terminal / read_command_block / read_command_output 工具描述升级为多行自然语言（何时用/何时不用/截断语义）；
3. 测试：`tests/ai_system_prompt_builder_tests.cpp` 锁定 prompt 各段。

**验证**：Debug 构建通过；33/33 AI 测试通过。

## 节点 N3 — UX：审批键盘流（2026-08）

**commit**: `d26c60b`（feat(ai): keyboard-first approval card (Enter approve / Esc deny)）

**范围**（对应研究 §5.1 Netcatty Enter=批准/Esc=拒绝）：
- 审批卡出现时自动强聚焦；Enter/Return 批准、Escape 拒绝；按钮旁显示快捷键提示。

**验证**：qmlcachegen/qmllint 通过；33/33 AI 测试通过。

## 节点 N4 — 会话存储加固：跨进程锁 + .bak 认证回退（2026-08）

**commit**: `82a547f`（fix(ai): lock conversation store and recover from backup on auth failure）

**范围**（对应评审 H6）：
1. 所有 load/upsert/erase/clear/export 读-改-写周期持有跨进程 `QLockFile`（5s 超时、30s 陈旧锁回收）——消除双实例静默丢更新与首写密钥创建竞态；
2. 主文件密文认证失败时回退 .bak envelope 解密（LKG 结构校验覆盖不到的比特翻转场景），暴露 `lastLoadRecoveredFromBackup()` 供 UI 提示；
3. 测试：认证失败回退、双实例交错更新保留。

**验证**：Debug 构建通过；33/33 AI 测试通过（含 2 个新用例）。

## 节点 N5 — 上下文压缩：预算感知 + typed 压缩 + 413 强制压缩重试（2026-08）

**commits**:
- `42bd658`（feat(ai): typed context compaction bounds requests to the model window）
- `e01204a`（feat(ai): retry with forced compaction on provider context overflow）

**范围**（对应研究 §8.1 Netcatty typed 压缩链 / §8.2 opencode preserve-recent 预算）：
1. 新建 `AiContextCompactor`（domain，纯函数）：token 估算（UTF-8 字节/4，与 context broker 同口径）、usable 预算（context − 输出预留 − 缓冲）、typed 压缩（旧消息 head/tail 截断、最近 N 条保留原文、工具输出 2k 字符封顶、UTF-8 边界安全）；
2. `sendAiMessage` 发送前压缩请求视图（会话模型不改动，后续轮次确定性重复压缩）；
3. `AiProviderErrorCode::contextOverflow`：HTTP 413 映射为可重试；`AiTurnRunner` 在无可见输出/无副作用工具时以更紧预算强制压缩并立即重试，仍超预算则保留原错误失败；
4. 测试：小请求不动、旧消息截断+尾部保留、工具输出封顶、UTF-8 安全、413 分类、端到端压缩重试（重试 payload 更小）。

**验证**：Debug 构建通过；34/34 AI 测试通过。

## 节点 N6 — 命令管理：合成回显（终端可见性）（2026-08）

**commit**: `aeac771`（feat(ai): synthetic echo makes agent commands visible in the terminal）

**范围**（对应研究 §8.1 Netcatty 合成回显）：
1. `AiCommandEcho::markerLine`：命令以单行 shell 注释形式在终端内宣告（pwsh/POSIX `#`、cmd `rem`），空白/换行折叠为单空格防逃逸；
2. `run_command` 执行前直接派发 marker（不经 observeTerminalInput，不污染输入重建与历史）；`interrupt_command` 发送 \x03 前宣告；
3. 测试：各 shell 标记形式、多行折叠、注释逃逸加固。

**验证**：Debug 构建通过；35/35 AI 测试通过。

## 节点 N7 — 上下文管理：scrollback 分页读取（2026-08）

**commit**: `99044eb`（feat(ai): read_terminal_output pages through the real scrollback）

**范围**（对应研究 §8.1 Netcatty terminal_read_context 分页语义）：
1. `TerminalEngine::scrollbackPage(firstLine, lineCount)` 新接口 + Ghostty 实现（全文格式化 + 行范围切片，SCREEN 坐标含历史+当前屏，返回 total/scrollback 行数供分页）；
2. Local/SSH backend 转发（ghostty 内部同步，调用线程查询与 worker feed 安全并发）；
3. 新工具 `read_terminal_output`：最多 300 行 / 16 KiB 分页、has_more 续读、UTF-8 安全截断、untrusted 标注、turn 预算；scrollback 是追加式证据，实时读取不适用冻结快照规则；
4. 测试：引擎分页（中间页/越界/非法参数）；工具目录更新为 14；fake backend 实现接口。

**验证**：Debug 构建通过；36/36（AI + terminal-engine）测试通过。

## 节点 N8 — 权限一致性：edit 模式对所有变更先询问（2026-08）

**commit**: `8bc495a`（fix(ai): edit mode asks before mutations like run_command）

**范围**（对应研究 §5.2 模式矩阵一致性）：save_runbook 与 SFTP 传输此前在 edit 模式静默放行而 run_command 询问——所有变更工具现在统一在 edit 模式先询问。

**验证**：35/35 AI 测试通过；全量 108/108 通过。

**范围**：
1. 研究完成：Netcatty 源码实测（A-F 六部分 + 16 项差距）、opencode commit 6c035e1（V1/V2 双架构）、Codex CLI（`docs/research/CODEX_CLI_ARCHITECTURE.md`）、Warp 官方文档；
2. 对比研究文档落盘：`docs/reviews/ai-product-design-research-2026-08.md`；
3. P0 正确性修复：
   - `AiTurnRunner`：toolCallCompleted 全量参数走 16 KiB 限额（此前绕过）；startAttempt 重置 attempt 级状态（m_responseId/pendingToolCalls/reasoning/toolContinuationPending），杜绝失败响应的 previous_response_id 泄漏；
   - `AiProviderRetryPolicy`：Retry-After 不再被 8s 退避上限截断（独立 5 分钟天花板）；`ProviderHttpClient` 支持 HTTP-date 格式 Retry-After；
   - `AiWaitCommandTool`：wait_command 结果输出封顶 24 KiB + output_truncated 标记，杜绝 64 KiB 上限悬挂轮次；
   - `AppController`：wait_command 与 wait_terminal_frame 轮询回调检查 completePendingTool 返回值，失败转入可见 error 态（此前悬挂）；
   - 输入交错防护：executeAiRunCommand/executeAiWriteToPty 在用户半行输入时返回 `user_input_pending`；用户输入到达时 agent 持有写租约 → handoffToUser，用户行完成 → resumeAgent（接线此前为死代码的接管机制）；
   - `McpJsonRpcProtocol`：新增 reset()（跨重启清缓冲）；tools/list 的 JSON-RPC error 显式失败（此前静默空成功）；
   - `McpStdioClient`：握手 10s 超时、工具调用 60s 超时（此前无任何超时）；fail() 强制 kill 子进程（此前协议失败后进程继续运行）。

**验证**：Debug 全量构建通过；`ctest -R ai` 32/32 通过（N1 提交前复跑）。
