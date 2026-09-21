# V5：体验收敛与工作区设计

启动：2026-09-12。状态：核心范围已随 0.5.0/0.5.1 交付，活动专项关闭。
V5 是工作跟踪系列，不是应用版本号。承接 V4、Netcatty/Nebula 对齐及
`TERMINAL_INTERACTION_REPAIR.md` 中尚未通过真实桌面验收的问题。

本文保留逐步实施和失败证据，不再作为当前待办清单。当前状态与仍需行动的
项目统一见 [V_SERIES_STATUS.md](V_SERIES_STATUS.md)。下方早期未勾选项是当时
计划快照，不能据此判断当前源码是否缺少该功能。

最新修订：[V5.5 Tab 拖拽手验反馈](testing/V5_TAB_DRAG_OWNER_FEEDBACK.md)。
按 owner 要求，本批仅编译静态 Release，不执行测试；独立窗口新增路径暂限单 Pane。
此前批次的测试结果不自动覆盖本批改动。

## 交付与审批边界

- 当前工作区保留，不为清空工作区而提交已知缺陷快照。
- 先完成 V5.1–V5.4，定向测试后汇总全量回归，提供本地静态 exe、问题/原因/
  修复/验证报告和验收清单。用户确认后再提交；不自动推送或发布。
- V5.5 是独立设计关卡：先展示详细 ADR、设计文档和示例，用户明确批准后
  才能改变 Tab/Pane/Session 归属和跨窗口拖放。2026-09-13 已获得明确批准。
- 小改动采用定向检查；节点汇总做完整门禁。实际桌面问题不能只靠单元测试
  宣布完成。保留当前关闭会话的后台清理行为与终端原始交互。

## V5.1 已确认的界面与数据问题

- [ ] 非激活 SFTP 标题收缩为图标，动画期间命中区域同步。
- [ ] 历史首开/切页/换会话自动读取；异步、缓存、无 Shell 命令注入。
- [ ] 历史插入/运行后保留列表位置；插入不计为执行。
- [ ] 脚本编辑及运行页宽度、卡片高度、窄宽度和 DPI 回归。
- [x] 本批终端/侧栏/脚本/窗格图标按钮统一必填名称、图标、提示、选中/hover/键盘焦点；整体桌面验收仍见 V5.4。
- [ ] 定位 hover 瞬时闪灰，记录状态变化证据，不以去掉反馈掩盖。
- [ ] 简化窗格按钮文案。
- [ ] SSH 新建/复制/恢复的身份信息一致。

## V5.2 布局及现有窗格正确性

- [ ] 分隔条双击恢复默认；适度比例吸附与脱离阈值。
- [ ] 窗格标题、按钮、内容与滚动条统一几何，防止覆盖。
- [ ] 同位置拖放不改变会话对应关系；使用不同会话标记验证。
- [ ] 脱离后残留定位与修复：普通/最大化、纯色/材质、DPI、重附着。
- [ ] 对现有附着流程做必要生命周期修复，不提前实现跨 Tab/窗口新模型。

## V5.3 侧栏体系与记录设置

- [ ] 共享占位侧栏/覆盖抽屉的表面组件、关闭与宽度契约。
- [ ] 展示方式和固定状态分开；外部点击关闭不误伤子菜单和未保存编辑。
- [ ] 连接详情更换条目使用可中断的短切换，不排队等待两次完整动画。
- [x] 连接历史记录开关，与终端原始录制、Shell 历史及清除旧记录分离；模块/重启/鼠标定向检查通过，整体验收见 V5.4。
- [x] 本批设置 schema 32→33 连续递增，迁移、无关字段保留与严格布尔类型测试通过。

## V5.4 验收节点

- [ ] 汇总功能验收清单及已知限制。
- [ ] 格式/QML/翻译/资产/结构门禁，静态 Release clang-tidy。
- [ ] Debug、静态 Release 全量 CTest；相关鼠标及桌面运行时回归。
- [ ] 本地静态 exe 交付；不自行打 MSI、发版或推送。
- [ ] 用户验收确认。
- [ ] 按 Conventional Commits 提交经过审阅的工作区改动。

## V5.5 Tab / Pane / Session 专项：已批准，实施中

禁止用勾选本节代替用户审批。设计阶段交付：

设计草案已放在 `docs/adr/0119-tab-pane-session-ownership-and-transfers.md`
与 `docs/design/tab-pane-session-interaction-proposal.md`。两份已获批准；
旧 QML 脱离窗格只是展示投影，以下实施项验证前不宣称真正跨窗口转移完成。

- [x] 记录审批：回原 Tab、首阶段多窗格子树合并、仅结束活动会话时关窗确认。
- [x] 布局域事务：跨 Tab 移动、中心交换保留 ID、提取 Tab、整树合并、失败原状。
- [ ] 会话归属与输入绑定：移动不启动/停止会话，旧视口失效，保留后台关闭。
- [ ] Tab/窗格拖放接线、目标提示、取消与焦点。
- [ ] 独立窗口归属、回附、活动会话关闭确认及恢复持久化。
- [ ] 定向模型/控制器/运行时验证，汇总全量回归与静态 exe。

本批已交付核心归属、转移、独立窗口及关闭确认的可测试版本；完整专项仍包含后续
跨窗口手势、窗口几何持久化等范围。详见
`docs/testing/V5_TAB_PANE_SESSION_ACCEPTANCE.md`；不将首批全量通过当作全部设计已实现。

专项前提交：`4c481fc`（实现）与 `5785146`（设计），均本地签名，未推送。

实施中证据（2026-09-13）：

- 域层转移验证与控制器假会话验证：整棵二窗格树合并、跨工作区交换/提取/移回，
  保留 Pane ID、原会话输入路由和每个后台的启动次数；容量/目标失效原状返回。
- 独立窗口真实归属与回附已接线；新增 schema 7→8 固定夹具，确认无关字段、
  SSH 恢复意图和默认主窗口归属。Debug 定向 workbench-domain、workspace-state-store、
  app-controller 三个完整目标通过（14.58s）。
- 初次控制器测试误在无 GUI 的测试程序创建 TerminalItem，触发字体库断言。
  已把视口绑定检查迁移到 GUI 运行时：空输入不写入 Shell，旧视口不激活，
  新视口正常激活；两项均通过。
