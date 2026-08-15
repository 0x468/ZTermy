# Netcatty AI 实现对比研究报告（ztermy V3 参考）

日期：2026-08
调研对象：`D:\Repo\tmp\Netcatty`（Electron/TypeScript SSH 终端，GPL-3.0，2046 文件）
性质：产品行为参考研究。仅提炼交互模式与架构思路，**不复制任何源码/素材/品牌**（遵循 ztermy AGENTS.md 产品边界）。
对照基准：ztermy V3（见 `docs/V3_AI_PROGRAM.md` 与 `docs/reviews/V3_AI_PRODUCT_REVIEW_2026-08-13.md`）。

> 文中文件引用均相对 Netcatty 仓库根目录；行号引用标注关键位置，便于复核。

---

## 1. Netcatty 实际发布的 AI 功能清单

### 1.1 聊天面板（按终端/工作区作用域）
- 每个终端 Tab/工作区拥有**独立作用域**（`scopeType: terminal | workspace | global`）的会话与草稿，切换 Tab 即切换 AI 上下文（`components/AIChatSidePanel.tsx` 中 `scopeKey = `${scopeType}:${scopeTargetId}``）。
- 会话历史抽屉（`components/AIChatSessionHistoryDrawer.tsx`）、空态时的 "Recent" 最近会话（Zed 风格）、导出 Markdown/JSON/TXT（`components/ai/ConversationExport.tsx`）、新对话、自动标题（首条消息截断 50 字符，`AIChatSidePanel.tsx` `autoTitleSession`）。
- 面板挂载优化：隐藏时若无草稿/消息/正在流式输出则卸载，否则保活（`shouldKeepAIChatSidePanelMounted`）；激活延迟到下一帧避免切换 Tab 卡顿。

### 1.2 命令执行（Agent 直接在真实 PTY 中执行）
- 内置 **Catty Agent** + 外部 CLI Agent（Claude Code、Codex、Cursor、Copilot CLI、Codebuddy、OpenCode），PATH 自动发现 + 鉴权探测（`components/ai/AgentSelector.tsx`、`electron/bridges/aiBridge/agentDiscoveryHandlers.cjs`、`sdk/*Driver.cjs`）。
- 命令**真正敲进终端**（`typedInput: true`），shell 专用包装器（posix/powershell/cmd/fish）+ 合成回显，用户肉眼可见命令在终端里执行（`electron/bridges/ai/ptyExec.cjs`、`ptyExecHelpers.cjs`、`shellUtils.cjs` `formatSyntheticEcho`）。
- 后台长任务：jobStart/jobPoll/jobStop 轮询模型，256KB 输出窗口、增量 offset、取消（Ctrl+C+signal 重试+30s 强制上限）（`electron/terminalWorker/aiExec.cjs`）。
- 串口/网络设备会话走 raw 模式（不套 shell、无退出码、禁用分页提示写进 system prompt）。

### 1.3 上下文
- **System prompt**：作用域描述 + 会话清单（hostname/label/protocol/os/user/shell/deviceType/hostChain/端口转发/连接状态）+ 权限模式规则 + web search 开关 + 用户技能（`infrastructure/ai/cattyAgent/systemPrompt.ts`）。
- **按需读取终端**：`terminal_read_context` 工具，支持 viewport/tail/head/lines 范围，默认 80 行、上限 300 行，返回 hasMoreBefore/After（`domain/terminalContextRead.ts`、`infrastructure/ai/harness/capabilityTools.ts`）。
- **显式选择附加**：终端选中文本 → 浮动 "Add to Conversation" 按钮（Sparkles 图标）→ 切到 AI 面板并附加为附件 chip（`components/terminal/TerminalView.tsx` ~719-757、`application/state/terminalSelectionAttachment.ts`、`components/TerminalLayer.tsx` `handleAddSelectionToAI`）。
- **会话记忆**：user goal、decisions、blockers、每会话最后命令，压缩后重新注入（`infrastructure/ai/harness/sessionState.ts`）。
- 附件：粘贴/拖拽文件（图片、PDF 等）chips；terminal selection 以 `[Terminal selection: xxx.log]` 文本块拼进 prompt（`buildPromptWithTerminalSelectionAttachments`）。

