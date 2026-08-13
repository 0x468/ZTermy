# ztermy V3 AI 重构进度（节点记录）

> 每个节点 = 一个可验证的阶段：日期 + git commit + 范围 + 验证结果。
> 配套研究文档：`docs/reviews/ai-product-design-research-2026-08.md`；
> Netcatty 对照：`docs/reviews/netcatty-ai-comparison-2026-08.md`；
> Codex 专项：`docs/research/CODEX_CLI_ARCHITECTURE.md`。

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