- GUI 回归复现主窗口布局未订阅变化、焦点回调重入绑定；改为工作区通知驱动的
  合并刷新，并限定实际活动窗口才能通过视口焦点改变会话。
- 整 Tab 鼠标投放尚在定向排查，域/控制器通过不等于鼠标手势通过。
- 已完成一次完整静态 Release clang-tidy，`build/v55-static-quality.log`，退出 0。
  此后对输入焦点、手势与测试夹具的改动仍需重新定向检查及最终汇总门禁。

1. 详细 ADR（Proposed）：会话/窗格/布局节点/工作区 Tab/原生窗口的身份、
   所有权及生命周期；移动不重连、复制新建会话、关闭与重附着的区别。
2. 交互说明：同 Tab 重排、边缘拆分、中心交换、窗格转 Tab、Tab 布局子树
   合并、脱离/回附、最后一个窗格或 Tab 的处理。
3. 可阅读的文档及示例：正常路径、取消/失败路径、连续拖放、跨 DPI、
   活跃输入和未保存内容的处理；先展示给用户。
4. 实施与迁移计划：事务回滚、稳定 ID、输入路由、焦点/IME、渲染资源、
   持久化/恢复、错误隔离，以及逐项测试矩阵。
5. 用户明确批准后，才拆分实现任务。

相关但不提前实施：非激活终端 Tab 全图标化、系统图标授权/自有图标、
主机遥测缓存复用。保持连接独立，不按同一 Profile 合并 Shell 会话。

## 研究依据

- Qt 6.8 ScrollView：内容宽度绑定 availableWidth，避免父内容隐式尺寸循环。
  https://doc.qt.io/qt-6.8/qml-qtquick-controls-scrollview.html
- Qt SplitView：复用尺寸约束及分隔条，不重写分屏基础控件。
  https://doc.qt.io/qt-6.8/qml-qtquick-controls-splitview.html
- Qt Popup：关闭策略与子弹窗边界。
  https://doc.qt.io/qt-6.8/qml-qtquick-controls-popup.html
- Windows Motion：一致、连贯、可中断、快速响应；时长是调参起点而非验收证据。
  https://learn.microsoft.com/en-us/windows/apps/design/signature-experiences/motion

## 实施证据

### V5.1 第一批（实现中，尚非整轮交付）

- SFTP 根据激活状态展开/收缩；沿用现有宽度动画和命中区域更新。
- 历史初次读取补齐页面、会话和连接就绪触发。缓存过期策略及列表位置仍待处理。
- ScriptEditor/ScriptRunPane 明确绑定 ScrollView 可用宽度；步骤及变量卡片按
  内容高度布局。窄布局和 DPI 的运行时检查尚在推进。
- 补齐历史/脚本提示；标题文案改为“显示标题/隐藏标题”。共享按钮及 hover
  运行时根因仍待处理，不能以补两处提示代替。
- SSH 复制保留来源身份，重连按实际连接请求更新用户名/地址。Debug 中
  `restoresSavedSshWorkspaceWithoutConnecting`、`managesPersistentTerminalWorkspaceSplits`、
  `usesContextualSshTabTitles` 通过（3 个用例，含初始化/清理共 5 PASS）。
  日志：`build/v5-controller-focused.txt`。真实会话重排对应关系另行验证。
- 已完成第一批 Debug 编译、64 个 QML 格式/lint；翻译门禁确认 2303 条。
- 新增 `--script-form-layout-smoke`，无终端命令执行。编辑/运行页在 320/520/800
  宽度下通过；步骤内容隐式高度为 142，卡片含边距为 158（旧固定值为 144）。
  证据：`build/test-data/v5-script-layout-final/logs/ztermy.log`。
- `QT_SCALE_FACTOR=1.5` 的同一布局检查通过，但有 DirectWrite 对旧式
  MS Sans Serif 的字体回退警告；该结果不代表跨显示器 DPI/实际字体视觉验收。
  证据：`build/test-data/v5-script-layout-dpi150/logs/ztermy.log`。
- 初次表单测试失败是 Repeater 委托不在表单 QObject 子树内；测试改为在
  已知布局的可视子项中定位，保留宽高断言，没有放宽通过条件。
- Debug 顶栏鼠标回归通过：1120/600 宽度下导航、设置关闭、加号菜单及
  SFTP 激活展开/非激活收缩全部通过。证据：
  `build/test-data/v5-title-navigation/logs/ztermy.log`。
- 本阶段未重新交付静态 exe、未跑全量、未提交。专项前还有明确未完成项。

### V5.1–V5.2 第二批（定向验证，尚非整轮交付）

- **滚动条覆盖窗格按钮**：旧滚动条放在 Main 的整个终端容器上，未跟随具体
  窗格的内容区域，也统一路由到当前视口。现移入 TerminalSplitNode 的叶子，
  跟随该视口的边界；显示标题时避开标题，隐藏标题时避开悬浮工具条，滚动
  只发给所属视口。`--pane-scrollbar-smoke` 在左右、上下及脱离视图、标题开/关
  共 6 组通过几何与鼠标命中检查；不同合成快照确认没有滚动邻居，新建菜单
  可打开。日志：`build/test-data/v5-pane-scrollbar-2/logs/ztermy.log`。
  这里的脱离视图检查不是原生窗口材质/DWM 残留的验收。
- **拖回同一位置仍重建布局**：原 moveTerminalPane 会删除/重建父分隔节点，
  重置比例并引起 UI 重建。现在识别同一兄弟、相同方向/先后顺序，保留整个
  树；Controller 仅在需要时切换焦点，不做无效持久化和布局广播。
  域测试保留 0.63 比例及全部节点 ID；`preservesSessionRoutingForNoOpPaneMove`
  用两个独立假会话、不同插入标记验证输入未交换且均只启动一次。相邻
  `managesPersistentTerminalWorkspaceSplits` 通过；workbench-domain、
  workspace-state-store、terminal-item 的定向 CTest 3/3 通过。
  日志：`build/v5-pane-routing.txt`。不表示跨 Tab/跨窗口重排专项已实施。