### 1.4 Provider / 模型
- **12 个内置预设**：openai/anthropic/google/ollama/openrouter/qwen/deepseek/kimi/zhipu/doubao/mimo/custom，含默认 baseURL、模型列表端点与默认模型（`infrastructure/ai/types.ts` `PROVIDER_PRESETS`）。
- 协议族抽象：openai / anthropic / google 三种 wire family（`ProviderStyle`），第三方兼容端点归入其一。
- 模型：Agent 硬编码预设（Claude Opus4.6/Sonnet4.6/Haiku4.5；Codex GPT-5.5…带 thinking level low/medium/high/xhigh；Cursor Composer-2.5…）+ SDK 运行时模型目录（`aiSdkAgentListModels`）+ 提供方 /models 发现 + 每模型 context window 覆盖 + 高级参数（temperature/topP/frequencyPenalty/presencePenalty/maxTokens）（`components/settings/tabs/ai/ModelSelector.tsx`、`AIChatSidePanelHelpers.ts`）。
- **API key 不进渲染进程**：renderer 用占位符 `__IPC_SECURED__`，主进程替换真实 key 后发请求（`infrastructure/ai/sdk/providers.ts` `API_KEY_PLACEHOLDER`）。

### 1.5 流式
- Vercel AI SDK `streamText`：text/reasoning/tool_call/tool_result/usage/performance 事件 → UI（`infrastructure/ai/harness/turnDrivers/cattyStreamProcessor.ts`）；文本经 requestAnimationFrame 批量 flush；thinking 块带 shimmer + 计时。
- HTTP 413 自动触发强制压缩并重试一次（`cattyRequestTooLargeRetry.ts`、`requestPayloadCompression.ts`）。

### 1.6 工具目录（50+）
`terminal_execute/terminal_poll/terminal_start/terminal_stop/terminal_read_context/get_environment`、`sftp_*`（list/read/write/upload/download/chmod/rename/mkdir/stat…）、`vault_hosts_create/import/list`、`vault_notes_*`、`snippets_*`、`scripts_*`（JS 自动化 nct API）、`portforward_*`、`host_open`、`url_fetch`、`web_search`、`tool_output_read`、`workspace_get_info/session_info`（`infrastructure/ai/harness/generated/cattyToolSpecs.json`、`shared/toolExecutors.ts`、`harness/capabilityTools.ts`）。

### 1.7 安全 / 权限
- 三档：observer（只读）/ confirm（写操作弹审批）/ auto（自由执行，blocklist 除外）（`cattyToolApproval.ts`、systemPrompt 权限规则）。
- 全局命令 blocklist（正则，**ReDoS 防护**：拒绝嵌套量词）；网络设备/串口跳过 blocklist（vendor CLI 语义不同）（`cattyAgent/safety.ts`、`shared/toolExecutors.ts`）。
- 审批卡：Once / Always（持久化 grant 规则）/ Reject，Enter=批准、Esc=拒绝，自动滚动+自动聚焦（`components/ai-elements/tool-call.tsx`、`shared/approvalGate.ts`、`harness/permissionGrants.ts`）。
- 命令超时默认 60s（可配到 24h）、maxIterations 默认 20、每会话一次一个命令（busy lock + 执行队列）（`shared/sessionExecutionQueue.ts`）。

### 1.8 斜杠命令 / 提及 / 键盘
- 输入 `/` 弹出**光标锚定**的 listbox：用户自定义 Quick Messages（slug+prompt）+ 用户技能（skill），方向键/Enter/Esc（`components/ai/ChatInput.tsx`、`SlashCommandPicker.tsx`、`infrastructure/ai/quickMessages.ts`）。
- `@` 提及作用域内主机。
- Enter 发送、Shift+Enter 换行（CJK 组合输入守卫）、Esc 关菜单；流式时发送键变 Stop 方块（`components/ai-elements/prompt-input.tsx`）。

### 1.9 网络搜索
Tavily / Exa / Bocha / Zhipu / SearXNG（`WEB_SEARCH_PROVIDER_PRESETS`），作为 `web_search` 工具 + system prompt 主动搜索指引。

### 1.10 本地化
en / ru / zh-CN / zh-TW 全量翻译（`application/i18n/locales/*/ai.ts`）。

---

## 2. 与"朴素聊天侧边栏"的 UX 差异（重点）

