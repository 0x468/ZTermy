# 内存专题：测量、AI 布局根因修复与重复验证

日期：2026-09-21。工作树起点：`0e9ce6c3f530ab6e7a04616d77b993330723df0d`。

**第二阶段已定位并修复合成三轮 AI 负载中的布局反馈问题。** 消息重建时的零宽度富文本产生异常高度，推动 ListView 反复创建 delegate 和 QTextDocument；GUI 忙于布局，销毁无法及时完成。修复明确传递宽度，以 Column 排列已知宽度的块，并在加载后更新位置。完整交互复跑通过；重复 A/B 和同源动态/静态 Release 数据见文末第二阶段章节。未提交或合并。

以下第一阶段记录保留其当时的结论与限制；“尚未修复”等表述是历史状态，以文末第二阶段结果为准。本文不把合成负载通过等同于真实 Provider、附件、长时会话或用户原始崩溃全部验收通过。

## 判定原则

目标是解释成本、峰值和释放行为，保留终端输入及渲染响应。任务管理器的单次截图、进程结束后归零、单次减少 Working Set，都不是修复泄漏的证据。第一轮不改产品缓存策略，不执行 GC、EmptyWorkingSet、工作集裁剪或冻结后台 Pane。

“三轮 AI 后约 1 GB 并卡死/崩溃”是待验证的用户报告，目前没有对应 dump、输入形态及 provider 信息。合成实验不需要等待 dump；但不能把一个合成 Markdown 场景通过等同于该问题已经消失。

分类以证据为准：

| 类别 | 所需证据 |
|---|---|
| 必要常驻 | 对象具有明确存活所有者，启动后平台/渲染/应用工作的最低成本 |
| 合理缓存 | 有界，重复负载后平台期，确有重用收益，生命周期可解释 |
| 可释放但未释放 | 隐藏/关闭之后明确对象仍存活；区分应用保留与 allocator 保留空闲块 |
| 重复持有 | 同一内容在模型、序列化、布局、纹理等处同时存在；确认是否必需 |
| 真实泄漏 | 重复相同周期后 live allocation/object 持续增长，能指向分配栈和丢失的释放链 |

## 指标口径与预算模型

采样脚本 `scripts/measure_process_memory.ps1` 使用 PowerShell 7、.NET Process、PSAPI、VirtualQueryEx、GetGuiResources、GPU WMI 性能计数器。无第三方运行依赖。`scripts/summarize_memory.py` 只使用 Python 标准库。