- **hover 先灰再变浅**：实际侧栏按钮全程 hovered=true，旧颜色动画仍出现
  深色中间帧。白底合成红通道最低约 183，最终 220；这是通道值，不是屏幕
  亮度测量。只修改零透明度端的 RGB 仍未通过复测（最低约 190）。最终改为
  保持反馈颜色、仅动画其 alpha；焦点边框和图标不淡化。真实按钮复测最低
  220、最终 220，未再出现暗色超调，保留 120 ms 动画及禁用动画设置。
  对主工具栏、Tab 和标题快捷操作的同类写法应用相同处理。
  证据：`build/test-data/v5-hover-before-2/logs/ztermy.log`、
  `build/test-data/v5-hover-opacity/logs/ztermy.log`。共享按钮/必填提示契约
  仍为独立待办，不能据此宣称全部按钮已统一。
- **历史列表位置**：ListView 原先直接绑定 QVariant 数组，通知时会替换模型。
  现在按首个可见命令与行内偏移恢复位置，同时保留选中命令；会话或搜索词
  变化时重置。`--history-scroll-smoke` 的真实 ListView 验证通过：原内容刷新
  保持 1000，前插一条后为 1049（49 高的同一行仍在原屏幕位置），选中项从
  22 正确跟随到 23；更换会话/搜索条件均回到 0。证据：
  `build/test-data/v5-history-scroll-drained/logs/ztermy.log`。
  `managesMultipleLocalTerminalTabs` 新增“插入前后历史相等”断言，与两项窗格
  用例共 3 个测试通过（含初始化/清理 5 PASS，`build/v5-history-routing.txt`）。
  该 UI 检查退出时仍有 QQmlEngine “2 items ... being created” 警告；显式完成
  incubation 后计数为 0 仍复现，尚不能归为未完成的列表委托。没有屏蔽警告，
  也不据此宣布生命周期全量验收完成，后续需定位引擎销毁路径。
- 本批 Debug 编译中出现 Qt 头文件 `qjsengine.h`/`qvariant.h` 的 C4702 警告。
  未隐藏该警告，完整门禁时需区分生成 QML 代码与项目源码的触发路径。
- 最新 Debug 构建、64 项 QML 格式/lint 与所改 C++ 格式通过；使用静态 Release
  编译参数检查 main.cpp（含本批 UI 检查头）的 clang-tidy 通过。尚未执行整轮
  静态 Release 构建/全量 CTest。运行检查与链接曾因同一 exe 占用触发 LNK1168，
  检查退出后顺序重链通过；后续同一产物的链接与运行检查必须串行。

### V5.1 第三批：共享图标按钮

- **原因**：Main、TerminalWorkbench、TerminalComposer 各自定义工具按钮；名称、
  图标及 AppToolTip 在调用处独立填写。即使有无障碍名称，也不保证有悬浮提示。
  窗格工具条和脚本编辑页又各自绘制背景，不能共享 hover/焦点修复。
- **修复**：新增 `AppIconButton`，强制 `label`/`iconName`；自动提供无障碍名称及
  默认提示，保留可选的更短提示和菜单展开时暂停提示。背景只动画 alpha，
  键盘焦点边框及图标不淡化。采用 TabFocus，鼠标按下图标不会仅因按钮聚焦
  而夺走编辑焦点；Enter/Space 激活只归属已获得键盘焦点的按钮，不新增全局
  快捷键或终端输入拦截。
- 移除三份重复按钮定义。替换 Main 10 处、TerminalWorkbench 19 处、
  TerminalComposer 2 处、ScriptEditor 3 处、ScriptRunPane 1 处，以及窗格
  工具条的 Repeater（6 种动作）。保留录制状态的自定义内容、已有点击逻辑、
  菜单和状态色，不将标题 Tab、普通文字按钮等不同交互强行合并。
- **定向证据**：扩展 `--toolbar-hover-smoke`，五个实际侧栏入口（文件/历史/
  脚本/笔记/AI）的提示均可见且完全展开、文案正确；缺名称/缺图标构造均被
  拒绝，键盘焦点状态断言通过，hover 中间帧未变暗。日志：
  `build/test-data/v5-shared-buttons/logs/ztermy.log`。65 个 QML 格式/lint、
  2303 条翻译检查通过；未增加新的翻译身份或复制第三方资产。
- 结构门禁发现 main.cpp 的测试入口增长超过旧基线。仅提取已有对象查找、
  焦点查询和鼠标辅助函数到 `RuntimeSmokeItems.h`，不改变其行为或放宽基线。
  main.cpp 从 4868 行回落到 4782；结构门禁通过。提取后的 Debug 构建、静态
  Release 参数 clang-tidy 通过；1120/600 宽度下顶栏鼠标回归通过，含工作台、
  SFTP、设置、加号菜单、关闭设置和 SFTP 展开/收缩。证据：
  `build/test-data/v5-shared-buttons-title/logs/ztermy.log`。
- 从共享按钮构建复制独立测试 exe（避免与正在链接的产物互相占用），再次运行
  窗格滚动条 6 组、历史列表位置及脚本表单 320/520/800 宽度：全部断言通过。
  证据：`build/test-data/v5-buttons-pane-scrollbar/logs/ztermy.log`、
  `build/test-data/v5-buttons-history-scroll/logs/ztermy.log`、
  `build/test-data/v5-buttons-script-form-layout/logs/ztermy.log`。
  历史检查的引擎销毁警告仍复现，不能归因于旧内联按钮定义；该项保持打开。
- 本批仍未提交、未发布、未做 V5 全量验收；Tab/Pane/Session 专项审批边界不变。

### V5.2 第四批：分隔条恢复与吸附（验证中）

- **缺口**：侧栏和命令编辑器独立处理拖动，没有恢复/吸附；窗格直接使用原生
  SplitView，比例计算还将分隔条厚度计入分母，均分时左右会相差几个像素。
- **实现**：`ResizeSnap` 统一边界及 8 px 进入 / 14 px 脱离的迟滞规则，单位为
  QML 逻辑像素。`ResizeGrip` 复用在工作台侧栏、终端侧栏、命令编辑器：双击
  默认分别为 208、520、132；保留原有上下限与原来的持久化入口。
- `AppSplitView` 复用 Qt SplitView，应用于终端和 SFTP 两栏。扣除 6 px 分隔条
  后计算比例；吸附 1/3、1/2、2/3，双击均分，并遵守子项的最小尺寸约束。
  Qt 文档要求 handle 保持纯视觉。最初尝试父控件的被动 TapHandler，运行时
  证明被内部过滤器阻断；后改为下述窗口级观察器，不添加抢占拖动的 MouseArea。
  https://doc.qt.io/qt-6.8/qml-qtquick-controls-splitview.html
  https://doc.qt.io/qt-6.8/qml-qtquick-taphandler.html
