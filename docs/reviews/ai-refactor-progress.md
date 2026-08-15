# ztermy V3 AI 重构进度（节点记录）

> 每个节点 = 一个可验证的阶段：日期 + git commit + 范围 + 验证结果。
> 配套研究文档：`docs/reviews/ai-product-design-research-2026-08.md`；
> Netcatty 对照：`docs/reviews/netcatty-ai-comparison-2026-08.md`；
> Codex 专项：`docs/research/CODEX_CLI_ARCHITECTURE.md`。

## 节点 N22 — AI 输入区拖放附件（2026-08-15）

**commit**: 本节点提交（见 git 历史）

**范围**：

1. 将本地文件直接拖进当前终端的 AI 输入区即可附加，拖入期间显示原生主题化落点反馈；
2. PNG/JPEG/WebP/GIF 自动进入现有图片加载器，其它文件进入现有 UTF-8 文本加载器；实际内容、大小、数量与解码限制仍由 C++ 异步加载链校验；
3. 拖放不会自动发送消息，不读取普通终端输出，也不会把附件投递给其它终端；完成后焦点返回输入框；
4. 260px 真实窗口布局合同确认 DropArea 与所有标题/输入控件均留在 AI 侧栏内。

**验证**：QML/C++ 格式、1788/1788 翻译与真实窗口常规/紧凑布局门禁通过；外部 Explorer 拖放保留为 owner 交互验收。

## 节点 N21 — NetCatty 级工具时间线与紧凑标题栏（2026-08-15）

**commits**: `ab5eebb` + 本节点修复提交

**范围**：

1. 连续工具调用按助手轮次合并为 `Used N tools`，活动时展开、回合完成后自动折叠，并允许用户覆盖自动状态；
2. 每项保留状态、名称、摘要以及有界参数/结果详情，详情和代码分别局部滚动、可复制，恢复历史后保持不可执行快照；
3. 助手正文改为无边框层级，工具时间线和公开 reasoning 成为正文前的可折叠活动，不再堆叠成大卡片；
4. 320px 窄侧栏将导出等低频动作收进标题栏更多菜单，修复重复 delegate 的无障碍定位和 UTF-8 名称验证；
5. 修正 AI 命令建议的缺失图标资源名，并补齐全局 success 颜色 token，消除设置页和其它状态点的未定义颜色告警。

**验证**：动态 Debug 真实窗口布局 smoke 覆盖常规/320px AI 侧栏、工具分组、标题栏动作和无障碍合同；本节点最终门禁见提交记录。

## 节点 N20 — Codex 外部 Agent 轮次编排（2026-08-15）

**commit**: 本节点提交（见 git 历史）

**范围**：

1. 新增原生 `CodexAgentTurnRunner`，将 App Server 初始化、轮次、流事件、指标、取消和工具请求收敛为与内置 `AiTurnRunner` 相同的应用语义；
2. 同时覆盖即时工具结果和异步挂起/恢复，后续可直接复用现有终端、SFTP、等待命令、MCP 与审批执行链，不另造工具实现；
3. 每次仅允许一个当前终端工具挂起，取消时先终止挂起操作并回送取消结果，再中断 Codex turn；
4. 保留 App Server 线程供同一终端后续轮次继续使用，并记录墙钟与首 token 延迟。

**验证**：MSVC 动态 Debug 下全部 `codex-*` 4/4；假 App Server 覆盖即时工具、异步恢复和立即取消；新增/修改 C++ 文件通过 clang-tidy warnings-as-errors。

## 节点 N19 — Codex 类型化事件映射（2026-08-15）

**commit**: 本节点提交（见 git 历史）

**范围**：

1. 将 App Server 的文本、思考摘要、工具、联网检索、用量、完成、取消与失败通知映射到现有 `AiStreamEvent`，直接复用 ztermy 已有对话和活动卡语义；
2. 思考流优先展示可读摘要，仅在服务端没有摘要时于条目完成后回退到原始内容，避免重复展示；
3. 事件严格绑定活动 turn，拒绝迟到或游离事件，限制标识符、增量、错误及工具参数大小；
4. 原始思考使用线性字节累计，避免长流式输出产生二次复制开销；JSON 用量仅接受 IEEE-754 可精确表达的非负整数。

