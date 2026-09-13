# V5.5 Tab / Pane / Session：实施与验收记录

2026-09-13。对应 [ADR 0119](../adr/0119-tab-pane-session-ownership-and-transfers.md)
与[交互方案](../design/tab-pane-session-interaction-proposal.md)。本页区分已实现、
自动验证及仍需验证的项目；不把设计批准等同于全部专项完成。

后续手验反馈已产生新修订，见 [Tab 拖拽与独立窗口反馈记录](V5_TAB_DRAG_OWNER_FEEDBACK.md)。
下面的构建与测试结果属于首批快照，不覆盖该修订；独立窗口多 Pane 的新增路径现已暂时收紧。

## 已批准的范围

- 单窗格独立窗口优先回到原 Tab；原 Tab 消失或已满时作为新 Tab 回附。
- 第一阶段包含整个多窗格 Tab 的子树合并。
- 只在关闭操作会结束活动会话时确认；移动和回附不确认。

## 本批实现

| 问题/旧实现 | 修改方式 | 验证 |
|---|---|---|
| `TerminalTab` 同时代表会话和布局位置 | 抽出 `TerminalSessionState`，保留现有 owning `unique_ptr` 注册表；布局转移只改变归属 | 控制器假会话确认移动前后每会话启动一次、停止零次，输入标记仍进入原会话 |
| 中心交换会互换会话的 paneId | 交换树边/叶子的位置，同时保留叶子 ID 和恢复意图 | 同/跨工作区交换及输入路由测试 |
| 只能在当前 Tab 内移动窗格 | 候选 WorkspaceState 支持跨 Tab 移动、提取、交换和整树合并；保存成功后发布 | 容量、目标失效、原 Tab 消失、保存失败保持原状的用例 |
| 脱离仅隐藏主窗口中的原窗格 | 实际提取工作区并记录 windowId，创建独立 Qt Window；支持多个独立窗口 | 原工作区/独立工作区叶子数、native owner/taskbar flags、缩放与回附检查 |
| Tab 拖动被 ListView 的原生拖拽滚动抢占 | 关闭 ListView 的拖拽滚动，保留专门的滚轮处理；Tab 自身处理结构拖动 | 同一鼠标路径由无拖动事件变为整棵二窗格树成功投放 |
| 目标 Tab ID 与旧布局短暂混用，旧窗格抢焦点 | 主窗口 ID 从完整布局快照派生，合并更新通知；仅活动原生窗口通过视口焦点激活会话 | 放大→新 Tab→脱离→回附连续回归 |
| 新视口绑定后旧视口仍可能发出排队信号 | 每次绑定拥有独立 QObject 连接上下文，解绑时销毁 | GUI 中用空输入验证旧视口不能激活，新视口正常；不写入 Shell 命令 |
| 关闭其它 Tab/排序可能影响独立窗口 | Tab 索引、排序和批量关闭按所属 windowId 限定 | `scopesTabCommandsToTheirOwningWindow` |
| ConPTY close 后直接丢弃进程句柄，可能留下 Shell | 关闭伪控制台后等待 500 ms；若自建 Shell 仍运行，则终止该进程并等待完成；沿用后台停机路径 | 新测试先复现等待 5 s 超时，修复后两 Shell 均退出，544 ms |