- 新增 `--resize-interactions-smoke`，检查实际工作台恢复、三方向拖动的吸附/
  脱离/上下限，以及左右和上下原生分屏的双击、拖动中吸附及释放后的比例。
  结果待构建后的运行证据；不先勾选完成。
- 首次检查触发 Debug abort 弹窗：日志确认是测试向 `qt_handleMouseEvent` 直接
  传入 `MouseButtonDblClick`，违反 QPA 接口约束，触发 QTBUG-71263 断言。
  进程命令行确认为 `--resize-interactions-smoke --data-dir .../v5-resize-first`，
  并非正式数据实例。保留日志后关闭该测试进程；测试改成两次 Press/Release，
  由 Qt 自行合成双击。该故障不能当作普通用户双击崩溃的证据。
  日志：`build/test-data/v5-resize-first/logs/ztermy.log`。
- 更换为正常 Press/Release 后检查不再触发 abort，能正常结束；但返回失败，
  不能据此交付。三种普通 ResizeGrip 的恢复/吸附/脱离/边界断言通过；实际
  工作台双击与原生分屏双击/实时吸附仍未通过。修改 preferred size、以及父级
  被动指针观察加临时约束的尝试均不足以修复原生分屏路径，需继续检查实际
  事件接收者和 Qt 拖动期间的覆盖行为。失败证据：
  `build/test-data/v5-resize-native-presses/logs/ztermy.log`、
  `build/test-data/v5-resize-constraints/logs/ztermy.log`。未勾选、未发包。
- 后续事件路由检查（`v5-resize-routing`）确认：测试显式激活窗口、先移动指针
  后，实际工作台双击通过；原生 handle 已取得鼠标抓取，父级 PointHandler
  不活跃、TapHandler.tapCount 为 0。核对本机 Qt 6.8.3 的
  `src/quicktemplates/qquicksplitview.cpp`：childMouseEventFilter 对 handle 的
  按下/移动直接返回 true，阻止继续传播，handleMove 又同步覆盖布局。
- 删除未生效的 QML 观察器，改为 `SplitHandleObserver` 在所属 QQuickWindow
  上观察鼠标事件，始终返回 false；只从自身 handle 边界内左键开始跟踪，
  不处理键盘。吸附采用临时约束并恢复原绑定，双击在鼠标释放、Qt 结束内部
  拖动后恢复均分，防止被拖动状态覆盖。额外检查内容区双击不重置、脱离吸附
  以及第二次拖动可越过原吸附位置，避免临时约束泄漏。
  首次构建在生成 QML 注册代码时缺少头文件搜索
  路径，已补目标私有 include 并显式重新配置；不以旧配置构建失败反复重试。
- 合并 V5 检查入口的重复退出代码，避免 main.cpp 随每个测试增长；结构门禁
  和静态 Release 参数 clang-tidy 已通过。新增 3 条翻译，2306 条检查通过。
- 重新配置后的 Debug 编译及 QML 质量目标通过；`v5-resize-window-observer`
  运行退出码为 0，没有再次触发 abort。实际工作台双击恢复、三种方向的
  ResizeGrip 恢复/吸附/脱离/边界，以及横向和纵向 SplitView 的均分、实时
  吸附、脱离和再次拖动全部通过。测试还检查内容区双击不改变比例、原最小
  尺寸约束恢复，并将二次拖动比例验证为 0.78，避免吸附约束残留。
  证据：`build/test-data/v5-resize-window-observer/logs/ztermy.log`。
  这确认了本次测试断言修复和分隔条定向验证，不替代静态 Release 或全量回归。
- 同一构建的窗格滚动条布局与命中回归也通过：横向、纵向和脱离布局分别
  检查标题显示/隐藏，共 6 组。证据：
  `build/test-data/v5-resize-pane-regression/logs/ztermy.log`，退出码 0。
  脱离布局的几何检查不等同于 DWM 材质残影验收，后者仍未完成。

### V5.1 第五批：历史页重新打开后的新鲜度与失败保留

- **原因**：原自动入口只接受 `idle`，第一次成功或失败后，关闭/重开与切页
  都不会再次读取文件；失败回调还清空上次成功的 Shell 历史快照。
- **修复**：页面可见且会话就绪时，首开、切回历史、切换会话和就绪变化均
  触发异步重读；`Qt.callLater` 合并同轮界面通知，Controller 对正在读取的
  同一会话直接合并请求。只有显式打开/切换/刷新触发文件 I/O；原每秒计时器
  仍只合并内存里的本次会话记录，不增加远程轮询或 TTL 定时器。
- 读取期间与读取失败时保留本会话上次成功快照；错误信息仍显示，不将失败
  伪装成成功。内存快照不持久化，也不跨会话复用；不注入 Shell 查询命令。
- 新增 Controller 测试：临时 APPDATA 中的历史文件增量可重读、两次并发刷新
  只完成一次、读取失败保留快照、新会话不继承快照，假终端 input/paste 均空。
  与 `managesMultipleLocalTerminalTabs` 一起通过，含初始化/清理共 4 PASS。
  证据：`build/v5-history-refresh.txt`。
- 新增 `--history-autoload-smoke`，用隔离 CMD 会话（没有受支持的历史路径，
  不读取个人历史、不输入命令）和合成成功结果测试真实 QML 的就绪、缓存后
  重开、切回历史页触发。退出码 0；这验证 UI 触发，不代替真实远程 SFTP
  文件读取验收。证据：`build/test-data/v5-history-autoload/logs/ztermy.log`。
- 历史滚动位置回归通过：相同上下文保留位置，新行插入后锚点移动为 1049，
  切换会话/搜索才复位。证据：
  `build/test-data/v5-history-refresh-scroll/logs/ztermy.log`。
  引擎销毁时 2 个创建中对象的警告仍存在。核对 Qt 源码后确认该计数并不等于
  incubationController 的待创建数；删除“等待其变为 0”的无效诊断步骤，
  不关闭警告、不将该项标成已修复。
- Debug 编译、QML 质量目标通过；新增测试的静态 Release 参数 clang-tidy、
  格式和结构门禁通过。真实 SSH 自动读取、重连身份变化及最终全量仍待验收。