**验证**：MSVC 动态 Debug 下 `codex-app-server-*` 3/3；新增映射器及测试通过 clang-tidy warnings-as-errors。

## 节点 N18 — Codex 原生进程生命周期与能力探测（2026-08-15）

**commit**: 本节点提交（见 git 历史）

**范围**：

1. 新增全异步 `QProcess` App Server 客户端，覆盖握手、线程新建/恢复、轮次启动、立即或延后中断、失败传播和有界工具往返；
2. 恢复线程时重新发送当前模型、工作目录、开发指令与动态工具目录，拒绝跨线程事件、未知工具、重复/超量请求和失效工具结果；
3. 新增安装探测：读取实际 CLI 版本，调用 `generate-json-schema --experimental`，验证 `dynamicTools` 及请求/响应必需字段，避免按版本号猜测；
4. 本机 Codex CLI 0.146.0 的实验 schema 已确认包含动态工具合同；稳定 schema 不含实验字段属于预期行为。

**验证**：MSVC 动态 Debug 下 `codex-app-server-protocol` 与 `codex-app-server-client` 2/2；假 App Server 覆盖工具执行、排队取消、恢复与 schema 探测；六个新增/修改 C++ 文件通过 clang-tidy warnings-as-errors。

## 节点 N17 — 原生外部 Agent 协议地基（2026-08-15）

**commit**: `3265290`

**范围**：

1. 对照 Netcatty 外部 Agent 驱动与 OpenAI 官方 App Server/SDK/非交互协议，选择 Codex App Server 作为首个原生适配器，不引入 Web、Node 或 Python 运行时；
2. 落地有界 JSONL 协议层，覆盖初始化、线程新建/恢复、轮次启动/中断、动态工具响应及三类入站消息；
3. 固定单终端所有权：Codex 自身以只读沙箱运行，终端操作仅通过 ztermy 动态工具，继续服从既有权限、规则、去重、预算、审计、取消和重连代际；
4. 动态工具能力必须运行时协商，当前安装版本不支持时明确不可用，不静默扩大权限或切换 Agent；完整客户端与 UI 尚未宣称完成。

**验证**：MSVC 动态 Debug 构建与 `codex-app-server-protocol` 聚焦测试通过；新增协议源和测试通过 clang-tidy warnings-as-errors。

## 节点 N16 — Anthropic Server Tool 精确续接（2026-08-15）

**commit**: `ed980d2`

**范围**：

1. 将 Anthropic `pause_turn` 视为同一逻辑轮次的暂停而非完成，自动原样续接，最多四次；
2. 按事件索引重建有界 assistant content block，保留服务端搜索结果及 `encrypted_content`，同时兼容同轮 client/server tool；
3. 续接期间不发布残缺助手消息，最终只对 UI 发布一次完成结果；协议异常或循环超限明确失败；
4. Provider 原生续接内容计入上下文预算且不可被截断破坏。当前仅保证活动轮次内精确回放，跨用户轮次与重启后的持久化仍为后续合同。

**验证**：Anthropic stream mapper、request factory、turn runner 与 context compactor 聚焦测试通过，覆盖暂停后续接、加密搜索结果回放、单次完成发布和循环上限。

## 节点 N15 — Provider 原生联网检索与引用（2026-08-15）

**commits**: `98fe65f`, `69ffab4`, `3038b80`

**范围**：

1. OpenAI Responses 与 Anthropic Messages 分别接入官方原生联网检索协议；兼容 Chat Completions 与 Ollama 明确显示不支持，不静默降级；
2. Provider 流事件统一映射为检索开始、查询、完成和结构化来源，活动卡片与最终来源列表分工展示；
3. 来源按 URL 去重并允许后续标题/摘要补全，只接受有界 HTTP(S)，其容量计入助手消息预算；
4. 来源随助手消息进入加密会话历史，恢复后仍可点击；用户消息和 Agent evidence 不得携带来源；
5. 320px 窄侧栏隐藏模型下拉框为上下文、命令、联网、Agent 模式和发送控件让位；ADR 0090 记录 Provider 原生策略与精确原生结果回放的后续边界。

