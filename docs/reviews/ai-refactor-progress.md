# ztermy V3 AI 重构进度（节点记录）

> 每个节点 = 一个可验证的阶段：日期 + git commit + 范围 + 验证结果。
> 配套研究文档：`docs/reviews/ai-product-design-research-2026-08.md`；
> Netcatty 对照：`docs/reviews/netcatty-ai-comparison-2026-08.md`；
> Codex 专项：`docs/research/CODEX_CLI_ARCHITECTURE.md`。

## 节点 N1 — 研究基线 + P0 正确性修复（2026-08）

**commit**: 待 N1 提交后回填（`git rev-parse --short HEAD`）

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