- 删除无效诊断后重编译，最终 `history-autoload` 与 `history-scroll` 都退出 0；
  22:17 的滚动检查仍报 1 个创建中对象（此前为 2），警告没有消失。
  最新证据：`build/test-data/v5-history-autoload-final/logs/ztermy.log`、
  `build/test-data/v5-history-scroll-final/logs/ztermy.log`（后者为追加日志，取
  2026-09-12 22:17 本轮记录）。AppController.cpp 与 main.cpp 的静态 Release
  参数 clang-tidy 也通过；并非全量静态 Release 门禁。

### V5.3 第一批：连接历史记录开关

- **原缺口**：连接状态变化无条件进入历史并异步保存，没有可关闭的入口。
  设置 schema 33 新增 `connectionHistoryEnabled`，默认开启；schema 32 迁移
  保留原有行为、模型/凭证引用和窗口设置。详细契约见 ADR 0115。
- 日志页增加“记录连接历史”。关闭停止新增与自动更新，不断开连接、不删除
  旧记录、不改变 Shell 历史或原始录制；已有活动条目在关闭时终止记录并显示
  “记录已停止”，该时间不是终端断开的时间。重新开启不补录关闭期间活动。
  手动收藏/删除/清理旧记录仍可保存，不删除原始录制文件。
- **写入修复**：旧析构调用 worker.clear 会丢掉最新排队快照。现在保存时合并
  待写快照，只留最新一份在进行中的原子写入之后；退出等待最后快照完成。
  关闭开关可完成关闭前采集数据的尾写，但不在 GUI 上等待磁盘。
- **测试抓出的初始化问题**：第一版只在历史模块构造时读取默认设置，真正
  设置随后才加载，导致重启后开关为关但仍新增记录。集成测试确实失败，
  修正为设置加载完成后同步策略，再跑同一用例通过。
- `application-settings`、`connection-history` 定向 CTest 通过；新增集成
  `persistsConnectionHistorySwitchWithoutStoppingSessions` 验证禁用、恢复、
  重启禁用、不补录、修改其它设置不重置开关、重置设置恢复默认及连接不被停。
  证据：`build/v5-connection-history-switch.txt`。
- 真实 QML 鼠标命中检查在 1120/600 宽度下均能关闭/重开开关；原顶栏导航、
  设置关闭、加号及 SFTP 展开/收缩回归同时通过。隔离数据证据：
  `build/test-data/v5-history-recording-ui/logs/ztermy.log`。
- Debug 编译与 QML 质量目标通过；翻译检查 2310 条、结构门禁通过。
  Qt 生成代码 C4702 警告仍在，不隐瞒或通过关闭警告处理；本批不是全量
  静态 Release 门禁、不是最终静态包交付，详情抽屉与材质等其它项继续待办。
- 最终补充失败保存检查：使用隔离目录中的未来版本设置模拟拒绝写入，确认
  setter 返回失败且运行策略不变。预期的 Unable to persist 警告由此触发。
  与上一批 Shell 历史测试一起运行共 4 PASS，证据：
  `build/v5-history-recording-final.txt`。最终 `application-settings` 和
  `connection-history` 再跑 2/2 通过，未放宽断言。
- 静态 Release 编译数据库参数下的定向 clang-tidy 全部通过：
  AppController.cpp、ApplicationSettings.cpp、ConnectionHistoryController.cpp、
  main.cpp 及本批三个测试源文件。仍需 V5.4 的全量静态分析与双构建回归。

### V5.2 第六批：脱离材质分层诊断（进行中）

- 当前脱离流程创建独立的 QML Window 与 TerminalSplitNode 视口，原生策略
  通过 configureDetachedWindow/applyBackdrop 设置；并不是直接重挂同一个
  QQuickItem。尚不能将用户截图中的矩形直接认定为旧纹理缓存。
- 新增 `--detached-material-smoke --data-dir <隔离目录>`：在真实脱离组件中
  使用合成终端快照，分别测试 acrylic/solid 的普通、最大化、隐藏后方主窗口、
  恢复尺寸。保存 Qt scene 与 Windows 合成截图并检查正文采样均匀性，用于
  区分渲染层残留和背景窗口透出；不建立 Shell/SSH 会话，不发送终端输入。
- Qt 的 grabWindow 路径与 QScreen 抓取 Windows 客户区的 BitBlt 路径不同；
  不能用原始场景截图代替桌面材质验收，也不能以 native 属性返回成功替代
  像素证据。依据：Qt 6.8.3 本机源代码 qwindowsscreen.cpp / qquickwindow.cpp，
  https://doc.qt.io/qt-6.8/qquickwindow.html#grabWindow 。
- 新测试已通过定向静态 Release 参数 clang-tidy 和结构检查；待本轮构建后
  获取图像证据。未修改材质实现，不宣称残留已经修复。
- 后续运行确实复现了旧尺寸矩形：Qt 原始场景均匀，桌面截图块内 RGB 为
  133/137/141，外部为 48/52/56；隐藏主窗口后仍存在。截图必须取屏幕上的
  客户区区域，最初按透明 HWND 获取的 GDI DC 图像不能当最终桌面证据。
- **纠正一次视觉误判**：并排预览时曾误认为负边距的完整客户区扩展消除了
  色块。逐点复核证明扩展前后像素完全相同。重设边距、窗口事件后重设、
  frameSwapped 后重设及两个透明属性的单独关闭均未通过断言；这些无效的
  实现/诊断覆盖已从产品代码和测试入口移除，没有留下定时刷新或事件过滤。
- **实际有效修复**：Qt 6.8.3 D3D 透明 swapchain 使用 DirectComposition；
  其 QPA 提供关闭额外 GDI 重定向表面的选项。隔离 A/B 禁用该表面后，原块内
  外都为 44/48/52。现在原生窗口创建前，在 D3D11/12、未选 legacy non-flip
  且用户未显式覆盖时启用该 Qt 选项；仅影响当前进程，OpenGL/软件不强制套用。
  保留原玻璃边距策略。详细约束见 ADR 0116。
- **独立 alpha 格式修复**：Qt setColor(opaque) 会将请求格式改为 -1，但已存在
  的透明 swapchain 不随之消失。共享 applyBackdrop 保持透明能力窗口的
  alphaBufferSize=8，创建即不透明的性能模式仍使用独立策略。修复后切换纯色
  没有再报 swapchain alpha 不一致。