ConPTY 的行为变化依据：[微软 ClosePseudoConsole 文档](https://learn.microsoft.com/en-us/windows/console/closepseudoconsole)。
这不是按进程名清理系统 Shell，也不是移动时结束连接；操作对象为会话持有的自建进程句柄。

## 可操作入口

- 窗格标题拖到另一窗格：边缘拆分、中心交换。
- 窗格标题拖到 `+`：提取为新 Tab；拖到已有终端 Tab：移入该 Tab。
- 终端 Tab 拖到当前可见窗格边缘：整个布局树合并，保留子树比例。
- 窗格/Tab 拖出主窗口：独立窗口；独立窗口工具按钮可回附。
- 独立窗口原生标题拖回主窗口 Tab 栏：通过系统移动结束事件请求回附。
- 活动独立窗口关闭会询问；取消保留会话。主窗口关闭到托盘不结束会话。
- 拖动期间 Esc 取消本次拖动；正常终端输入不增加新的补全拦截。

## 自动证据

- Debug 和静态 Release 曾各完成 126/126 CTest（61.50 s / 59.56 s）。
  这些结果早于最后的 ConPTY 退出及按窗口批量关闭修正；最终门禁应以追加记录为准。
- 完整静态 clang-tidy：`build/v55-static-quality.log`，退出 0。
  收尾改动的复验记录：`build/v55-final-tidy.log`，结果在完成后追加。
- 连续窗口回归：`build/test-data/v55-atomic-window-state/logs/ztermy.log`，退出 0。
  包含旧视口失效、原生独立窗口、原 Tab 回附与二窗格 Tab 鼠标合并。
- ConPTY 复现：`build/v55-conpty-before.txt`；修复验证：`build/v55-conpty-after.txt`。
- 工作区 schema 7→8 的固定旧文档夹具，检查恢复意图、无关字段保留及重写版本。
- 早期失败记录保留：`v55-window-transfers-debug*`、`v55-transfer-gesture-diag`、
  `v55-merge-gesture-diag`、`v55-window-stage-diag`。不将其算作通过证据。

## 边界与下一阶段

- 当前一个独立窗口承载一个工作区布局树；尚未实现独立窗口内部的多 Tab 标题栏。
- 跨原生窗口直接投放到任意窗格、完整目标预览与通用拖动代次协议仍待完善。
- 窗口位置、尺寸和最大化状态的持久化尚未实现；本批 schema 8 保存归属和回附目标。
- 主机密钥提示或 IME preedit 期间拒绝转移的检查已实现；真实 IME 组合输入、
  跨显示器 DPI、Snap Layouts 和物理原生标题拖动仍需要桌面验收。
- 此次未连接真实 SSH 主机；假会话及本地 Shell 证据不能代替真实远程连接验收。
- Qt 的管线缓存锁警告在并存实例中仍可见；没有把它过滤为不存在。

以上边界不是新的审批问题；已批准的方案继续作为后续实施依据。

## 本批交付结果

- Debug 全量 CTest：126/126，60.94 s，`build/v55-final-debug-ctest.log`。
- 静态 Release 全量 CTest：126/126，57.44 s，`build/v55-final-static-ctest.log`。
- 最后的嵌套独立窗口回附/原 Tab 消失/保存失败/按窗口批量关闭补测：
  `build/v55-final-transfer-controller.txt`，3 个用例通过（含初始化/清理共 5 PASS）。
  其中未来 schema 拒绝保存的警告是主动制造的失败路径，模型保持原状。
- 全量静态 clang-tidy 及收尾复验均退出 0，日志依次为
  `v55-static-quality.log`、`v55-final-tidy.log`、`v55-last-tidy.log`、
  `v55-window-return-tidy.log`（均在 build 下）。
- 最终格式、71 个 QML 的格式/lint、2316 条翻译和结构门禁通过，
  `build/v55-complete-gates.log`。Qt 生成代码中的既有 C4702 警告仍存在。
- 静态窗口连续操作与关闭确认取消：`build/test-data/v55-static-window-final/`，退出 0。
- 静态顶栏鼠标、键盘/滚轮路由：`v55-title-final`、`v55-keyboard-final`，均退出 0。
- 修复后已退出的 3 次隔离 GUI 测试 PID 26344、23192、18136，复核未发现子进程残留。

独立静态 exe：`build/validation/v55-20260913/ztermy.exe`，50,729,472 字节。
SHA-256：`6EDD3D59D44F53ABCB83B50D49812B5966BDF9ECA0CC5B413FA40AA420CCC491`。
副本与构建输出哈希相同。本批不生成 MSI、不提交、不推送；尚有上节所列后续专项范围。

工作区存储升级为 schema 8；推荐先用隔离目录验证，避免与旧安装版交替写同一份工作区状态：

```powershell
.\build\validation\v55-20260913\ztermy.exe --data-dir D:\Repo\Qt\ztermy\build\test-data\v55-manual
```