1. **AI 回答不只在面板里，还"活"在终端里**：Agent 执行命令时把命令真实敲进 PTY（带合成回显），用户在终端里看到命令被敲出、运行、输出——不是把命令文本贴回聊天框。面板里同步出现可折叠工具卡（`$ cmd` 等宽标题 + 参数 + stdout/stderr/退出码）（`electron/bridges/ai/ptyExec.cjs`、`components/ai-elements/tool-call.tsx` `extractDisplayCommand`）。
2. **面板是终端 Tab 的右栏子面板之一**（SFTP/Scripts/History/Theme/Notes/AI Chat 同列），按 Tab 作用域渲染（`components/terminalLayer/TerminalLayerSupport.tsx` `AIChatPanelsHost`：`absolute inset-0 z-10` 覆盖层 + 侧栏 slot）。不是浮在窗口上的独立聊天窗。
3. **选中即带上下文**：终端选中文字出现悬浮 Sparkles 按钮 → 一键切到 AI 并把选区作为附件 chip 挂进草稿（Settings 里可关闭该动作）。上下文是**显式附加**，不是隐式抓取。
4. **审批内联在消息流中**：待批准的工具卡自动滚入视野、自动聚焦 Approve，三按钮 Once/Always/Reject，键盘可完成全流程；拒绝/超时（5 分钟）在流里给出明确提示文案。
5. **斜杠命令锚定光标**，选择即插入 prompt 文本或技能 token，不用开命令面板。
6. **作用域可视化**：每 Tab 独立会话历史，面板标题下方可感知当前会话属于哪个终端/工作区（scopeKey 贯穿）。
7. **性能敏感挂载**：隐藏面板只保留有"活内容"的实例（草稿文字/消息/流），空面板直接卸载；激活延迟一帧。
8. **输入框即教学**：placeholder "Message Catty Agent — @ to include context, / for commands"；空态 "Ask about your servers, run commands, or get help with configurations." + Recent 列表。
9. 无独立 onboarding：配置入口集中在 Settings → AI（Providers/Agents/Tools/Search/Safety 五个子页 + 外部 Agent 发现卡片 + 权限规则管理 + Quick Messages 编辑器）。

---

## 3. 架构

### 3.1 分层
React UI（`components/`）→ 应用状态（`application/state/useAIState.ts`、`useAIChatStreaming.ts`）→ `infrastructure/ai/harness`（AgentRuntime + TurnDrivers）→ Vercel AI SDK `streamText` → 自定义 fetch 适配器 → Electron IPC bridge（`window.netcatty.aiFetch / aiChatStream`）→ 主进程发 HTTP（真实 key）；命令执行走 worker 的 PTY exec handlers（`netcatty:ai:exec / jobStart / jobPoll / jobStop`）。

### 3.2 Turn 编排（AgentRuntime）
- 每 chat session 串行执行 turn；AbortSignal 取消；事件流（`AgentEvent`：turn_start / tool_call / tool_result / model_delta / usage / performance / compaction…）广播给 UI 与 traceStore；ToolOutputStore（大输出句柄） + ToolResultDedup（防重放） + SessionStateStore（记忆）（`infrastructure/ai/harness/agentRuntime.ts`）。

### 3.3 Catty 循环（streamText + tools）
- `streamText({model, messages, instructions, tools, toolApproval, stopWhen: isStepCount(maxIterations)})`；每步执行工具后把结果作为 tool 消息追加进 UI，模型可多轮迭代；`prepareStep` 在每步前做 token 预算/压缩/记忆注入（`cattyStreamProcessor.ts`、`cattyTurnDriver.ts`）。
- 工具执行经 `createCattyToolsFromCatalog`（Zod schema 由 cattyToolSpecs.json 生成）→ `shared/toolExecutors.ts`（会话作用域校验 → observer 拦截 → blocklist → bridge.aiExec）→ `electron/terminalWorker/aiExec.cjs`（PTY 写包装命令）。

### 3.4 终端上下文采集
- 每会话注册 `TerminalContextReader`（scrollback+viewport+pending 文本快照），`terminal_read_context` 工具按需读取；系统提示词里的 host 列表由 `useTerminalAiContexts` 实时构建（`components/terminalLayer/useTerminalAiContexts.ts`、`domain/terminalContextRead.ts`）。

### 3.5 多 Provider
- 三协议族 `createModelFromConfig`（@ai-sdk/openai|anthropic|google）；流经 IPC 的 fetch 适配器（非流式 aiFetch / 流式 aiChatStream + ReadableStream 拼装）；主进程替换占位 key。外部 Agent 走各自官方 SDK 驱动（`electron/bridges/aiBridge/sdk/claudeDriver.cjs` 等）+ 独立鉴权（claudeAuth.cjs / codexHelpers.cjs）。