- 正式启动、无手工环境覆盖的 `v5-detached-policy-final` 退出 0；100% 下
  acrylic/solid × 普通/最大化/隐藏父窗口/恢复共 8 场景原始图像检查通过，
  隐藏父窗口后的最大化及恢复另通过桌面像素检查。150% 的同一检查也通过。
  证据：`build/test-data/v5-detached-policy-final/logs/ztermy.log`、
  `build/test-data/v5-detached-policy-dpi150/logs/ztermy.log`，各自 captures 目录
  保存原始/桌面图像。150% 仍有 MS Sans Serif 字体回退警告，不隐瞒。
- 主窗口 `--window-appearance-smoke` 通过，覆盖 acrylic/transparent/mica/
  micaAlt/solid 与透明度契约。日志中的一次设置保存警告来自故意输入非法
  透明度的拒绝测试。证据：`build/test-data/v5-composition-appearance/logs/ztermy.log`。
- 本批 NativeWindow.cpp/main.cpp 静态 Release 参数 clang-tidy、格式和结构
  门禁通过。仍待真实会话重附着、跨显示器 DPI、任务栏与最终双构建全量验收；
  不据此勾选 V5.2 的完整脱离验收，也未提交或发包。
- 合成路径变更后的顶栏鼠标回归也通过：1120/600 下工作台、SFTP、设置、
  设置关闭、加号菜单及日志开关均正常。证据：
  `build/test-data/v5-composition-title/logs/ztermy.log`，退出码 0。

### V5.3 第二批：共享侧栏表面与连接详情（进行中）

- 原详情以宽度从 0 动画展开，正文反复重排；没有外部点击关闭或固定选项，
  且 selectedEntry 是点击时的投影副本，后续连接状态变化不会同步。
- 新增 SidePanelSurface，由占位的 TerminalWorkbench 和覆盖的 AppSideDrawer
  共用表面样式与可访问名称；布局宽度仍归宿主所有，不引入会话归属框架。
- 覆盖面板复用 Qt Popup，固定状态独立于展示方式。未固定时外部点击/Escape
  关闭，显式关闭始终可用；dismissBlocked 可让含未保存内容的调用方拦住
  自动/显式关闭和条目替换，当前只读连接详情不设置此标志。
- 正文固定宽度并可滚动。退出/进入各 90ms，条目切换总预算 180ms，连续
  请求只保留最新条目；不追加多个完整动画。数据更新按 ID 刷新，不触发切换。
  当前条目移出列表或被删除则关闭详情；隐藏日志页关闭其覆盖面板。
- 新增 --side-drawer-smoke，使用隔离目录和合成连接记录，验证真实 QML
  鼠标路径；结果待本批构建完成后记录。未据源代码或编译通过先勾选完成。
- 初轮测试先后发现测试加载路径没有对应资源别名、合成记录缺少必填的本机
  用户/主机字段。改用模块加载并让夹具先通过领域有效性检查，没有放松
  产品校验。有效夹具后，1120/600 宽度的实际鼠标检查通过。
- Qt 在行的按下事件时先自动关闭 Popup，因此不能在释放时立刻替换旧正文。
  关闭期间收到选择保留为最新待展示项，退出完成再显示它；固定状态则直接
  播放中途可中断的两段动画。没有增加外部全局鼠标拦截器。
- 额外验证旧正文保留至退出边界、中途改选不跳变偏移、最终只显示最新项；
  打开并点击面板外的子菜单不会误关详情。阻挡标志同时挡住条目替换和关闭。
  100% 与 150% 全部通过，证据：`build/test-data/v5-side-drawer-boundaries/`
  和 `build/test-data/v5-side-drawer-dpi150/` 的日志与 drawer-600/1120.png。
  已查看 600 宽截图，标题/正文/按钮正常排布；150% 仍报既有字体回退警告。
- 固定类型窄化后最终 Debug 构建、70 个 QML 格式/lint、2313 条翻译、结构
  与静态 Release 编译数据库下 main.cpp（含新测试头）的 clang-tidy 通过。
  最终运行 drawer、toolbar-hover、history-scroll 均退出 0，日志位于
  `build/test-data/v5-drawer-final-<检查名>/logs/ztermy.log`。history-scroll
  的引擎销毁警告仍有 3 个创建中对象，未解决或隐藏。
- 共享契约记录在 ADR 0117。当前完成定向实现与验证，不据此宣布真实编辑器
  未保存确认、物理 Escape、全部 DPI/材质或 V5 最终全量验收完成；未提交。

### V5.4 前置：QML 未完成构造警告

- **症状**：历史列表定向测试退出码为 0，但引擎销毁时报告 1–3 个对象仍在
  构造。此前等待 incubation 队列的尝试无效，不能因此归为“测试结束太快”。
- **源码路径**：Qt 6.8.3 的 QQmlEnginePrivate 析构检查 inProgressCreations；
  控件 contentItem getter 会触发 executeContentItem/quickBeginDeferred，
  与普通列表异步 incubation 计数不是同一件事。AppToolTip 的尺寸绑定直接
  访问 contentItem，会提前触发构造；列表频繁替换/销毁暴露未完成计数。
- **对照证据**：仅把提示框改为 Qt 原生 contentWidth/contentHeight 后，原用例
  不再告警。恢复旧写法重编译后再次出现 3 个未完成对象，新检查脚本退出 1。
  旧写法日志：`build/test-data/v5-qml-lifecycle-09c9edd974cc4fa1aa291c458fd55108/logs/ztermy.log`。
- **修复**：提示框使用原生内容尺寸；审查同类绑定后，AppMenu、AppComboBox、
  EditableSuggestionField 与 ActionButton 也改用 implicitContentWidth/Height，
  避免尺寸读取主动启动 contentItem 延迟构造。保留尺寸上限和布局/操作语义，
  不加等待计时器、手工执行私有 complete 或日志过滤。
- 新增 `scripts/verify_qml_lifecycle.ps1`：新建隔离目录运行历史列表检查，
  除进程退出码外，将未完成构造/重复构造/FATAL 日志作为失败；60 秒超时
  留下进程与日志供诊断，不操作正式数据。复现旧写法失败后，修复构建连续
  三次通过（目录后缀 a97dd9fd67a94d9a8d671bcf19f7b448、
  88f3e50ae8db4e5a923366a00bb9eae3、723ff609323844f9b63d6394ffe5c257），
  补充超时逻辑后的同脚本再次通过（后缀 b9024d9b39cb47ab8de661777ad7946c）。