**验证**：Provider request/stream、conversation model/store、AppController 聚焦测试通过；
clang-tidy（warnings-as-errors）、C++/QML 格式、qmllint 与 1743/1743 翻译门禁通过；
动态 Debug 和静态 Release 均为 112/112。直接启动当前静态 Release 后，真实 Windows
窗口确认 320px 窄侧栏隐藏模型选择器、联网检索在兼容协议下明确禁用且底栏无越界，
扩宽至约 510px 后模型选择器恢复。OpenAI/Anthropic 实际联网、来源打开和原始
Markdown 复制仍保留为 owner/provider 验收项。

## 节点 N14 — Provider 原生图像附件收口（2026-08-15）

**commits**: `b6a1a60`, `55cffcd`, `98f81d6`, `25ca982`

**范围**：显式图像选择、异步解码与缩略图、当前终端草稿隔离、四类 Provider 原生载荷、历史回放占位、调试跟踪脱敏、中英文翻译和 Windows 真实窗口选择验收；详见 ADR 0089。

**验证**：动态 Debug 与静态 Release 112/112，翻译 1733/1733；真实窗口附件选择与预览通过。

## 节点 N13 — NetCatty 级会话主界面（2026-08-15）

**commit**: `fc2d61a`

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

**commits**: `12db6c2`, `7300c15` + pending follow-up

**范围**：

1. 删除模型可见的 `list_sessions` 与 `read_multi_session_status`；
2. 所有 ztermy 原生读写、SFTP、笔记、frame、wait 工具移除
   `session_id`/`session_generation` 参数与结果字段；
3. AppController 在回合开始冻结所属 tab ID + reconnect generation，通过宿主上下文注入
   工具；切换焦点不重定向，关闭或重连返回 `scope_changed`；
4. 序列化上下文与系统提示不再暴露内部路由标识；
5. 领域读取层直接接收唯一当前终端快照，应用层不再把它包装为候选会话集合或查找
   allowed-target set；内部 tab ID 与 reconnect generation 只用于异步代际校验；
6. ADR 0086 固化“一侧栏、一对话、一终端”，并记录 Warp、VS Code、OpenCode、Claude
   Code/Codex 的宿主资源绑定共性。

**验证**：动态 Debug 构建通过；读取层、terminal-frame、Agent scenario 与
`app-controller` 聚焦测试通过。模型可见 schema、结果、系统提示均有负向断言，确保旧
多会话工具与身份字段不会重新出现。全量门禁在本节点后续提交前继续执行。

## 节点 N11 — Provider 原生图片附件（2026-08-15）

**commits**: `b6a1a60` + `55cffcd` + `98f81d6`

**范围**：

1. 当前终端草稿支持显式 PNG/JPEG/WebP/GIF，多图缩略图、移除和纯图片消息；
2. OpenAI Responses、OpenAI-compatible、Anthropic、Ollama 分别使用原生多模态负载；
3. 图片加载、解码和缩略图生成移出 GUI 线程，并限制数量、单图/总字节和解码像素；
4. 后续轮次和历史只保留图片省略标记，不隐式重发二进制；恢复历史清空当前草稿附件；
5. 调试追踪保留结构和长度但省略 Base64，ADR 0089 固化该合同；
6. QML 文件选择结果显式转换为字符串 URL 列表再跨越 meta-object 边界，避免原生
   `url` 列表到泛型 variant 的静默丢失；上下文项使用可展开的人类可读预览，不再向
   用户暴露内部 serializer JSON。

**验证**：Debug 与静态 Release 全量测试均为 112/112；翻译门禁确认 1733 条全部完成；
真实 Windows Debug 窗口完成 PNG 选择、缩略图、移除入口与窄侧栏无越界检查；追踪净化
测试锁定 JSONL 不含原始 Base64。视觉模型对图片内容的回答仍保留为 owner/provider
验收项，不在自动化中向外部模型上传用户文件。

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