### 3.6 内存管理（值得抄作业的点）
token 估算 → 预压缩（LLM 摘要 temp=0，保护最近 N 条，保留最新用户目标）→ 413 重试强制压缩；压缩 trace 上报 UI（compaction 状态文案）（`harness/contextManager.ts`、`contextCompaction.ts`、`cattyRuntime.ts`、`contextBudget.ts`、`tokenEstimator.ts`）。

---

## 4. UI 设计要点（值得借鉴）

- **消息样式**（Claude-Code 风格）：用户消息右侧圆角描边气泡（bg-muted/50、border-border/50），助手回复左侧纯文本无边框，**无头像**（`components/ai-elements/message.tsx`）。
- **Thinking 块**：流式时展开 + "Thinking" shimmer + 秒表；结束自动折叠为 "Thought for 4s" + 60 字预览；内容区顶部渐变淡出（`components/ai/ThinkingBlock.tsx`）。
- **工具卡**：状态图标色编码——黄色盾牌(待批)/蓝色转圈(执行中)/红色×（错误）/绿色✓(完成)/斜杠(中断)；等宽 `$ cmd` 标题 + tooltip；可折叠 Arguments/Result；审批按钮红 Reject / 绿 Once / 绿 Always（`components/ai-elements/tool-call.tsx`）。
- **工具分组**：连续工具调用合并为 "Used N tools" 可折叠组，Agent 还在干活时保持展开，回复后自动收起（`components/ai/ToolCallGroup.tsx`）。
- **终端上下文卡**：显示 `lines 12-34 / 200 | live | more available` 摘要（`components/ai/toolArtifacts/TerminalArtifactCard.tsx`）。
- **Markdown**：Streamdown + CJK 折行 + 安全代码高亮；宽代码/表格局部横向滚动，不撑宽面板；每代码块右上角 Copy 只复制源码。
- **输入区**：附件 chip 行（终端选区=方括号图标 / 图片 / 文件）、provider+model 胶囊、发送键三态（箭头→转圈→停止方块）、展开按钮（`components/ai/ChatInput.tsx`、`prompt-input.tsx`）。
- **颜色**：Tailwind token 低透明度混色（border/25、bg-muted/10、text-foreground/62 等），审批用黄/绿/红点缀；终端同色系 chrome（compose bar 用 color-mix 派生）。
- **可定制钩子**：`.ai-chat-message[data-role=...]`、`[data-section="ai-chat-panel"]` 供 Custom CSS（en/ai.ts 有说明）。
- **性能细节**：React.memo 深度相等、lazy+Suspense fallback、rAF 批量 flush、消息分批渲染（50 条/批 + "Load earlier messages"）、useDeferredValue 会话列表、Profiler 遥测。

---

## 5. 对比表：Netcatty vs ztermy V3