- toolbar-hover、script-form-layout、title-navigation-mouse 回归均退出 0；
  证据为 `build/test-data/v5-deferred-<检查名>/logs/ztermy.log`。新下拉框测试
  首次使用箭头边界坐标、错误期待可编辑框保留索引，已改为箭头中心和该
  控件原有的“文字写回、索引 -1”契约；不为测试通过改变产品逻辑。
- 这是确定触发路径的修复，不据此声称此前用户关闭 Tab 闪退与本警告同因，
  也不等于所有 QML 生命周期路径已证明无缺陷。字体回退和最终全量仍另行跟踪。
- 修正后的两个下拉控件均在 1120/600 下通过展开尺寸和鼠标选择检查，抽屉
  交互也全部通过，`v5-deferred-controls-final` 退出 0 且无构造警告。
  70 个 QML 格式/lint、2313 条翻译、结构检查已通过；最终 main.cpp（包含
  新测试头）的静态 Release 参数 clang-tidy 也通过，未关闭警告或放宽门禁。

### V5.4 第一次完整门禁（2026-09-13）

- Debug 与静态 Release 全部默认目标构建完成。CTest 均为 126/126 目标
  通过，分别 41.79s / 42.72s。日志位于 `build/v5-20260913-*-ctest.log`。
- 真实 SSH 环境变量未设置；单独以文本日志运行，确认 SSH real-host 为
  2 passed / 7 skipped，不能把 CTest 的 passed 当真实连接完成。
  证据：`build/v5-20260913-debug-real-host.txt`。
- 完整静态 Release clang-tidy、C++ 格式、QML/结构目标退出 0；日志：
  `build/v5-20260913-static-quality.log`。翻译门禁清除了一个已不用的关闭详情
  身份，目前有效翻译 2312 条。Debug 质量日志：`build/v5-20260913-quality.log`。
- 当前静态 exe 的 history-scroll、history-autoload、toolbar-hover、
  script-form-layout、pane-scrollbar、resize-interactions、side-drawer、
  detached-material、title-navigation-mouse、window-appearance 均退出 0。
  各日志/截图在 `build/test-data/v5-static-<检查名>/`。独立生命周期脚本也
  通过，目录后缀 8a0e221d2857442ebf89f9d09593e329。
- dumpbin 依赖表未出现 Qt/VC Runtime/OpenSSL DLL，见
  `build/v5-20260913-static-dependencies.txt`。当前二进制 50,612,736 字节，
  SHA-256 `56C216FB0E00E3C453A792BD11780E365495D1FD2481272DD558C1258F764C3C`；
  它是该轮证据对应的候选产物，尚未交付为最终验收版本。
- 新增 `docs/testing/V5_EXPERIENCE_ACCEPTANCE.md`，按问题/原因/修复/自动证据/
  人工边界汇总，用户确认栏不自动勾选。

### 交付复核发现：连接后的 Profile 编辑与辅助 I/O 归属

- `refreshTerminalHistory` 调用 `transferRequestProvider(tab.sourceProfileId)`，
  后者从当前 m_profiles/m_keychain 构造快照；不是终端连接时的身份快照。
  `sftpConnectionRequest`、终端上传/下载/批传以及 AI 文件下载同样使用这条
  可变 Profile 路径。连接后编辑目标或身份可能让旧终端的辅助请求转向新配置。
- 初连由 startSshConnection 进入 ssh->start；复制/恢复/手动/自动重连经
  attemptSshReconnect 进入 ssh->start。下一批应在这两个实际启动边界绑定
  会话使用的请求提供者，并在重新绑定时取消旧历史任务、递增结果代次和
  清理旧身份的内存历史。普通独立 Profile 传输保持其原有按任务快照的语义。
- 需要用隔离夹具验证连接后编辑 Profile 不改变旧会话目标、重连才采用新
  身份、旧异步历史结果不得进入新身份。应覆盖共享调用者，不只修一处历史
  调用。此项不涉及 Tab/Pane/Session 跨容器归属设计，不提前实施架构专项。
- 当前完成源码路径核实，尚未做该修复；门禁全绿不构成安全交付依据，
  候选 exe 暂不作为最终验收版本交付，后续更改需要重新验证。

### 连接上下文修复与回归（2026-09-13）

- 先加入回环/临时目录用例。补齐夹具的 known-hosts 路径后，旧实现确实
  返回修改后的 Profile 端口而不是原连接端口，测试失败：
  `build/v5-remote-context-before-valid.txt`。不依赖真实主机或 Shell 命令。
- 初连和所有实际重连入口绑定 Profile/身份/路由快照；历史、终端 SFTP、
  上传/下载/批传与 AI 文件下载复用该次连接的提供者。请求还必须匹配实际
  SSH 的主机/端口/用户名；没有上下文或不匹配时失败，不静默采用新 Profile。
- 新连接取消旧历史任务并更换代次，清除旧历史/会话捕获/半行输入、待上传
  文件与旧目录，停止旧 SFTP；取消当前 AI 回合与待执行 SFTP 工具，保留对话。
  已入队传输仍捕获原目标。凭证不额外复制或持久化；详见 ADR 0118 的边界。
- 补测又复现旧 SFTP 队列回调覆盖新主目录：`v5-remote-context-old-callback.txt`。
  所有 SFTP 回调现通过共享 currentTab 校验来源实例的弱引用，覆盖主目录、
  状态、文件/列表结果及主机密钥提示。不是只靠 disconnect 处理已经排队的信号。
- 同一用例验证新/旧提供者、重连采用新身份、旧历史结果拒绝、旧 SFTP 回调
  拒绝、空/不匹配上下文失败。测试明确取消旧身份引用再改用户名，保留钥匙串
  身份优先的既有语义；没有为了测试改变共享身份模型。
- Debug 定向 4 个用例共 6 PASS，相邻 CTest 5/5；静态版本完整新用例 3 PASS。
  证据：`build/v5-remote-context-guarded.txt`、`build/v5-remote-context-final-static.txt`。
- 重新构建后全量 CTest：Debug 126/126，42.18s；静态 Release 126/126，41.58s。
  日志为 `build/v5-context-final-{debug,static}-ctest.log`。真实主机跳过限制未变。
  全量静态 clang-tidy 和最终静态运行时复验在本次门禁继续收尾，不沿用旧 exe 哈希。