- Working Set：当前驻留物理页，含可共享页，不代表本进程独占。
- Private Bytes / Commit Size：PSAPI 的 PrivateUsage / PagefileUsage。当前 Windows 上是同一私有提交口径，**不能相加**，不是实际 pagefile 文件占用。[Microsoft 定义](https://learn.microsoft.com/en-us/windows/win32/api/psapi/ns-psapi-process_memory_counters_ex)
- VA reserve：VirtualQueryEx 中 `MEM_RESERVE` 总和；不包含已提交区。VA commit 按 PRIVATE / IMAGE / MAPPED 分列；不能直接与 Private Bytes 相加。大块地址保留不是数 GB 物理 RAM。
- GPU dedicated/shared：按本次 PID 聚合 GPUProcessMemory 实例；不是纹理数量，也不是全部 DWM 占用。不把 shared GPU 内存再加到进程提交得出“总内存”。
- GUI 指标是 USER/GDI 对象数；QML QObject / QQuickItem 另由测量入口统计。对象数量不能直接乘一个常数推导准确内存。
- CPU 列是单核百分比，20 逻辑处理器机器上 100% 等于占满一个逻辑核，不等于任务管理器的整机 100%。

预算工作模型：

```
进程私有提交 ≈ Qt/QML/JS/应用常驻 + allocator 保留空闲块
             + 每会话(终端引擎/快照/观察器/队列/线程栈提交)
             + CPU 图像/文本布局 + AI 模型/请求/附件/流解析
             + 驱动/Qt 分配中计入本进程的部分
```

DLL/EXE 映像共享、VA 保留、GPU/DWM 是并列观测维度，不强行做一个可加总的圆饼图。组件的精确 MiB 归因在堆栈/单变量实验前保持未知。

## 所有权链和候选成本（只读检查）

| 组件 | 当前源码与所有者 | 预期释放/边界 | 本轮判断 |
|---|---|---|---|
| Qt Quick/QML | `main.cpp` → NativeWindow → QQmlEngine/root object → 页面树 | window/engine 销毁；隐藏不等于销毁 | 原 UI 基准启动约 15,426 QObject、7,433 QuickItem，常驻 UI 有值得归因的成本 |
| D3D/RHI | QQuickWindow scene graph / Qt RHI / D3D11 device、swapchain、驱动 | 渲染线程延迟销毁、窗口资源释放；driver 可缓存 | 不能把 Private Bytes 的全部差额归因给 GPU |
| 字体/纹理/图标 | FontCatalog、Qt 字体回退/布局、glyph atlas、SvgIconImageProvider | 多数是 engine/process/renderer 生命周期缓存 | 需 WPR/堆快照与字符集单变量实验；当前无逐字体/逐纹理字节统计 |
| TerminalItem | `TerminalTextureNode` 持有 `QImage image`、主 texture、cursor texture；viewport 持有 shared snapshot | scene graph node owns texture；尺寸不变重用 image | CPU image 是渲染 backing store，有用途；不是看到 CPU/GPU 两份就删除一份 |
| 终端像素预算 | ARGB32 backing store | 约 `4 × 物理宽 × 物理高`；DPR 平方增长 | 1120×800 完整表面在 DPR=1 约 3.42 MiB/份，DPR=2 约 13.67 MiB/份；真实 viewport 小于窗口，驱动对齐/暂存另算 |
| Ghostty | GhosttyTerminalEngine::Impl 持有 terminal/renderState/row iterator 等 | Impl destructor 成对 free；scrollback 用库默认策略 | 当前包装调用 `ghostty_terminal_new`，没有应用级 scrollback 上限参数，不能虚构“已限制为 N 行” |
| 终端快照 | LocalTerminalSession pending snapshot → controller/session → TerminalItem | shared_ptr；latest-frame coalescing，旧快照随最后引用释放 | 要测 simultaneous live snapshots，而非累计 produced 总数 |
| ConPTY | LocalTerminalSession 拥有 ConPtyProcess、engine、读/写/退出/停止线程 | requestStop → stopFinished → session 析构 | shell/conhost 是子进程，不计入父进程 Private Bytes；输入队列有 1 MiB/4096 event 上限 |
| SSH/SFTP | TerminalSessionState 的 unique_ptr SSH/SFTP/model，异步工作线程、transfer manager | requestStop/deferred stop/reaper | 未连接真实 SSH；不读取用户凭据自动跑远端实验 |
| Pane/Tab | AppController 的 session 容器 + workspace 布局；每 Pane 独立 backend | close → retireTerminalTab → m_closingTabs → 16ms reaper，等 backend stopFinished 再 erase | 关闭动作发生不代表立即释放；需记录 stop 和尾窗口 |
| AI 展示模型 | TerminalSessionState unique_ptr AiConversationModel | clear / session retirement | 默认 64 messages、单文本 256 KiB、conversation 20 MiB；是内容账本限额，不是进程堆上限 |
| AI 富文本 | AiAssistantPane ListView，cacheBuffer=max(height,800)；MarkdownMessage 同时有 streaming TextEdit 和已完成 block 树 | delegate 生命周期、布局缓存、scene graph | hidden TextEdit/完成后文本重复持有是候选；尚未证明主因 |
| AI Provider | AiTurnRunner → ProviderHttpClient requests map → RequestState → SSE/NDJSON parser、mapper、reply | release erase state + reply deleteLater；cancel/error 亦需覆盖 | `readAll()` 的瞬时 QByteArray 在 parser 限制之前分配；parser 上限不限制网络缓冲瞬时峰值 |
| AI parser | SSE 单行64 KiB、event1 MiB、buffer2 MiB；NDJSON line1 MiB、buffer2 MiB | 完成/错误清理 request state | 不能由 parser 有界推出 provider mapper/工具历史/重试请求均已计入同一总预算 |
| AI 附件/上下文 | session attachments、model images、provider messages/JSON、replay/tool history | 发送/clear/cancel/关闭各自有不同生命周期 | 原图、base64、JSON、QString、布局/缩略图的并存需要单独压力场景 |
| 命令输出语义观察 | SemanticTerminalObserver → CommandBlockStore；AI frame tracker 保持当前屏及 changed lines | session 生命周期及有界淘汰 | block 默认64，显示输出64 KiB/block，artifact 总8 MiB；可能与 Ghostty scrollback 合理重复 |

上述是当前代码的所有权审计，不是分配栈证明。尤其 UI 隐藏保留、allocator 高水位、驱动缓存应与真实泄漏分开。

## 测量矩阵

每个单元 dynamic Release 和 static Release 分开，3 次最低、候选差异再做交错 A/B/A/B（至少各5次）。固定 1120×800 logical、实际 DPR=1、dark/en、D3D11、同显示器、同电源计划。窗口不遮盖、不最小化，桌面保持解锁。首次观察启动单列；热启动三轮不冒称冷启动。冷启动应在可安排重启时另跑，不用清系统缓存模拟。

上面是完整矩阵的控制方案。**本轮AI阶段入口已固定dark/en；普通空载组使用出厂默认system主题/system语言，实际主题颜色未单独记录，不与dark/en组直接作组件差分。** 采样窗口DPI为96。后续普通启动组也应先用合成settings模板固定主题/语言，并记录模板哈希；目前不能把其余未跑单元标为完成。

| 场景 | 操作、阶段与判据 | 第一轮/后续 |
|---|---|---|
| 全新空载 | fresh data-dir，30s；启动峰值、末5s中位数 | 首轮 |
| 本地终端 idle | 等 running，再10s；与 empty 同进程差值 | 阶段入口 |
| 持续输出 | 现有 performance-benchmark 20,000 lines；另扩展 10min steady rate/突发/宽行/Unicode | 现有入口可复用；长时后续 |
| scrollback | 达默认上限平台期，滚屏、搜索、resize；库级限额只在实验分支做A/B | 后续 |
| 2/4/8 Pane | 固定总窗口面积和字体；分别同时输出、空闲、隐藏、关闭 | 后续 |
| Tab 生命周期 | 8 Tabs轮换20次，逐个关，最后等待30/60s | 后续 |
| SSH/SFTP | 隔离测试服务器/合成目录，0/1连接，目录10/1k/10k项，上传下载、取消、断连 | 后续，需固定测试端点 |
| 材质 | acrylic/mica/micaAlt/transparent/opaque，其他相同；DWM全局代价单列 | 已有环境开关，后续 |
| AI UI | 未打开/空面板/同会话3轮240 chunks/隐藏/clear/close | 优先首轮，合成内容不落盘 |
| AI Provider | loopback SSE/NDJSON，三轮与30轮；慢流/突发/缺终止/错误/取消/重试 | 高优先级下一步；UI直注模型不能替代 |
| AI 内容类型 | plain/Markdown长列表/代码块/表格/reasoning/tool replay/附件0,1,4 | 高优先级下一步 |

稳定值使用阶段末5s样本中位数，并报告每轮范围、跨轮标准差。10s无增长只是短窗口平台，不代表长时间稳定。释放时延从 clear/close 的 UTC 标记到对象数与内存回落观测，采样分辨率约250ms；WMI慢采样区间更粗。

## 工具盘点与开销

已安装：WPR `10.0.26100.9444`，WPA `11.7.383.39833`，Qt `6.8.3`，MSVC19.44，CMake4.4.0，Python3.13、PowerShell7。未找到 PATH 中的 VMMap/UMDH/GFlags。本轮没有安装工具。

已有仓库能力：`collect_performance_acceptance.ps1`、terminal/UI performance-benchmark、composition matrix、AI concurrency soak、terminal stability soak、QML lifecycle verification。

本轮 Win32 counters 每250ms；VA/GPU/进程后代约2s一次。WMI首查实测7.13s，导致短UI基准第一轮只采到1个点，**判为采样无效**。已改为启动前预热；后续慢查询约0.32–0.37s，仍会让相邻快样本间隔变大，CSV记录实际时间与 slowProbeMilliseconds，不伪称严格250ms。第一次短基准只用于发现候选，不进入正式前后对比。

后续原生归因优先 WPR/WPA：短15–30s VirtualAlloc/heap + CPU/D3D ETW，在只含合成内容的隔离测试中观察调用栈；高事件率 heap tracing 有明显开销，和无 tracing 基线分开。先 `wpr -status` 确认没有他人会话，再核对本机 `wpr -profiles`，不停止/取消未知 recording。ETW 可能包含进程命令行/路径，因此不能在用户真实终端/AI会话上直接套全系统收集。

需要 VMMap/UMDH 时仅取 Microsoft Sysinternals / Windows SDK 官方源，记录版本、签名、下载哈希、安装组件和采集成本。UMDH堆栈数据库/GFlags仅针对重命名的实验副本并恢复；不能给正常 `ztermy.exe` 全局打开开销。当前没有进行这些全局设置。

## 第一轮证据

原始数据根目录：`build/memory/`，每组有 environment.json、run-N/samples.csv、result.json、isolated data-dir 和应用原有汇总。汇总器生成 summary.json。二进制 SHA256 单独保存，不把同一个Git HEAD当作二进制完全相同的证据。

原始数据仅保留在本次工作树的 `build/memory/`，不提交生成的 ZIP、CSV、日志、截图或冻结二进制。仓库保留测量脚本、生命周期校验器、结论和复现命令，避免把可重新生成的实验产物长期纳入版本控制。

本机：Windows11 Pro 25H2 / build26200、i5-13600K（20逻辑核）、约64 GiB RAM、RTX4060Ti / driver32.0.16.1664、2560×1440。系统还有 Oray 虚拟显示驱动；没有因此推断它导致问题。

旧 dynamic Release（SHA256 `760ED98D511BE8BBB020A3466C7FFC8503B588CC2A07951A37D75FD2EF69B7A0`）：

| 空载30s，MiB | run1 | run2 | run3 |
|---|---:|---:|---:|
| Private Bytes末5s中位数 | 234.54 | 235.21 | 238.91 |
| Working Set末5s中位数 | 253.31 | 252.75 | 257.29 |
| GPU dedicated末5s中位数 | 39.67 | 38.79 | 39.67 |
| VA仅reserve末5s中位数 | 6341.87 | 6335.16 | 6330.43 |

该组中后段与隔离构建准备/编译可能重叠，保留为探索记录，CPU/延迟不作为最终验收基线。观察到的6.3GiB地址保留并非6.3GiB物理内存。三轮自然退出，已追踪的实验后代无残留。

旧UI基准240 chunks：有效采样的run2/run3 Private Bytes采样峰值约524.26 / 422.04 MiB；均完成且正常退出，心跳最大间隔68 / 149 ms。没有多轮、释放尾窗口，不宣称稳定值或泄漏。run1 WMI首次开销导致漏采，保留但排除统计。

### 同进程复现：AI第二轮失去响应

新测量构建 `build/memory/dynamic-stages-240` 已在连续运行中复现：第一轮240 chunks完成，第二轮开始后不再返回Qt事件循环，Private Bytes持续增长。前两轮均被外部180s watchdog终止，峰值分别1536.00 / 1589.59 MiB，不能把强制终止视为通过。run1在约115s时已超过1GiB且Responding=false。

这些输入直接进入模型，不经过网络Provider，不调用AI工具；因此已经建立一条与Provider无关的可重复故障路径，但不能据此排除其他Provider/附件问题。前两轮追踪的子进程均无残留。

本机另外发现已安装的CDB `10.0.22621.755`（Windows SDK Debuggers/x64）。run2约75s时非侵入采集线程栈并立即分离，保存 `run-2/hang-stacks.txt`，只输出栈、没有full dump。GUI线程路径：DirectWrite glyph bearings → QTextLine layout → QTextDocumentLayout → QQuickTextEdit implicit width → Loader/GridLayout → delegate incubation → ListView viewport → Flickable contentY → attached ScrollBar setSize/position 更新。私有函数只有近邻导出符号，不能把导出符号偏移误读成精确函数名。

Qt6.8.3官方源码确认 attached ScrollBar 的positionChanged会回写Flickable.contentY，setSize可能先钳制position；可变高度delegate估算变化能进入此反馈链。[Qt6.8.3源码](https://raw.githubusercontent.com/qt/qtdeclarative/v6.8.3/src/quicktemplates/qquickscrollbar.cpp)

run1栈采集撞上watchdog退出，GetContextState失败，只作为失败诊断尝试保留，不作为根因栈。run2栈采样造成短暂停顿，故该轮CPU/延迟有采集扰动标记；其他时间序列保留。

第三轮同样卡在第二轮，180s峰值1587.48 MiB。GPU dedicated三轮峰值56.32/47.11/49.48 MiB，shared峰值21.66/18.30/14.53 MiB；没有与1.5GiB私有提交同比增长。整体CPU中位数约占一逻辑核（95–98% single-core），符合持续GUI计算，而不是仅等待网络。

单变量B1：`-DetachScrollBar`，只在测量路径将附加滚动条断开。仍然在第二轮卡住并超时，Private Bytes峰值1583.95 MiB。GUI栈仍在Loader → TextEdit implicitWidth → 文本重排，调用入口改为Flickable fixup/viewport更新。**该对照否定了“只修自动滚动条即可解决”的假设；未实施滚动条产品改动。** 原始数据 `dynamic-detached-scrollbar`。

单变量B2：`-NoAiDelegates`，仅将ListView.model置空，C++模型仍接收同样三轮文本。用于区分模型和展示，不是可交付的优化，更不能把不显示AI消息的低内存当作产品成绩。原始数据 `dynamic-model-only`。

B2结果：完整通过三轮及clear/close，三轮完成末5s分别271.00/270.96/271.04MiB。整次峰值383.89MiB主要不在AI累积阶段。关闭工作台/Tab还会改变终端尺寸和当前页面，末尾349.64MiB不能直接解释成“模型没释放”；其对象数、GPU和跨循环堆栈还需细分。

单变量B3：将MarkdownMessage的prose TextEdit包在普通Item内，保持宽度、文本、Markdown和选择行为，避免Loader直接读取TextEdit隐式宽度。仍在第二轮卡住，180s峰值3241.77MiB，**候选无效且更差，已完整撤回**。原始数据 `dynamic-wrapper`，候选diff在证据中留档。当前工作树没有QML产品变更。

### 动态/静态普通空载复核

后续串行运行，没有本任务编译/其他实验并行。每组3次，每次30s，fresh data-dir，1120×800、DPI96、D3D11，出厂system主题/语言。括号为三轮末5s中位数的min–max；SD为跨轮样本标准差。

| 指标 | Dynamic Release | Static Release |
|---|---:|---:|
| Private Bytes，MiB | 233.23（232.52–233.71），SD0.60 | 234.43（230.31–235.31），SD2.67 |
| Working Set，MiB | 251.15（250.50–251.54） | 241.73（240.54–241.75） |
| GPU dedicated，MiB | 39.91（39.54–40.03） | 39.79（39.48–39.79） |
| GPU shared，MiB | 1.75（1.72–2.29） | 3.70（2.27–3.71） |
| 线程数 | 53 | 53–55 |
| Handle数 | 1653–1655 | 1585–1591 |

出处：`dynamic-idle-confirmation/summary.json`、`static-idle/summary.json`。不能仅凭static Working Set更小就宣称节省了同等私有内存；两组私有提交基本同量级。两份旧Release均缺少嵌入的精确Git构建证明，以文件哈希作为本轮二进制身份。

| 变体 | SHA256 |
|---|---|
| 原dynamic Release | `760ED98D511BE8BBB020A3466C7FFC8503B588CC2A07951A37D75FD2EF69B7A0` |
| 原static Release | `28904EAD126D2B293820E695B1482BE940EEEF9EA1FA007B75C01A9152EC0AE7` |
| AI阶段基线 | `CCC17B144141A5230D26C417719C7076CF15A67222E8C113C168D86EB4E7CF84` |
| 断开滚动条 | `B826F841C6A1A10B9F75812D39D01CAEDAF83EC9439CE512693CC38BE4BDB200` |
| 无消息delegate | `985BAFE99ED9AF3F3F2B03446DE80F9C2D0389F660C6A259B8BEB53A34EB3B90` |
| 宽度包装候选（已撤回） | `A4FF1BB4E6599D4D0ED8309C80FAC777E17E4D7769B8D016DBD55D2B56A47008` |

### 优化前后与置信度

| 对照 | 结果 | 能说明什么 | 不能说明什么 |
|---|---|---|---|
| 原展示3轮重复 | 3/3在第二轮挂起；1536–1589.59MiB/180s | 复现可靠，单轮基准漏掉跨消息问题 | 不能认定与用户原崩溃同一根因 |
| 去附加滚动条1次 | 同样失败 | 仅修滚动条不足 | 不能排除它参与复杂反馈 |
| 无delegate1次 | 三轮及clear/close完成，AI约271MiB | 当前合成输入的增长依赖展示层 | 不能作为产品节省值、也不能证明所有模型/Provider路径无泄漏 |
| 隔离implicitWidth1次 | 失败，更高峰值 | 该布局候选无效 | 不可保留为性能优化 |

产品优化节省的常驻、峰值、释放时延、CPU/GPU代价：**尚无可报告的已验证改善**。真实泄漏与布局循环中不断创建/延迟销毁对象目前未完全区分。下一步应在同一复现上统计delegate create/destroy及QTextDocument存活数，取得Qt匹配符号或target-only堆分配栈，建立最小QML复现，再尝试修复；不要继续凭栈里的单个函数名改行为。

## 可人工复现

在 PowerShell7、解锁的真实桌面运行；所有 OutputDirectory 必须不存在，脚本拒绝覆盖。

```powershell
pwsh -NoProfile -File scripts/measure_process_memory.ps1 -SelfTest
pwsh -NoProfile -File scripts/measure_process_memory.ps1 `
  -Executable <dynamic-release>/ztermy.exe -OutputDirectory build/memory/new-idle `
  -Scenario idle -Repetitions 3
pwsh -NoProfile -File scripts/measure_process_memory.ps1 `
  -Executable <instrumented-dynamic-release>/ztermy.exe -OutputDirectory build/memory/new-ai-stages `
  -Scenario ui -MemoryStages -Chunks 240 -Repetitions 3
python scripts/summarize_memory.py build/memory/new-ai-stages
```

失败复现预期：脚本最终以非零状态报告超时，CSV/JSON仍保存。B1/B2分别在相同命令追加 `-DetachScrollBar` / `-NoAiDelegates`；二者只在 `-MemoryStages` 下有效。这些是诊断实验，不是用户设置或修复开关。`-TimeoutSeconds`默认180，可明确延长；不要把超时/强制结束计为通过。

`ZTERMY_MEMORY_STAGES=1` 仅在已有 `--ui-performance-benchmark` 路径激活，不改变正常产品流程。自包含合成文本直接注入 AiConversationModel；不访问网络、不调用外部 Agent、不需要 API key。QML仅计数，不转储文本。关闭只针对实验Process对象；不按名称批量杀进程。对已观察后代按PID+创建时间检查，未知进程不碰。

本工作树通过 CMake `msvc-dynamic-release` preset 构建；Ghostty使用原锁定提交的本地副本（不写主仓库依赖），副本的CMake wrapper加 `-Dversion-string=1.3.2-dev`，避免tar源码读取外层ztermy tag产生版本panic。该标签来自依赖build.zig.zon。原主工作树预构建二进制与本次测量构建分组报告。

## 优化门槛与未完成项

目前没有基于上述探索峰值实施产品内存优化。第一候选：QML启动常驻页面、AI完成后两套文本表示、hidden workbench保留；其后是Provider突发读/mapper/replay/附件复制。CPU backing store和Ghostty不是默认“应该删”的对象。

每个候选需单变量A/B，报告常驻、峰值、回收时延、CPU、GPU、心跳、终端输出/滚动/输入正确性。差异小于跨轮离散程度则不接受。假设被证伪也记录。无堆快照时不能把未回落全部叫泄漏。

尚缺：同源dynamic/static完整矩阵、native heap分配栈、真实纹理数及字节、Provider端到端多轮/附件压力、长时间close/reopen斜率、SSH/SFTP、IME/DPI/输入延迟人工验收，以及用户原始崩溃dump。这是分阶段专题，首轮报告不等于全部性能验收通过。

## 最终检查与交付范围

- 最终diff只增加 `main.cpp` 的显式内存测量模式以及采集/汇总/自检脚本和本文；产品QML候选均已撤回，没有缓存裁剪、GC或冻结Pane。
- CMake `msvc-dynamic-release` 构建通过；`ai-conversation-model`、`provider-http-client`两项CTest通过。
- 修改后的 `main.cpp` clang-format检查、clang-tidy（warnings-as-errors）通过；`git diff --check`通过。
- Win32采集器 `-SelfTest`、`python -B scripts/test_memory_summary.py`通过。汇总测试覆盖MiB换算、缺失GPU值不变成零、时区等价。
- 三轮AI失败复现属于**被测试程序失败、诊断成功**；模型对照完整走完阶段。没有把它写成AI产品功能通过。
- 所有采集组的 `survivors` 均为空；只对本次创建的Process对象及其后代执行watchdog清理，没有操作既有ztermy/conhost。
- 未跑完整Debug/static CTest、未作发布包、未作性能优化验收；本阶段没有产品行为修复需要宣称完成。

## 第二阶段：原生对象计数与根因修复

### 证据链

`AiMemoryDiagnostics` 只在合成内存模式创建。它以非拥有指针观察 delegate、文本项及原生 QTextDocument，记录 create/destroy/live、contentsChanged、documentSizeChanged、宽高变化及最终几何。只保存字符串长度和数字，不保存正文、选区、终端输入或凭据。观察器不调用隐式宽高 getter，避免测量本身触发布局。JSONL 在专用 writer 线程落盘，待写缓冲有界；记录 droppedSnapshots/writeFailed 供校验。退出前先清空 QML diagnostics 引用，再停止计时、排空并 join writer；QObject 随窗口销毁，避免 QML teardown 访问悬空对象。

1. 原始布局的 `phase2-discovery` 在第二轮收到全部 240 个 delta（24,720 字符）后挂在完成切换；长度不再增长。110 秒诊断窗口内累计创建 317 个 delegate，存活 315；QTextDocument 创建 1,273、存活 1,261。
2. 被重建的旧消息在块宽为零时曾报告约 307,440px 高度，正常宽度下只有 7,920px。ListView 行位置随重建漂移数十万像素，观测值达到约 -40,735,361px。
3. 每次异常高度驱动 ListView 的视口填充/重新定位，又创建旧消息块；创建与销毁数量严重失衡。结合第一阶段布局栈，根因落在 **宽度尚未确定时参与高度计算的富文本 → 嵌套布局 → ListView 重建反馈**。这不是单凭 Private Bytes 推断的泄漏，也不表明 Provider 输入无界增长。
4. 只加 `Loader.active: width > 0` 可使 3 轮结束，但 `phase2-width-gate` 仍创建 256 个 delegate、776 个 QTextDocument，旧行重复重建约 83 次，因此没有作为最终修复接受。
5. 明确宽度并改用原生 Column 后，`phase2-explicit-width` 完整通过：6 个 delegate、24 个 QTextDocument，clear/close 后两类 live 均为零；各行位置稳定。没有强制 GC、缩短历史、冻结 Pane 或截断消息。

### 产品改动与诊断改动

产品修复只涉及两处 QML：

- `AiAssistantPane.qml`：消息 delegate 直接引用 `conversationList.width`，避免创建期间依赖尚未就绪的附加 `ListView.view`。
- `MarkdownMessage.qml`：块容器从 ColumnLayout 改为 Column；Loader 明确继承容器宽度、正宽度后再创建富文本；块数据赋值后调用一次原生 `forceLayout()`，及时反映块高度与位置。代码块、表格内部布局、流式节流、文本选择和链接逻辑保留。

完整 diff 中其余内容是显式 benchmark 入口、原生观察器与校验/采集脚本。`product-layout-fix.patch` 单独列出 A/B 唯一的产品差异，便于审阅。`forceLayout()` 是 Column 的原生位置更新，不是每个 token 强制刷新或垃圾回收。[Qt Column 文档](https://doc.qt.io/qt-6/qml-qtquick-column.html#forceLayout-method)

### 交互与诊断收尾回归

`phase2-interactions-final`：完整三轮，各轮 240 行。流式中选择前 16 个字符，完成后核对选区仍在；解除选择后读取合成 QTextDocument，逐行核对全部 240 行内容；随后滚动、跳至开头/末尾、1120→920→1120 宽度变化，再隐藏、清空、关闭终端。调用现有滚动函数和原生选择接口，并非物理鼠标/键盘人工验收。

结果：exit=0，峰值 Private 439.13MiB，delegate 8/8 创建/销毁，QTextDocument 30/30，最终 live=0；droppedSnapshots=0、writeFailed=false，无 TypeError/ReferenceError。切换视口会合理创建额外行，所以交互组不与固定视口组的 6/24 绝对数量混比。

此前 `phase2-interactions` 失败保留在证据中：三轮内容/选择已过，测试滚动后没有恢复与实际“回底”按钮一致的 stickToBottom；同时栈上诊断对象早于 QML teardown 析构，产生错误。修正的是测例和诊断生命周期，完整复跑通过后才接受。没有把这次失败删除或计作通过。

### 重复对照的边界

A/B 按 A1、B1、A2、B2、A3、B3 顺序交错运行，均为 Qt 6.8.3 动态 Release、同一原生诊断代码、240 chunks × 3 轮、180s watchdog、1120×800、DPR=1、D3D11、dark/en、acrylic，逐次全新 data-dir；不清空系统缓存、不并行构建。A 仅撤回 `product-layout-fix.patch` 中的布局差异。原始输入直接进入 C++ 会话模型，没有 HTTP Provider 或网络内容参与。

峰值是 CSV 中采到的 Private Bytes 最大值；`kernelPeakCommitBytes` 另存在 result.json，避免把未采到的瞬时峰值说成不存在。平台是每阶段最后 5s 的中位数，不是无限时长稳态。CPU seconds 是该进程累计 CPU 时间：A 卡在第二轮且观察到超时，B 完成更多阶段后退出，不能换算成等工作量吞吐加速比。GPU 为 PID 计数器观测值，不是所有纹理的堆分配账本。

10ms 心跳只覆盖各 10s stage 的事件循环；不覆盖两个 stage 间全部流式注入时间。A 未完成阶段没有可用 heartbeat 终值，超时本身才是其挂起证据。B 的心跳和终端运行冒烟不能替代真实输入延迟、IME、不同 DPI 的人工验收。

clear 后对象释放时延按第一个显示 delegate/document live=0 的 JSONL 时间点减去 `ai-cleared.utc` 计算，是采样上界（通常最多一个 500ms 周期），不是精确析构耗时；不把它解释为进程提交回落时间。Private Bytes 未回到启动值的部分仍需 allocator/Qt/渲染分配分析或更长周期实验。

### 第二阶段命令与二进制身份

以下命令从本工作树根目录执行。源文件中的采集与校验脚本位于 `scripts/`；`build/*.cmd` 是本次实验的临时编排文件，不进入仓库。重新执行时使用新的 OutputDirectory，脚本拒绝覆盖已有实验。A/B 的冻结二进制不被后续构建覆盖。

```powershell
rtk proxy cmd /c build\configure-memory.cmd
# 构建修复版并部署到全新的绝对路径前缀：
rtk proxy cmake --install build/msvc-dynamic-release --prefix C:/Users/GWF/.codex/worktrees/0f53/ztermy/build/memory-runtime-review-b --component Runtime
# A 的唯一产品差异见 product-layout-fix.patch；保留当前 QML 备份后：
rtk proxy git apply -R build/product-layout-fix.patch
rtk proxy cmd /c build\build-memory.cmd
rtk proxy cmake --install build/msvc-dynamic-release --prefix C:/Users/GWF/.codex/worktrees/0f53/ztermy/build/memory-runtime-review-a --component Runtime
rtk proxy git apply build/product-layout-fix.patch
rtk proxy pwsh -NoProfile -File build/run-memory-ab.ps1
rtk proxy pwsh -NoProfile -File scripts/measure_process_memory.ps1 `
  -Executable build/memory-runtime-review-b/ztermy.exe `
  -OutputDirectory build/memory/phase2-interactions-final -Repetitions 1 -MemoryStages -Interactions
rtk proxy cmd /c build\check-memory-phase2.cmd
rtk proxy cmd /c build\configure-memory-static.cmd
rtk proxy pwsh -NoProfile -File scripts/measure_process_memory.ps1 `
  -Executable build/msvc-static-release/ztermy.exe `
  -OutputDirectory build/memory/phase2-static-b -Repetitions 3 -MemoryStages -Chunks 240 -TimeoutSeconds 180
rtk proxy pwsh -NoProfile -File build/run-memory-runtime.ps1
rtk proxy python -B scripts/validate_ai_memory.py build/memory/phase2-ab-a-1 build/memory/phase2-ab-a-2 build/memory/phase2-ab-a-3 --expect-failure
rtk proxy python -B scripts/validate_ai_memory.py build/memory/phase2-ab-b-1 build/memory/phase2-ab-b-2 build/memory/phase2-ab-b-3 build/memory/phase2-static-b
rtk proxy python -B build/compare-memory.py
rtk proxy python -B build/tables-memory.py
```

| 二进制 | SHA256 |
|---|---|
| A/B 原布局 A | `BB9EA77172A4EFED48D3A758267B2A7A5EB06A77D08EDDC23E77B3A44C821026` |
| A/B 修复 B，交互复跑 | `A1D2FB13AD509A6CBF7B5C5E2F165C464384580B44B11C0A53B0924185DDD3CD` |
| 最终动态 Release | `30ADE6F15495B48F317E893995CADA0FD456B17FBC9A08FDF6822BC96132C687` |
| 最终静态 Release | `4E750699476C7F0B298F395B9B9A2F564BFFBF272F02E2B8DDB1EDF093127A4E` |

A/B 两者诊断代码完全相同。冻结 A/B 后，clang-tidy 要求将诊断线程的 `const std::stop_token` 参数改为 const 引用、单 mutex 的 lock_guard 改为 scoped_lock；最终动态/静态包含这两处诊断写法修正，产品 QML 不变。该区别明确保留，不把不同哈希说成同一二进制。

静态与动态对照使用相同产品源码和 Qt 6.8.3，但 Qt 静态包来自本机 self-built 配置，动态包为已安装 MSVC 包；因此比较的是两种实际 Release 配置，不声称差异仅由链接方式单独造成。既有用户进程未停止，主机其他负载未做实验室级控制。

### 同参数重复结果

| 组别 | n | 采样 Private 峰值中位数 MiB | 范围 MiB | 样本标准差 MiB | 内核峰值中位数 MiB |
|---|---:|---:|---:|---:|---:|
| A 原布局动态 | 3 | 1619.61 | 1617.07–1620.68 | 1.85 | 1619.61 |
| B 修复动态 | 3 | 477.51 | 456.13–481.20 | 13.53 | 481.86 |
| B 修复静态 | 3 | 459.50 | 437.26–478.84 | 20.81 | 480.02 |

| 运行 | 采样/内核峰值 MiB | CPU seconds | delegate 创建/最终 live | QTextDocument 创建/最终 live | GPU dedicated/shared 峰值 MiB |
|---|---:|---:|---:|---:|---:|
| A 原布局动态 1 | 1619.61/1619.61 | 126.69 | 867/865 | 3473/3461 | 40.01/13.41 |
| A 原布局动态 2 | 1617.07/1617.07 | 125.69 | 863/861 | 3457/3445 | 37.02/11.76 |
| A 原布局动态 3 | 1620.68/1620.68 | 125.31 | 861/859 | 3449/3437 | 56.32/24.13 |
| B 修复动态 1 | 477.51/478.93 | 33.22 | 6/0 | 24/0 | 37.02/10.44 |
| B 修复动态 2 | 481.20/482.54 | 31.88 | 6/0 | 24/0 | 49.30/17.65 |
| B 修复动态 3 | 456.13/481.86 | 31.72 | 6/0 | 24/0 | 60.38/19.55 |
| B 修复静态 1 | 437.26/438.36 | 31.16 | 6/0 | 24/0 | 37.68/14.85 |
| B 修复静态 2 | 459.50/484.30 | 31.02 | 6/0 | 24/0 | 53.07/24.34 |
| B 修复静态 3 | 478.84/480.02 | 31.11 | 6/0 | 24/0 | 37.02/14.36 |

### 各阶段 Private Bytes 平台（每组各轮最后 5s 中位数再取中位数，MiB）

| 阶段 | A 原布局动态 | B 修复动态 | B 修复静态 |
|---|---:|---:|---:|
| empty-idle | 222.05 | 222.52 | 220.95 |
| terminal-idle | 304.58 | 305.37 | 304.53 |
| ai-open-empty | 270.23 | 270.21 | 310.93 |
| turn-1-complete | 339.82 | 340.59 | 340.14 |
| turn-2-complete | N/A | 342.87 | 343.86 |
| turn-3-complete | N/A | 344.11 | 343.73 |
| ai-hidden-retained | N/A | 353.31 | 352.12 |
| ai-cleared | N/A | 394.62 | 391.72 |
| tab-closed | N/A | 475.79 | 435.54 |

A 在第二轮挂起，后续阶段是 N/A，不把进程强制退出的归零值填入。

B 修复动态：clear 后对象归零首次观测 4–22ms；各已完成 stage 最大心跳间隔范围 58–67ms。
B 修复静态：clear 后对象归零首次观测 5–116ms；各已完成 stage 最大心跳间隔范围 59–63ms。

### 可接受的结论与剩余风险

三组 A/B 使原布局 3/3 第二轮超时变成修复版 3/3 完整完成。按内核峰值中位数，1,619.61→481.86MiB，减少 1,137.75MiB（70.2%）；按 250ms 采样峰值中位数则为 1,619.61→477.51MiB（70.5%）。后者会遗漏短峰值，优先采用前者。变化量远大于本组三次离散，但 n=3 仅作描述统计，不宣称跨机器置信区间。

该修复消除了本合成场景的布局重建失控，首轮与空闲平台基本没有下降。动态/静态内核峰值中位数 481.86/480.02MiB，差异很小，不据此推荐改链接方式来降低峰值。GPU dedicated/shared 没有一致的改善，CPU 累计值也不是等时等工作量的吞吐基准。

clear 后原生消息对象确实释放；进程 Private Bytes 仍保留较多，尤其关闭侧栏/终端后的阶段。缺少 allocator/Qt/驱动分配账本时，不能全部叫泄漏，也不能全部叫合理缓存。本次不改变 allocator、终端 backing store、历史长度或全局缓存策略。

尚未覆盖：真实 Provider 长输出/附件/工具循环、长时反复 clear/reopen 的斜率、复杂大表格/代码块的压力矩阵、用户原始崩溃输入与 dump，以及 IME/DPI/真实键盘延迟等人工验收。静态原布局未另做失败 A 组；静态结果证明修复版在该配置下通过，而非独立验证其原版必然出现同一问题。未做完整 Debug/static CTest 或发布包验收。

### 最终检查、失败边界与交付

- 动态、静态 Release 构建通过；Qt 头文件 C4702 告警仍有输出，没有隐藏为无告警构建。
- `ztermy_qml_quality_check` 通过：90 个 QML 文件格式匹配，qmllint 通过；main.cpp 与 AiMemoryDiagnostics.h 的 clang-format、main.cpp 包含新头文件的 clang-tidy（warnings-as-errors）通过。
- `ai-conversation-model`、`provider-http-client` 两项 CTest 通过；Win32 probe SelfTest、Python 汇总自检、全部 9 次主测量的生命周期校验通过。
- 完整交互回归通过，且诊断 writer 的 drop/error 标志均为零。正常产品模式不创建该观察器，不启动 writer、不输出诊断文件。
- 最终动态构建的 `--ui-layout-smoke` 退出 0，用时 173.90s，AI 无障碍及错误恢复 contract 均通过。实际查看混合标题/列表/表格/代码块截图，没有重叠；该图像检查针对布局，没有独立验收主题配色。首次 120s watchdog 截断了仍持续前进的全页面检查，保留失败证据；改用 300s 后完整通过，未更改 A/B 的 180s 窗口。
- **完整 `--terminal-render-smoke` 未通过**：最终修复动态版和冻结原布局 A 都在日志 `Native detached-window drag merges into a tab: true` 之后，以 `0xC0000409`（-1073740791）异常退出，分别用时 22.11/26.86s；不是 watchdog。终端输出/滚动检查在该步骤之前已通过。无 dump/栈，不能凭退出码认定具体内存破坏原因。本次没有修改窗口迁移逻辑，该失败作为独立未解决风险交付，不能宣称整个 smoke 或窗口迁移验收通过。
- 使用同一已有入口的 `--performance-benchmark`（不执行分離窗口迁移，仍验证终端渲染与 workbench 拖动缩放）隔离验证通过：20,000 行、completion=1,833ms、382 次 frame swap、maximumHeartbeatGap=21ms、resize/scrollbar/terminalRendered 均 true，exit=0、survivors=[]。JSON 的 splitWorkspacePassed 在此模式是跳过分支的 true，不作为分屏通过证据。

异常边界与独立终端基准的精确命令：

```powershell
rtk proxy pwsh -NoProfile -File build/run-memory-runtime.ps1 `
  -Executable build/memory-runtime-review-a/ztermy.exe `
  -Scenarios terminal-render-smoke -RunPrefix phase2-runtime-control-a `
  -ResultFile runtime-control-a-results.json
rtk proxy pwsh -NoProfile -File scripts/measure_process_memory.ps1 `
  -Executable build/memory-runtime-final-dynamic/ztermy.exe `
  -OutputDirectory build/memory/phase2-terminal-benchmark -Scenario terminal -Repetitions 1 -TimeoutSeconds 180
```

交付文件：本报告、两处产品 QML 改动、新原生探针及复现脚本。生成的证据归档、配置、密钥、会话文件、真实终端输入和 AI 正文均不进入仓库。现有用户数据与实例未动；本阶段不打 tag，也不制作发布包。

## 第三阶段：Pane、Tab、scrollback 与回收斜率

### 方法

动态 Release 使用同一 `measure_process_memory.ps1` Win32 采样器。生命周期场景固定
1120×800、DPI 96、D3D11、全新 data-dir；依次建立 1/2/4/8 Pane，关闭回到单
Pane，建立 8 Tab、轮换 20 次并全部关闭。三轮独立进程用于描述离散度；另在同一
进程连续重建并关闭三组 8 Tab，观察累积斜率。20,000 行终端输出独立重复三轮。
现有用户实例未停止，计数仅针对实验 PID；不清空系统缓存。

诊断入口通过 `-MemoryStages -Lifecycle` 启用，`-LifecycleCycles 1..5` 控制同进程
重复次数。正常产品路径不读取这些环境变量，不执行合成生命周期，也不创建额外
定时器或采集对象。

### 三轮生命周期结果

各阶段数值为阶段最后 5 秒 Private Bytes 中位数再取三轮中位数；范围反映三轮
离散，不是置信区间。

| 阶段 | 中位数 MiB | 三轮范围 MiB | QML/终端对象结果 |
|---|---:|---:|---|
| 空窗口 | 220.87 | 219.72–222.94 | 15,437 objects；0 TerminalItem |
| 单 Pane | 319.61 | 308.84–320.57 | 16,392；1 |
| 2 Pane | 282.95 | 282.14–359.38 | 17,369；2 |
| 4 Pane | 324.50 | 310.36–450.24 | 19,325；4 |
| 8 Pane | 460.02 | 401.02–510.38 | 23,237；8 |
| 关闭回单 Pane | 463.04 | 362.98–615.37 | 精确回到 16,391；1 |
| 8 Tab 空闲 | 416.36 | 406.57–608.68 | 16,735；仅活动视口 1 |
| 8 Tab 轮换 20 次 | 687.47 | 401.89–730.02 | 对象数不增长 |
| 全关后 30 秒 | 667.18 | 364.59–786.59 | 15,446；0 |
| 全关后 60 秒 | 379.80 | 368.25–664.33 | 15,446；0 |

三轮内核峰值分别为约 972.83、1,349.28、987.82 MiB；差异较大，因此不把任一
单次峰值当作固定容量。所有运行 exit=0、forcedTermination=false、survivors=[]。

### 重复关闭与持续输出

同一进程三组 8 Tab 的关闭后短平台依次为 738.54、754.34、749.20 MiB，未形成
单向增长；第三组关闭后继续等待到 60 秒降至 372.73 MiB。每次关闭后对象数均
精确回到 15,446，TerminalItem 为 0。说明回收存在十几至数十秒延迟和明显的
allocator/Qt/驱动保留，但本场景没有对象泄漏或逐轮 Private Bytes 斜率。

20,000 行终端输出三轮均 exit=0。全程采样峰值约 437.18、421.57、534.11 MiB，
末 5 秒 Private Bytes 中位数为 322.65、321.07、324.95 MiB；GPU dedicated
末段约 33 MiB。该工作负载没有随重复轮次增长。

批量关闭 Pane 的阶段最大事件循环间隔在本组约 277–688ms，说明关闭大量本地
会话仍有可感知停顿风险；这是响应性能问题，不是用缓存裁剪能够证明解决的内存
泄漏。后续若优化，应单独跟踪关闭路径耗时并保持终端退出正确性。

### 结论

现有证据不支持加入强制 GC、Working Set trimming、冻结后台 Pane、缩短终端历史
或激进释放图形缓存。基线和多 Pane 占用偏高，但对象随关闭完整回收，Private
Bytes 最终回落且无重复斜率。暂不实施产品内存策略改动；只有真实 Provider/附件
证据或更长时间的单向增长重新出现时，才进入原生 heap/ETW 分配栈分析。

原报告中的独立窗口 `0xC0000409` 已定位为运行时检查夹具在 QML 合并销毁窗口后
继续读取悬空 C++ 引用。改用 `QPointer` 并在销毁前捕获移动结束状态后，完整
`--terminal-render-smoke` 退出 0；回附合并、Tab 拖动、关闭独立窗格和四 Pane
后续步骤全部通过。受限自动化桌面不能移动系统指针时，测试通过同一个
`detachedWindowMoving` 信号注入目标坐标，释放仍走真实 `WM_EXITSIZEMOVE`。

第三阶段提交前回归：Debug 与静态 Release CTest 均为 129/129；首次 Debug
仅翻译目录因新版本卡文案缺项失败，lupdate 后补齐中文条目并完整复跑通过。
静态 Release 全量 clang-tidy、C++/QML 格式、qmllint、翻译、品牌/界面资产与
代码结构 ratchet 均通过；静态完整 `--terminal-render-smoke` 再次退出 0。
本阶段没有发布请求，因此未生成 MSI/portable，也未执行 ICE。