| 维度 | Netcatty（源码实测） | ztermy V3（依 `docs/V3_AI_PROGRAM.md` 与产品评审推断） |
| --- | --- | --- |
| 面板形态 | 每终端 Tab 右侧子面板（SFTP/Scripts/History/Theme/Notes/AI 并列），按 terminal/workspace 作用域隔离会话 | 原生 QML 工作台侧栏，可缩放/移动（评审表："Dedicated terminal-side Agent surface — Implemented"） |
| 上下文默认 | system prompt 注入 host 清单 + 权限规则；终端内容**按需**读取（terminal_read_context 工具）；选中文本显式附加 | 普通提问**只含会话**，不隐式抓取视口/回滚；显式附加选区/最近 1/3/5 条语义命令块（CommandBlockStore + OSC133/633，无语义标记时降级为标注的近似附件） |
| 命令生成→执行 | Agent 直接执行（真 PTY 敲入+合成回显+工具卡），confirm 模式审批；无独立 Insert 流 | Generate → Insert 或显式 Run；"direct visible Run 即授权、不加冗余警告"（原则 5/默认清单） |
| 权限模式 | observer / confirm / auto + 全局 blocklist + 持久 grant | Read-only / Ask / Edit / Auto / YOLO 五档 + allow/ask/deny 规则（精确/前缀/glob/正则 + once/session/profile/global）+ 可编辑建议匹配器（0.3.6）——比 Netcatty 更细 |
| 工具面 | 50+：terminal/sftp/vault hosts/snippets/scripts/portforward/notes/web/url | run_command/read_command_output/wait_command/interrupt_command/write_to_pty + SFTP 读写 + 遥测/转发/脚本/笔记/历史工具 + MCP 客户端（0.3.4+） |
| Provider | 12 预设 + custom + 三协议族；key 主进程持有 | OpenAI Responses / Anthropic / DeepSeek / Kimi / Z.AI / Ollama / 兼容端点，品牌预设不泄漏协议到 UI（原则 3） |
| 外部 Agent | 内置 Catty + Claude/Codex/Cursor/Copilot CLI/Codebuddy/OpenCode SDK 驱动 | 0.3.11 进行中：Codex App Server 有界协议层已落地；原生进程客户端、能力协商、事件映射、当前终端工具桥和选择器仍待端到端完成，保持权限/审计/取消契约一致 |
| 流式 | streamText + thinking 块 + usage/performance 事件 + 413 压缩重试 | 流式 + 合并后再做 Markdown 布局（Qt Quick 线程不阻塞）；reasoning 跟随流式展开、出正文即折叠 |
| 记忆 | 每作用域持久会话 + token 预算压缩（LLM 摘要）+ 会话状态再注入 | 默认仅会话保留；持久加密历史 opt-in（0.3.1 ADR 0061）；Agent 工具证据随会话保留（bounded） |
| 斜杠命令 | Quick Messages（用户自定义 prompt 快捷）+ 技能，光标锚定 listbox | 键盘优先的内置斜杠选择器（0.3.9）；用户技能 0.3.10 复用同一选择器 |
| 选区附加 | 选中终端文本 → 浮动按钮 → 附加为附件 chip | 附件菜单：当前选区/最近 1/3/5 条命令（语义块），随请求生效并保留为证据 |
| 网络搜索 | Tavily/Exa/Bocha/Zhipu/SearXNG 工具 | 0.3.10 规划：provider 无关工具 + 可见引用 |
| 审计/可观测 | traceStore + 每步 usage/performance 事件 + 面板诊断开关 | 活动/审计视图（0.3.1 ADR 0060）+ 本地 JSONL 请求追踪（0.3.5） |
| 终端语义 | prompt 探测 + shell kind 探测 + marker 包装 + 合成回显 | OSC133/633 + nonce 语义命令块 + 能力质量（none/basic/rich）+ 输出覆盖元数据——ztermy 语义更重 |
| 双写冲突 | 每会话 busy-lock + 执行队列（一次一条） | 每会话一个 Agent 写租约 + 只读 observer fanout + 有序 user/Agent PTY 输入（0.3.1） |
| 语言 | en/ru/zh-CN/zh-TW | en/zh-CN |

**ztermy 已有优势**：五档模式与规则系统、语义命令块（OSC133/633）、写租约与并发门、无 UI 线程 I/O、加密历史与审计、不复制 React/Electron 结构（评审已明示"按能力追踪 parity，不复制组件结构"）。

---

## 6. 可借鉴结论（仅提炼模式，不复制实现）

1. **Agent 动作的"终端可见性"**：命令真实敲进 PTY + 合成回显 + 面板工具卡双通道呈现，透明且低学习成本。ztermy 已有 PTY 写租约，可考虑在工具卡上补"命令已在终端执行"的同步反馈（如回显行高亮）。
2. **工具卡信息架构**：折叠态只显示 `$ cmd` + 状态色图标，展开才看参数/结果——与 ztermy 0.3.7 的"原生工具卡"目标一致；其 Enter/Esc 审批快捷键、自动滚动聚焦值得对齐。
3. **Provider 协议族归并**（openai/anthropic/google 三种 wire family + 主进程持 key）印证 ztermy"Provider 无关类型 + 适配器"原则；Netcatty 的每模型 context window 覆盖与 413 压缩重试可补进 ztermy 的 context budget 设计评审。
4. **Quick Messages**（用户自定义 prompt 快捷 + slug 斜杠）是轻量、无 provider 耦合的"伪技能"；ztermy 0.3.10 技能可先以此形态落地再扩展。
5. **面板性能工程**：rAF 批量 flush、分批渲染、隐藏面板按"有活内容才保活"卸载、lazy 加载——与 ztermy"流式合并后再布局、AI 不阻塞 16ms 输入预算"目标同源，可互相印证验收口径。
6. **显式附加是共同结论**：Netcatty（选中文本按钮）与 ztermy（语义命令块附件菜单）都不做隐式抓取；ztermy 的语义块 + 近似降级设计在定位上领先，Netcatty 的"选区按钮常驻终端"交互可作为降级路径的补充参考。
