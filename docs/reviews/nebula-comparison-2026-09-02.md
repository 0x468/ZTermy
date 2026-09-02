# Kuddev/nebula 与 ztermy 源码级对比报告

日期：2026-09-02

性质：产品、架构与实现对比；不构成迁移方案或第三方代码采用许可

结论可信度：源码与仓库文档为主，发布页与截图为辅；未构建、未运行 Nebula 二进制

## 1. 对比快照与方法

本报告对比以下两个固定快照：

| 项目 | 快照 | 版本线 | 本地位置 |
|---|---|---|---|
| Nebula | `f4314a8d68b653e5cac1930eae3467c4af786c36` | `v1.5.0-2-gf4314a8` | `D:\tmp\nebula` |
| ztermy | `ecd94aeb1adf835f7cd484502e9aba9b77ff2904` | `0.4.4` | `D:\Repo\Qt\ztermy` |

资料来源：

- Nebula 固定提交：[Kuddev/nebula@f4314a8](https://github.com/Kuddev/nebula/tree/f4314a8d68b653e5cac1930eae3467c4af786c36)
- Nebula README：[README.md](https://github.com/Kuddev/nebula/blob/f4314a8d68b653e5cac1930eae3467c4af786c36/README.md)
- Nebula 1.5.0 发布页：[Nebula Terminal 1.5.0](https://github.com/Kuddev/nebula/releases/tag/v1.5.0)
- Nebula 许可证：[GPL-3.0](https://github.com/Kuddev/nebula/blob/f4314a8d68b653e5cac1930eae3467c4af786c36/LICENSE)
- Nebula Runtime API：[runtime-control-api.md](https://github.com/Kuddev/nebula/blob/f4314a8d68b653e5cac1930eae3467c4af786c36/docs/runtime-control-api.md)
- ztermy 当前源码、架构文档、ADR、验收文档与性能报告。

采用了四种核对方式：

1. 阅读产品文档、发布说明、安装说明和截图；
2. 查看 Cargo/CMake 依赖、crate/target 边界和平台代码；
3. 追踪会话、终端、SSH、SFTP、AI、窗口和持久化的实际代码路径；
4. 用源码规模、测试入口、最大文件和现有性能报告检查工程成熟度。

限制：Nebula 未在本机完成编译和真实交互验收，因此报告中的“已实现”表示存在连贯源码、测试或发布证据，不等于本机验证通过。两者的性能负载不同，不能直接比较吞吐数字。

## 2. 一句话结论

Nebula 是一个面向本地开发、WSL 和 AI CLI 的“持久化终端工作台”；ztermy 是一个面向 PVE/VPS/服务器日常管理的“原生 SSH 工作台 + 内置供应商 AI 助手”。

Nebula 的领先面主要在通用终端工作流：常驻会话、AI CLI 生命周期、快捷终端、命令补全、文件/Git 抽屉、窗格广播和 Runtime 控制 API。ztermy 的领先面主要在远程运维闭环：主机工作台、远程资源监控、原生端口转发、可恢复 SFTP 批量传输、每主机连接策略、终端编码和真正内置的对话式 AI。

因此，Nebula 不是 ztermy 应当整体追赶的新模板。它揭示了 ztermy 在“终端本身的日常效率”上仍有缺口，但它最重的 AI CLI/Agent 编排方向与 ztermy 已确定的产品边界不同，不应被当成差距照搬。

## 3. 产品定位差异

| 维度 | Nebula | ztermy |
|---|---|---|
| 首要对象 | 本地 Shell、WSL、SSH、AI CLI | 保存的 SSH 主机、本地 Shell、SFTP 与运维工具 |
| 核心叙事 | 终端会话、分屏和 AI CLI 不随窗口消失 | 一个原生、低摩擦、可长期日用的 Windows SSH 工具 |
| AI 主路径 | 在真实终端里运行 Claude/Codex/OpenCode/Pi 等外部 CLI，并检测其状态 | 应用内自有的供应商驱动助手，直接对话并调用当前终端工具 |
| 信息架构 | 终端工作区为中心，Files/Git/SFTP/Agents 是围绕窗格的能力 | 工作台/主机为中心，终端、SFTP、监控、脚本、笔记和转发围绕主机展开 |
| 典型用户 | 本地开发者、AI 编程 CLI 重度用户 | 管理多台服务器、PVE/LXC/VPS 的技术用户 |
| 产品广度 | 接近终端 + 文件浏览器 + Git 客户端 + Agent 控制台 | 接近 SSH 管理器 + SFTP + 运维面板 + 内置 AI 助手 |

如果评价标准是“替代 Windows Terminal/Warp 并承载多个 AI CLI”，Nebula 当前明显更完整。如果评价标准是“每天管理服务器，连接、监控、传输、转发、脚本和 AI 问答在一个工具内闭环”，ztermy 的方向更聚焦。

## 4. 底层架构

### 4.1 技术栈

| 层 | Nebula | ztermy |
|---|---|---|
| 语言 | Rust 2024 edition，固定 Rust 1.97.1 | C++23 + QML |
| UI | GPUI + gpui-component；保留旧 winit/OpenGL 壳 | Qt 6.8 Quick/Quick Controls，自定义 Windows 非客户区 |
| 终端核心 | Alacritty 派生网格 + `vte` 解析 | 固定版本 `libghostty-vt` 适配层 |
| 终端绘制 | GPUI 自定义 `Element`，按单元格塑形并直接画 quad/text | 单个 `QQuickItem`，当前主要为 CPU `QImage` 栅格化后上传纹理，光标独立节点 |
| 本地 PTY | ConPTY；发布包附带 side-by-side `OpenConsole.exe`/`conpty.dll` | Windows 系统 ConPTY，独立读写线程 |
| SSH | `russh` + Tokio，应用内原生协议栈 | `libssh2` + 原生 Windows socket/事件等待，应用内原生协议栈 |
| SFTP | `russh-sftp`，复用已认证连接 | libssh2 SFTP；终端、目录浏览和传输采用明确的拥有者/worker 边界 |
| 网络/AI | `ureq`、Tokio、供应商特定 JSON | Qt Network 异步请求、流式事件映射、供应商适配器 |
| 配置 | Lua 5.4 + 文本设置 + 多个 JSON/JSONL 存储 | 多个显式版本化 JSON 存储 + Windows/便携凭据边界 |
| 安装 | Inno Setup，ZIP + setup EXE | WiX MSI，静态/动态 portable；静态版可做到单 EXE 主体 |

### 4.2 Nebula 的结构

Nebula 是一个 9 成员 Cargo workspace：`nebula_app`、`nebula_terminal`、`nebula_config`、`nebula_config_derive`、`nebula-completions`、`nebula_hook`、`nebula_gpui`、`nebula_settings`、`nebula_split`。

它的正确边界包括：

- 终端网格与 UI 框架隔离，PTY I/O 线程不触碰 GPUI；
- `nebula_split` 用纯数据树表达布局、比例和方向导航；
- Runtime API 把 UI/PTY 操作派回真正的 owner 线程；
- SSH 连接阶段来自 `russh` 的真实调用点，不靠解析 `ssh -v` 文本；
- 会话快照原子写入，并有三次启动失败后的 crash-loop breaker；
- 发布依赖固定到精确 Git SHA。

但当前结构也背负明显成本：

- GPUI 已是默认产品壳，旧 winit/OpenGL 壳仍保留，形成迁移期双实现；
- 对 GPUI、gpui-component 和 winit 使用自有 fork/补丁，升级责任落在项目自身；
- `nebula_app` 仍是超大聚合 crate，存在多个 3,000～10,000 行文件；
- 根 `Cargo.toml` 同时承担依赖配置、迁移说明和补丁知识库，认知负担较大。

### 4.3 ztermy 的结构

ztermy 的分层意图更传统：QML 负责呈现，C++ application 层协调状态，domain 层保存纯模型，infrastructure 层负责 SSH、SFTP、ConPTY、凭据、网络、日志和 Windows 平台能力。

它的正确边界包括：

- SSH、PTY、文件传输和 AI 网络请求不阻塞 GUI/render thread；
- 终端视口始终是一个自定义 item，不创建单元格 QML 对象树；
- SSH 连接、SFTP、传输、端口转发、远程监控各自有可取消 worker；
- 工作区仅保存“恢复意图”，不会把重启后的 SSH 冒充为仍存活的远端会话；
- installed/portable/session 三类凭据和数据位置有明确合同；
- 静态与动态包分别验证其依赖闭包。

当前主要结构债务是 `AppController.cpp` 过度集中，QML 主页面和设置/AI 面板也偏大。虽然底层能力已拆出 domain/application/infrastructure 类型，但 UI-facing 状态和动作仍大量汇集到单个控制器。

### 4.4 规模与测试表面

以下是固定快照上的静态统计，不代表代码质量高低：

| 指标 | Nebula | ztermy |
|---|---:|---:|
| 第一方主要语言文件 | 336 个 Rust 文件 | 353 个 C++/H/QML 源文件 |
| 第一方主要语言行数 | 约 188,155 | `src` 约 99,918 |
| 独立测试目录代码 | 约 4,291 行；大量测试内嵌在模块中 | `tests` 约 24,534 行 |
| 静态识别测试入口 | 约 1,448 个 `#[test]`/`#[tokio::test]` 属性 | CMake 注册 118 个测试 |
| 最大文件 | `display/mod.rs` 约 10,371 行 | `AppController.cpp` 约 15,753 行 |

Nebula 的测试颗粒更细、数量更大，但没有在本次研究中实际执行。ztermy 的测试入口较少，但覆盖了较多独立进程、真实窗口、ConPTY、打包和可选真实主机合同。二者不能仅按测试数量判断可靠性。

## 5. 终端与本地 Shell

### 5.1 Nebula 更强的部分

1. **Shell 覆盖更广**：PowerShell 7、Windows PowerShell、CMD、Git Bash、Nushell、WSL 发行版均有检测和稳定 profile id。
2. **内联与弹窗补全**：持久历史、目录、PATH 命令和文件候选可显示 ghost text 或列表；远端/WSL 会路由到各自文件系统。
3. **快捷终端**：全局 `Ctrl+`` 打开 Quake 风格顶部终端，PTY 与 scrollback 隐藏时保留。
4. **窗格工作流更成熟**：拖拽分屏、窗格脱离成 tab、缩放、按方向聚焦、标题头、tab 内广播输入。
5. **命令面板更像统一入口**：窗口、tab、pane、cwd、已保存命令和 AI 会话动作集中检索。
6. **内联图片和文档**：OSC 1337 图片、Markdown/GFM、图片、原生 TeX 数学和代码/JSON 文档 tab。
7. **顶部 tab 过载处理**：分页、前后导航、边缘自动滚动，而不是仅依赖固定宽度或普通滚动。

### 5.2 ztermy 更强或更明确的部分

1. **终端编码边界明确**：UI/引擎内部 UTF-8，远端可选 UTF-8/GB18030，并处理跨 packet 的不完整序列；SFTP 文件名编码也有独立策略。
2. **Windows 原生交互细节较完整**：Snap Layout、原生 hit-test、DPI、IME、链接/路径/地址/Git hash quick select、复制后选区策略、右键/中键行为均有专门合同。
3. **性能决策有可复现 A/B 记录**：光标独立节点、8 ms 最新快照节流、AI 工作台惰性保留均来自固定 Release 负载。
4. **低性能模式已有产品入口**：可保持所选材质语义，同时使用不透明表面和减少动画；Nebula 更依赖 GPU/GPUI 路径。

### 5.3 ztermy 的现实缺口

- 没有 Nebula 级别的 fish-style 补全和统一候选弹窗；
- 没有全局快捷终端；
- 分屏存在，但拖拽重排、pane detach、broadcast input、pane header 等工作流不如 Nebula 完整；
- tab 过多时的分页、边缘自动滚动和导航提示仍可继续打磨；
- 没有通用的 Files/Git 抽屉和文档 tab。

其中前三项与“终端是否顺手”直接相关；Files/Git/TeX 则很可能让 SSH 工具变成另一款编辑器，应慎重。

## 6. SSH、代理、SFTP 与运维

### 6.1 SSH 连接

两者都已经是应用内原生 SSH，不是简单包一层系统 `ssh.exe`。

Nebula：

- 用 `russh` 实现直连，支持私钥、证书、加密密钥口令、Windows Credential Manager 密码和 keyboard-interactive/MFA；
- 从 `~/.ssh/config` 解析别名、用户、端口、IdentityFile 和单级 ProxyJump；
- 连接阶段为 Resolve、Connect、Authenticate、OpenShell、Ready/Failed；
- 相同 `user@host:port` 的新 tab 可复用已认证连接；
- SOCKS5/HTTP CONNECT 和系统代理位于统一网络设置；
- 特殊转发/显式命令仍可走兼容的系统 SSH 路径。

ztermy：

- 用 `libssh2` + 自有 Windows TCP 抽象实现连接；DNS、TCP、认证、打开终端都可取消并有独立超时；
- 支持密码、私钥、Windows OpenSSH Agent、ProxyJump/host chain、显式代理、keepalive、重连、启动命令、环境请求和终端类型；
- 保存 profile 是一等产品对象，连接进度、失败恢复和每主机高级设置更贴近运维工具；
- 当前没有把同一主机多个 terminal/SFTP/telemetry 全部复用为共享 SSH transport。

Nebula 的“认证连接复用”是值得单独评估的技术点，但不能直接套进 ztermy：ztermy 当前强调每个 session/worker 的明确所有权，libssh2 session 的线程归属、辅助 channel、公平轮询和单点故障传播都必须先重新设计。

### 6.2 SFTP

两者都支持目录浏览、上传、下载、递归操作、取消、进度和错误展示。

Nebula 的优势：

- SFTP 是 Files 抽屉的一部分，本地、WSL、SSH 的文件浏览在同一信息架构中；
- 复用已认证 SSH 连接；
- 远端文件可直接进入 Markdown/JSON/图片查看工作流；
- 分段下载和请求窗口有明确上限。

ztermy 的优势：

- 暂停/续传、失败后恢复、应用重启后的 interrupted 状态、原子完成文件、冲突策略、批量与递归 job graph 更体系化；
- 传输中心、聚合操作、Explorer drag-out、远端虚拟文件拖出和多选批处理已有独立合同；
- 对 100,000 条计划项、深度、路径长度、符号链接和并发数有显式上限；
- SFTP 文件名 UTF-8/GB18030 转换适合现实中的旧服务器。

如果把 SFTP 当“顺手打开远端文件”，Nebula 的整合感更好；如果把它当“可靠搬运大量服务器文件”，ztermy 的恢复和队列模型更完整。

### 6.3 ztermy 明显领先的运维能力

- **远程资源监控**：CPU、内存、磁盘、网络、进程和 SSH latency，带历史曲线与详情面板；Nebula 源码中未发现同类 `/proc` 采样器。
- **原生端口转发**：local、remote、dynamic SOCKS 转发是持久规则和独立 job；Nebula 只在兼容系统 SSH 命令中提到 forwarding，没有同等级原生管理面。
- **主机工作台**：profile 分组、搜索、最近连接、凭据、代理、脚本、日志与端口转发围绕服务器组织；Nebula 的主入口仍是 terminal tab。
- **编码兼容**：终端与 SFTP 文件名的 GB18030 路径是面向老旧中文环境的实际差异化能力。

## 7. 会话、窗口与工作区连续性

### 7.1 Nebula

Nebula 有两个不同层次的“连续性”：

1. `keep_session` 开启时，关窗只隐藏/分离 UI，PTY 仍在同一 resident 进程中；第二次启动通过 loopback Runtime/mux 请求恢复窗口。
2. resident 进程已经消失时，`session.json` 每秒自动保存 tab、split tree、cwd、SSH 启动描述和部分 AI CLI session id；下次冷启动重建 pane、重连 SSH，并可重新输入外部 Agent 的 resume 命令。

其优点是快照格式同时可作为可导入/导出的 workspace 文件，并包含异常退出识别和连续三次恢复失败后的隔离机制。

但源码与 README 有一个重要差异：README 把“关窗不杀会话”作为主卖点，`nebula_settings` 当前默认却是 `keep_session=false`、`restore_session=true`、`resume_ai=true`、`tray=true`。因此默认可靠承诺是“冷恢复布局/重连”，实时 PTY 常驻是可配置能力，而不是默认必然行为。

### 7.2 ztermy

ztermy 也有两个层次：

1. 用户显式开启“关闭到托盘”时，窗口隐藏但应用进程与现有终端保持运行；默认值是关闭。
2. 真正退出/崩溃后，版本化工作区保存 split tree 和 local/SSH restore intent；本地终端重建，保存的 SSH profile 重新连接，不声称远端进程仍活着。

两者的冷恢复已经接近同一类别，Nebula 更进一步保存自定义名称、颜色、窗口状态、可导出 workspace 和外部 AI CLI resume identity；ztermy 更保守地只恢复自身能负责的终端意图。

建议：ztermy 可借鉴“工作区导入/导出”和“恢复崩溃循环隔离”，但无需恢复外部 Agent，也不应把 SSH 重连描述成会话仍存活。

## 8. AI：两条完全不同的路线

### 8.1 Nebula 的主路线是 AI CLI 宿主

Nebula 会识别 Claude、Codex、OpenCode、Pi、Kimi、Gemini、Copilot 等前台程序，通过以下方式建立体验：

- 修改或包装这些 CLI 的 hook/notify 配置；
- 用 `nebula-hook.exe` 经命名管道返回回合开始、完成、等待输入和失败状态；
- 在 tab/sidebar/tray 显示品牌、spinner、dot 和注意力状态；
- 保存部分 CLI session id，恢复或 fork 会话；
- Runtime API 可启动、等待、读取、提示、分叉 Git worktree、委派 Agent；
- SSH 下另有随机 token 的私有 OSC bridge 把远端 hook 事件映射回本地 pane。

这是一套“管理终端里的 Agent 进程”的系统，不是把模型能力直接嵌入终端。

### 8.2 Nebula 的内置供应商助手很窄

Nebula 确实有 provider 设置，预置 OpenAI、Anthropic、Google、Ollama、OpenRouter、Qwen、DeepSeek、Kimi、Zhipu、Doubao、MiMo、Azure OpenAI 和自定义兼容端点。

但当前 `ai_assistant.rs` 的核心产品能力是“失败命令修复建议”：命令非零退出后，发送命令、退出码、cwd、Git branch 和最多 2,000 字符的输出尾部，请模型返回一条修正命令；用户快捷键只插入，不自动执行。它不是完整的多轮聊天侧栏，也没有 ztermy 当前的工具循环和对话历史模型。

### 8.3 ztermy 的主路线是自有内置 Agent

ztermy 的助手运行在应用侧栏，具备：

- OpenAI Responses、OpenAI-compatible、Anthropic Messages、Ollama，以及 ChatGPT 订阅登录；
- 模型目录获取、reasoning summary/effort、流式 Markdown、思考内容和 usage；
- 多轮对话、历史、新建会话、手动压缩和调试 trace；
- 默认不无差别附加终端输出，用户选择的区域和 AI 自己调用产生的工具结果进入后续上下文；
- read-only/ask/automatic/yolo 等权限模式与可持久允许规则；
- 当前 tab 内的终端、SSH、SFTP、监控、脚本、笔记和转发工具；
- 命令跟踪、真实退出码、等待/中断、输出完整性标记和超时语义。

这一路线在“无需安装另一个 CLI，打开 SSH 就能问”上明显更适合 ztermy 用户。

### 8.4 应如何理解差异

Nebula 的 AI sidebar 更像“Agent 任务雷达”，ztermy 的 AI sidebar 更像“终端副驾驶”。前者在多 Agent 生命周期、通知和 worktree 编排上领先；后者在供应商对话、上下文、工具使用和 SSH 运维语义上更深入。

根据 ztermy 的永久产品边界，Claude Code、Codex、OpenCode 等只能作为 UX 研究对象，不能集成、检测、启动或托管。因此 Nebula 最大的一组 AI 功能不是 ztermy 的待办项。

## 9. UI/UX 差异

### 9.1 Nebula 的优势

- 视觉中心始终是 terminal/pane，导航更轻，适合长时间停留；
- sidebar tab 和 top tab 两种形态、分屏 header、拖拽 docking、tab rename/color 构成完整工作区；
- AI 活动状态在 tab、sidebar、tray 和 toast 之间统一；
- Files/Git/SFTP drawer 跟随当前 pane 的 cwd，减少上下文切换；
- command palette、quick jump、completion popup 和 quick terminal 形成统一键盘工作流；
- 设置作为普通可复用 tab，与终端处于同一窗口模型。

### 9.2 ztermy 的优势

- 工作台先展示主机和分组，更适合“我要去哪台服务器”而非“我要开什么 shell”；
- 连接过程、主机监控、SFTP、传输、端口转发和脚本都以远端目标为中心；
- Windows 原生标题栏、Snap Layout、强调色、材质和品牌资产一致性更明确；
- 终端工具栏直接暴露运维操作，不需要通过大型 command palette 才能发现；
- 设置密度和词汇已针对个人极客用户调整，而不是通用开发环境。

### 9.3 双方共同问题

- 两边都有大而复杂的 UI 文件，容易在修一个焦点/悬浮/滚动问题时影响其他区域；
- 动效、透明材质、滚动跟随和弹层焦点都需要真实低配设备与窗口级验收，框架本身不会自动保证流畅；
- 两边都在自定义标题栏、IME、DPI、终端 selection 和 pane 生命周期上承担平台复杂度。

## 10. 渲染与性能

### 10.1 Nebula

当前默认 GPUI 路径从终端网格生成 `RenderSnapshot`，在一次锁内取纯数据，锁外解析颜色并用 GPUI quad/text 绘制。它刻意逐 cell 塑形，避免排版引擎改变终端网格位置；CJK、Powerline、box drawing 和 IME 是迁移闸门。

Nebula 自有路线图记录了一次 50 MiB 混合负载：旧壳约 2.4 MiB/s、GPUI 约 2.3 MiB/s，GPUI 内存约 85 MiB、旧壳约 118 MiB。但这是上游自报、旧版本时点和不同负载，不能与 ztermy 的数字横向比较。

它的风险是：GPUI 与组件来自固定 fork，Windows mixed-DPI、输入拖选和 Markdown/数学等功能还依赖项目补丁；没有 GPU 的云桌面/虚拟机体验值得单独验证。

### 10.2 ztermy

ztermy 当前终端仍主要走 CPU 栅格化 + scene graph texture。其性能专项已证明：

- 分离光标节点消除了固定负载中的 idle 全纹理上传；
- 8 ms 最新快照投递减少约 90.3% GUI snapshot 更新和约 36.7% 估算上传量；
- 惰性保留终端工作台减少约 29.8% 启动 QML 对象；
- 活跃输出阶段 paint P95 约 4 ms，但高吞吐时仍有大量全表面纹理上传；
- 在现有桌面上，Acrylic/Mica/透明/真不透明没有稳定的跨指标优胜者；低配物理机仍是开放验证轴。

### 10.3 判断

Nebula 的默认绘制架构在长期上更接近“直接 retained GPU terminal”；ztermy 的路径更保守，当前正确性和 Qt 公共 API 边界更稳，但全帧纹理路径存在明确上限。

这不意味着应迁移到 GPUI 或重写 UI。ztermy 已经建立数据驱动性能程序，下一步仍应在低配物理机上测量 active damage、GPU/软件渲染和复杂 QML 页面，再决定是否进入 glyph atlas/QSG geometry/QRhi 原型。

Nebula 附带新版 `OpenConsole.exe`/`conpty.dll` 并声称系统内置 ConPTY host 在其负载下可能慢 3～8 倍。这个点值得 ztermy做独立 A/B 探针，但必须先验证许可证、side-by-side 部署、系统版本兼容和真实输入/resize 收益，不能按上游结论直接引入额外二进制。

## 11. 配置、发布与维护

### 11.1 Nebula

- ZIP 中除 `nebula.exe` 外还要求 `runtime/nebula-hook.exe`、`runtime/conpty.dll`、`runtime/OpenConsole.exe`、字体、文档和许可证目录；
- Inno Setup 生成 per-user 安装器，可安装字体和配置登录启动；
- 应用内更新会读取 GitHub Release、下载匹配架构的 setup EXE，并校验长度、PE header 和 SHA-256；
- 配置面较分散：Lua、旧 TOML、文本设置、provider JSON、profile JSON、session JSON、history JSONL 等并存；
- 默认产品壳已经切到 GPUI，但 legacy shell 仍保留为显式 feature。

当前文档存在快速迭代痕迹：仓库已是 1.5.0，README 的推荐下载段仍写 `NebulaTerminal-v0.6.0-windows-x64.zip`；“关窗会话不死”的宣传也没有说明 `keep_session` 默认关闭。使用它作为参考时，应以源码和实际运行验收覆盖 README。

### 11.2 ztermy

- 支持静态 portable、静态 per-user MSI、动态 portable 和动态 MSI；
- 静态包主程序不依赖 Qt/OpenSSL DLL，动态包显式部署 Qt/QML/plugin/OpenSSL 闭包；
- installed 与 portable 数据位置、凭据位置、包内容和 MSI 结构均有自动合同；
- 当前没有正式的应用内自动更新链，GitHub Actions 发布路径也已回退；
- 版本化 JSON + last-known-good backup 比 Nebula 多存储源并存更容易审计，但迁移编号仍需严格遵守项目 skill。

Nebula 的“应用内检查、后台下载、校验后再启动安装器”值得在 Z 系列安装器成熟后借鉴；现在先做会造成发布链与安装器重复建设。

## 12. 许可证与参考边界

Nebula 整体为 GPL-3.0-or-later，并明确包含 Alacritty 派生终端代码、GPUI/gpui-component fork、vendored winit 和其他第三方内容。

在 ztermy 尚未选择许可证、且不准备接受 GPL 传染性约束的前提下：

- 可以学习产品结构、公开协议、行为合同、测试方法和交互结果；
- 可以重新独立设计“tab overflow”“broadcast input”“runtime snapshot”等概念；
- 不应复制 Nebula 的 Rust 源码、算法表达、图标、品牌、主题、README 图片或补丁；
- 若未来确实考虑依赖其 crate 或复用代码，必须先做单独许可证决策，不能作为普通实现细节混入。

本报告中的源码路径用于审计证据，不代表批准复用。

## 13. 详细能力矩阵

“领先”描述的是当前产品完成度，不代表 ztermy 必须实现。

| 能力 | Nebula | ztermy | 判断 |
|---|---|---|---|
| 本地 Shell 检测 | PowerShell/CMD/Git Bash/Nushell/WSL | PowerShell/CMD/Git Bash 等 Windows catalog | Nebula 更广，尤其 WSL/Nushell |
| 原生 SSH | `russh` | `libssh2` | 均有，架构不同 |
| SSH 配置导入 | 深度读取 `~/.ssh/config` | profile 为主，支持高级连接参数 | Nebula 更贴 OpenSSH 生态 |
| SSH 连接阶段 | 5 个真实阶段 | DNS/TCP/认证/终端等真实阶段 | 接近 |
| 已认证连接复用 | 有 | 无共享 transport pool | Nebula 领先，但迁移风险高 |
| Keepalive/重连 | 有相关能力 | 显式、可观察、profile 化 | ztermy 更产品化 |
| ProxyJump/代理 | 单级 jump + system/manual proxy | host chain、显式代理、系统代理边界 | ztermy 更偏运维配置 |
| 原生端口转发 | 未发现同级管理面 | local/remote/dynamic SOCKS | ztermy 明显领先 |
| SFTP 浏览 | Files 抽屉统一体验 | 独立 SFTP 面板/树 | Nebula 整合感更好 |
| 传输恢复 | 有取消、分段与事务 staging | 暂停/续传/恢复/批量/冲突/job graph | ztermy 更完整 |
| 远程监控 | 未发现 | CPU/内存/磁盘/网络/进程/latency | ztermy 独有优势 |
| 终端分屏 | 拖拽、detach、zoom、header | 持久 split tree、键盘 focus/resize | Nebula 交互更成熟 |
| 广播输入 | tab 内按接收 pane 模式重编码 | 无 | Nebula 领先 |
| Tab 过载 | 分页、自动滚动、拖拽 | 顶部 tab 与滚动 | Nebula 领先 |
| 快捷终端 | Quake-style | 无 | Nebula 领先 |
| 会话常驻 | opt-in keep_session | opt-in close-to-tray | 接近，默认都需核对 |
| 冷恢复 | tab/split/cwd/SSH/Agent identity | tab/split/local/SSH restore intent | Nebula 保存信息更多；ztermy 语义更克制 |
| Workspace 导入导出 | 有 | 无完整用户文件工作流 | Nebula 领先 |
| 持久命令历史 | JSONL + completion | 会话/远端历史、命令/脚本体系 | Nebula 补全更顺；ztermy 管理更强 |
| 命令补全 | ghost text + popup | 无同级统一补全 | Nebula 明显领先 |
| Files/Git | 本地/远端文件 + Git stage/commit/pull/push | SFTP/笔记/脚本，无通用 Git drawer | Nebula 领先，但可能超出 ztermy 定位 |
| Markdown/TeX | 文档 tab + GFM + native math | AI 消息与本地笔记 Markdown | Nebula 文档能力更广 |
| 内联图片 | OSC 1337 | 未形成同级能力 | Nebula 领先 |
| 外部 AI CLI | 检测、hook、恢复、fork、delegate | 永久排除 | 路线差异，不是缺陷 |
| 内置模型对话 | 失败命令修复建议为主 | 完整多轮侧栏 Agent | ztermy 明显领先 |
| Provider 广度 | 13 类 preset | 主要协议族 + ChatGPT 订阅登录 | Nebula preset 更多；ztermy 协议深度/账户登录更强 |
| AI 工具 | Runtime 操作外部 Agent/pane | 当前 tab 的终端/SSH/SFTP/监控/脚本/笔记 | 服务对象不同 |
| Runtime API | 版本化 JSONL，window/tab/pane/agent | 无通用外部控制面 | Nebula 领先，但不是当前必要项 |
| 托盘通知 | AI attention 深度集成 | 关闭到托盘、基础菜单 | Nebula 更丰富 |
| 应用内更新 | 下载并验证 installer | 无正式链路 | Nebula 领先 |
| 静态单体分发 | 发布包含 helper/runtime/font | 可提供静态单 EXE 主体 | ztermy 更简洁 |
| 低性能模式 | 依赖 GPUI/GPU，旧壳可回退构建 | 用户可选低性能模式/WARP 路径已测 | ztermy 产品入口更明确 |

## 14. 对 ztermy 的建议

### 14.1 第一优先级：直接提升终端日用效率

1. **命令补全专题**

   在不读取无关终端内容的前提下，基于 ztermy 已有命令历史、cwd、远端辅助 channel 和脚本索引提供 ghost text/popup。先做本地和可靠 shell integration，再扩展 SSH；候选来源必须可见、可关闭、可测延迟。

2. **Tab/Panes 交互补完**

   增加 tab overflow 导航、边缘自动滚动、拖动反馈；评估 pane header、拖拽分屏和 detach。不要先改布局模型，当前持久 split tree 已足够作为后端。

3. **快捷终端**

   调研一个进程级单例的下拉终端，复用现有 LocalTerminalSession 和托盘生命周期。它对日常使用价值高，且不改变 SSH 产品定位。

4. **工作区导入/导出**

   把现有版本化 workspace state 暴露为用户可审阅文件；导入前验证 schema、profile 引用和最大 pane 数，失败不覆盖当前工作区。

### 14.2 第二优先级：有价值，但必须先做架构验证

1. **认证 SSH 连接复用**

   先写 ADR 和压力测试，回答 libssh2 session 的单线程 owner、多个 channel 的公平性、断线传播、host key/credential identity、SFTP 和 telemetry 共存。没有这些答案前不要为了“第二个 tab 更快”改造。

2. **side-by-side OpenConsole A/B**

   只做可移除的性能探针，在同一台低配机对比启动、resize、吞吐、IME 和包体；收益不明确就不增加发布依赖。

3. **Pane broadcast**

   对多主机运维有价值，但风险也高。应默认关闭、有明显状态、区分文本与控制键、按每个 pane 当前终端模式重新编码，并提供目标预览。

4. **恢复循环隔离**

   在 last-known-good backup 之外增加“连续恢复启动失败后隔离 workspace”的机制，避免坏布局或特定 profile 形成启动崩溃循环。

### 14.3 延后到安装器/发布链成熟后

- 应用内更新检查、后台下载、哈希/PE 验证和启动安装器；
- 自动下载动态依赖或按架构选择包；
- 更完整的 update reminder/skip version UX。

### 14.4 不建议跟进

- 外部 Codex/Claude/OpenCode/Pi Agent 检测、hook 修改、resume/fork/delegate；
- 为追赶 Nebula 而加入通用 Git 客户端、图片查看器、TeX 阅读器；
- 在没有明确外部自动化消费者前建设全量 Runtime 控制 API；
- 用 Lua + 多套散落存储替换现有版本化配置；
- 因为 Nebula 使用 GPUI 就重写 Qt Quick UI；
- 直接引用 Nebula GPL 源码、fork 补丁、图标、主题或品牌资源。

## 15. 建议的竞争定位

ztermy 不需要成为“另一个 Nebula”。更合理的对外/内部目标是：

> Nebula 擅长让本地开发会话和外部 AI CLI 持续运行；ztermy 擅长让远程主机的连接、监控、传输、转发、脚本和内置 AI 协作形成一个可靠闭环。

接下来真正需要补的是“作为终端是否足够顺手”，而不是“是否拥有 Nebula 的每一个外围面板”。命令补全、tab/pane 操作、快捷终端、工作区导入导出可以显著缩小日用体验差距；远程监控、端口转发、恢复型 SFTP、编码兼容和自有 AI 则应继续作为 ztermy 的核心护城河。

## 16. 最终判断

Nebula 当前的功能广度和终端工作区完成度高于 ztermy，尤其在本地/WSL、分屏、补全、Files/Git、快捷终端和外部 AI CLI 编排方面。它已经接近一个“小型终端 IDE”。

ztermy 并没有因此“目标跑偏”。相反，源码对比证明它已经在另一条轴上形成更深能力：服务器监控、端口转发、可靠传输、主机配置、旧编码兼容和内置供应商 Agent。真正的风险不是功能少，而是继续把大量功能堆进 `AppController` 和超大 QML 文件，同时忽略终端基础交互的最后一公里。

推荐结论：吸收 Nebula 的终端效率设计，不吸收它的产品边界；用 ztermy 自己的架构和测试重新实现少数高价值行为，而不是进行框架、终端核心或 AI 路线迁移。

## 附录 A：关键源码证据索引

Nebula：

- `D:\tmp\nebula\Cargo.toml`：workspace、release profile、GPUI fork 与 winit patch
- `D:\tmp\nebula\nebula_app\Cargo.toml`：产品依赖、默认 GPUI 壳、russh、Tokio、平台 API
- `D:\tmp\nebula\nebula_app\src\gpui_shell\terminal\element.rs`：终端快照与 GPUI 绘制
- `D:\tmp\nebula\nebula_app\src\mux.rs`：单实例与 attach 协议
- `D:\tmp\nebula\nebula_app\src\session.rs`：冷恢复、split tree、workspace 文件与 crash-loop breaker
- `D:\tmp\nebula\nebula_app\src\ssh_session.rs`：原生 SSH 阶段、认证与连接池
- `D:\tmp\nebula\nebula_app\src\ssh_sftp\`：SFTP 事务、分段传输和资源上限
- `D:\tmp\nebula\nebula_app\src\ai_hook.rs`：外部 AI CLI hook 与命名管道
- `D:\tmp\nebula\nebula_app\src\ai_assistant.rs`：失败命令修复助手
- `D:\tmp\nebula\nebula_app\src\runtime_api.rs`、`docs/runtime-control-api.md`：控制面
- `D:\tmp\nebula\nebula_settings\src\lib.rs`：`keep_session`、`restore_session`、tray 默认值
- `D:\tmp\nebula\scripts\package-release.ps1`、`scripts/build-installer.ps1`：发布与安装器

ztermy：

- `docs/ARCHITECTURE.md`：分层、终端数据流和线程合同
- `docs/PERFORMANCE_PROGRAM.md`、`docs/testing/PERFORMANCE_OPTIMIZATION_RESULTS.md`：性能方法与测量结果
- `src/domain/terminal/GhosttyTerminalEngine.*`：终端核心边界
- `src/ui/terminal/TerminalItem.*`：Qt Quick 终端绘制与输入
- `src/application/ssh/SshTerminalSession.*`：SSH worker、辅助 channel 与监控
- `src/application/sftp/TransferManager.*`、`src/domain/sftp/TransferBatch.*`：传输恢复和批处理
- `src/application/forwarding/PortForwardingJob.*`：原生端口转发
- `src/domain/workbench/WorkspaceState.*`：split tree 与 restore intent
- `src/application/ai/`、`src/infrastructure/ai/`：内置 Agent、provider、上下文和工具
- `src/platform/windows/NativeWindow.*`：标题栏、DWM、托盘和 Windows 行为
- `docs/testing/DISTRIBUTION.md`：静态/动态 portable 与 MSI 合同