### 本地验收版交付（2026-09-13）

- 上下文修复后完整静态 clang-tidy/格式/QML/结构目标退出 0，日志：
  `build/v5-context-final-static-quality.log`。不是仅定向分析结果。
- 最终静态 history-autoload、toolbar-hover、script-form-layout、pane-scrollbar、
  resize-interactions、side-drawer、detached-material、title-navigation-mouse、
  window-appearance 均退出 0；历史滚动另由生命周期脚本通过（目录后缀
  c42ffa9ff51146648b6fd8dfe27b949e）。日志在 `build/test-data/v5-context-final-*`。
- 最终静态 150% 的 detached-material 和 side-drawer 均通过；仍是单屏缩放
  检查，不冒充跨显示器验收。字体回退警告仍列在验收清单。
- 将单 exe 复制到独立的 `build/validation/v5-20260913/ztermy.exe`，校验复制
  前后 SHA-256 一致，并从该独立目录运行历史滚动检查，退出 0、无构造警告。
  大小 50,616,832 字节，SHA-256 为
  `869A4B79C43A3DD47363D0E2C9DB73215A47A011F21804975E5994DE579D0F6F`。
- 详细操作清单、已确认原因、修复、自动证据、真实 SSH/跨屏等人工边界见
  `docs/testing/V5_EXPERIENCE_ACCEPTANCE.md`。该文件提供隔离数据目录启动方式。
- 至此交付本轮可测试的本地静态快照；用户验收/提交确认尚未发生。不提交、
  不推送、不生成 MSI，不推进仍需设计审批的 Tab/Pane/Session 专项。

任何未运行或未通过项目保持未勾选。以上不是人工验收的完成声明。

### 顶栏与终端交互追加打磨（2026-09-13）

- 设置 Tab 没有与 SFTP 一样按激活态收缩。复用 `TerminalTabAction` 的收起态：
  设置非激活宽 38px，活跃显示标题；终端暂用内置通用图标承载状态，活跃项
  展开标题。完整的 Profile 图标方案仍留待后续设计，不新增 Tab 所有权模型。
- 多 Tab 左右箭头占据标题空间且连续点击与布局动画相互干扰；删除箭头入口，
  列表滚轮直接移动 `contentX`、不切换会话，保留下拉和快捷键。下拉关闭按钮
  清除额外背景。首轮鼠标测试抓到设置 `implicitWidth` 与省略文本的绑定环，
  改为明确宽度后，在 1120/600px 重跑无 QML WARN。
- 侧栏拖动时视口留白仍做中速动画，控制器每次移动又写磁盘并刷新完整 Tab
  模型，造成追赶感。拖动中用瞬时宽度直接分配侧栏/终端几何，停用该段动画，
  松手只保存一次。`v5-side-resize-debug` 的真实鼠标夹具验证视口与侧栏几何
  差值小于 2px、拖动中持久值未变、释放后提交。
- 终端滚动条的轨道来自显式 `Rectangle`；拖动时快照未更新就重复按旧 offset
  发送相对滚动。去掉轨道、收细滑块、闲置淡出，未收到更新时仅保留最新位置，
  拖动期间固定滑块高度。`v5-new-scrollbar` 的六种布局/命中回归通过；高速
  滚动的目视体验仍留给人工验收。
- 窗格标题原只有拖拽手势，没有点击激活；TapHandler 在点击后切换活跃窗格
  并聚焦视口。Debug 夹具已验证点击与标题上中部投放，将左右布局改为上下。
  曾尝试提高 DropArea 层级，回归发现无帮助并撤回。诊断事件证明标题本身
  可接收投放；投放到原布局位置会按模型保持不变，不等于拖放失效。
- 原活跃 Tab 关闭先发布旧焦点已失效的列表，再激活下一个；激活又触发一次
  同步工作区保存。改为先选好活跃项，再发布列表，只保存一次。独立静态
  Release 8 Tab 计时中位数由旧快照 209ms 降为新版 110ms，见
  `build/test-data/v5-{old-tab-timing-unloaded,final-tab-timing}/logs/ztermy.log`。
  后台会话停机仍异步，不把这组本地模拟当真实 SSH 关闭验收。
- Debug/静态 Release 全量 CTest 各 126/126 通过，但两套各 12 并发同时启动
  时 Windows 弹出一次 `cmd.exe` 0xc0000142。只读进程核查发现 9 个本轮
  `cmd.exe /D /Q` 子进程父进程已退出；只终止这 9 个确证测试子进程，复核
  数量为 0，旧 Codex/Ollama 进程未动。两套本地终端停机测试随后单独复跑
  均通过且无同类残留。关联高并发，但具体 DLL 初始化失败原因未确认；后续
  全量门禁改为串行/限并发，不把 126/126 隐瞒为“没有弹窗”。
- 静态 Release 全量 clang-tidy、格式、QML、翻译、结构门禁退出 0；70 个 QML
  格式正确，删除箭头后翻译 2310/2310 有效。质量门禁后增量重建静态 exe。
  复制到 `build/validation/v5-20260913-tab-polish/ztermy.exe`，长度
  50,632,704 字节，SHA-256 为
  `F842518DDBCE3682FB3976F76606C6024531C39C50FFFD0C4DE508EF9969ADB7`；
  复制与构建源哈希相同，保留上轮快照未覆盖。
- 从独立 exe 串行启动 title-navigation-mouse、pane-scrollbar、terminal-render、
  ui-keyboard 四项，均退出 0，无新 QML WARN；生命周期脚本重复 3 次通过。
  其中 terminal-render 验证侧栏按住时即时几何/松手保存、窗格标题点击与标题
  投放，ui-keyboard 验证 8 Tab 溢出滚轮不改变活跃终端。相应日志在
  `build/test-data/v5-polish-artifact-*/logs/ztermy.log`。单独运行后没有留下新的
  `cmd.exe`；真实 SSH、高速鼠标滚动/缩放和跨屏仍需人工验收。
- 细项验收与本次弹窗诊断记录更新于 `docs/testing/V5_EXPERIENCE_ACCEPTANCE.md`。
  工作区保持未提交，用户确认栏保持空白；Tab/Pane/Session 架构专项仍需设计
  文档与审批，不在本轮实现。
