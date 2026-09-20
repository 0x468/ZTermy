# 0.5.0 汐：体验打磨与回归记录

日期：2026-09-20。版本文案：汐 / 半轮月，一道水。

## 交付顺序

1. `92f66a7`：保存上一阶段窗格选区、悬浮操作与材质设置修复。
2. `4e96234`：统一 hover、增加有边界的滚轮加速、修复首次拖选及窗格边界；
   `ui/v2-design-system` 快进合入 `main`。
3. `a5fc187`：版本更新至 0.5.0，版本卡改为半月与水纹，悬停时轻微起伏；
   减少动画、关闭动画及性能模式不播放该效果。
4. `eb287f1`：修复完整回归发现的问题并保存验证证据。
5. `c580efb`：关闭独立窗格时保留主窗口选择，补上多窗格 B 及切到 A 后关闭的回归。

首轮回归完成时尚未推送或打标签；后续所有者要求发布 `0.5.0`，见文末发布前复核。
用户已有的 `CMakePresets.json` 改动不纳入提交。

## 问题、原因与修复

| 问题 | 确认的原因 | 修复与边界 |
| --- | --- | --- |
| 标题按钮 hover 先深后浅 | 标题动作和窗口按钮仍保留颜色插值，与侧栏的即时反馈不同 | 移除这些按钮的 hover 颜色插值，保留 Tab 激活动效；工作台与设置导航复用 `SideNavigationItem` |
| 快速滚轮缺乏响应增强 | 设置与普通终端回滚没有共享的连续滚轮步进规则 | 同向短间隔滚轮逐级增强，最大四倍；间隔或方向变化重置。像素滚动、终端鼠标协议及备用屏幕按键不走加速 |
| 非激活窗格第一次拖选中断 | 同一 viewport 重复 attach 后再次发送 resize，终端引擎重置尚未结束的选区手势 | 同一 viewport 重复 attach 直接返回，QML 不重复挂接；运行时断言完整 release、无 cancel、无激活 resize |
| 四窗格边框粗细不同 | 分隔布局存在小数位置，活动边框又使用不同厚度 | preferred size 取整，活动边框统一为 1 个逻辑像素；截图及运行时同时检查边框厚度和位置 |
| 快速启动/关闭 Shell 时连续 `cmd.exe 0xc0000142` 弹窗 | Shell 初始化尚未连接控制台时先关闭 ConPTY | 明确关闭会话时先结束自身持有的 Shell 进程，再释放管道和 ConPTY；仍在后台清理，不注入输入，不屏蔽 Windows 错误弹窗 |
| 静态测试启动停在 Qt 错误弹窗 | 两个显式选择 offscreen 的静态测试未链接该平台插件 | 为 application-instance、window-state 显式导入插件，并设置启动超时 |
| 递归窗格部分动作没有实现 | 根递归组件缺少 copy mode、scrollToFraction 转发 | 按已有轻量转发方式交给当前活动 viewport，不增加第二套终端状态 |
| 回归脚本沿用旧设置布局 | 已合并的不透明度控件仍在 Tab 顺序中，按需出现的重置按钮未计入 | 更新焦点测试模型，保留可访问的重置按钮；独立验证亚克力、玻璃和透明背景的不透明度控件 |

ConPTY 的初始化错误机制有 [Microsoft 文档](https://learn.microsoft.com/en-us/windows/console/creating-a-pseudoconsole-session#creating-the-hosted-process)
说明；本机以两个立即关闭的并行控制台，以及 16 次快速启动/关闭进行了定向验证。
随后完整回归期间检查 Windows System / Application Popup 事件，没有新增该错误。

结构整理仅复用已有职责：转移私有实现结构、移出拖动输入层及运行时辅助函数、
删除与现有 Binding 重复的属性赋值。没有扩大结构门禁基线。
拖动输入层仅在标题命中或实际拖动时可见，不能常驻遮挡其它控件。

主题导入测试保留 `QVERIFY(has_value())`，随后提取有默认值的局部副本，
避免静态分析无法沿 Qt 测试宏证明 optional 有效性的问题；未移除断言或放宽导入规则。

## 验证环境与记录

- Windows 11、MSVC 2022、Qt 6.8.3、Ninja，通过 CMake presets 构建。
- Debug：`msvc-dynamic-debug`，129/129 CTest 通过，41.78 秒。
- 静态 Release：`msvc-static-release`，129/129 CTest 通过，41.87 秒。
- 两套 CTest 顺序运行，每套内部 parallel 12，避免跨进程矩阵争用系统剪贴板。
- 构建缓存显式启用 BUILD_TESTING，不修改用户 presets。
- 日志：`build/v05-debug-final.log`、`build/v05-static-final.log`。
- 静态 Release 全量 clang-tidy：302 个翻译单元通过；C++ 格式、90 个 QML 文件格式、
  QML 检查与结构门禁通过，见 `build/v05-quality-final.log`。
- 翻译目录、图标/元数据等 CTest 门禁包括在上述 129 项内，`git diff --check` 通过。
- 首轮回归二进制：`build/msvc-static-release/ztermy.exe`，文件版本 0.5.0。
- 首轮二进制 SHA-256：`8849c41903f0a45952a0be3bb49f6477ef7c83a4581da2f800e0a163030e95cb`。
- 测试数据隔离在 `build/test-data/`，不使用用户的主机、密钥和历史数据。

### 运行时证据

已确认的终端检查包括首次拖选连续性（0 次激活 resize）、即时 hover、
选区操作菜单、标题聚焦/拖动、单窗格脱离与回附、独立窗口按钮贴边、
最大化→最小化→唤回仍最大化、四窗格边框及滚动条交互。

截图位于 `build/test-data/v05-confirmed-terminal-render/`，包括
`four-pane-edges.png`、`detached-pane-maximized.png` 和 `terminal-render-complete.png`。
版本卡实图位于 `build/test-data/v05-final-ui-layout/dark-regular-about.png`。
这些文件是本地构建产物，不进入版本控制。

最终源码的 `ui-layout` 完成，日志明确记录 `Responsive UI layout runtime smoke test completed`；
包括亮/暗主题与紧凑/常规尺寸下的工作台、设置及助手页面。

以下九项在最终静态 Release 上正常可见启动，均退出 0，数据目录为
`build/test-data/v05-final2-<检查名>/`：

| 检查 | 结果 |
| --- | --- |
| ui-keyboard | 通过：设置焦点、主题键盘选择、对话框、空/单 Tab 和 hover |
| theme-settings | 通过：统一主题及材质不透明度设置 |
| title-navigation-mouse | 通过：标题栏鼠标导航与菜单 |
| terminal-render | 通过：输出、滚动、首次拖选、窗格布局、脱离及回附 |
| lifecycle-runtime | 通过 |
| window-appearance | 通过 |
| window-resize | 通过 |
| window-dpi | 通过：本机 Qt 缩放 1.0，不替代混合 DPI 跨屏检查 |
| window-runtime | 通过：最大化、最小化、唤回、还原 |

最终复查 Windows System / Application Popup，最后一条 `cmd.exe 0xc0000142`
仍为修复前的 03:09:49；后续定向、两套 CTest 和可见窗口回归未新增该错误。

原生窗口检查必须正常可见启动：隐藏启动会改变 Windows 首次 ShowWindow 行为，
不能用其结果判断最大化/最小化回归。包装脚本等待应用进程本身退出，
不使用 PowerShell `Start-Process -Wait` 的整棵后代进程树等待。
完整截图矩阵需要超过两分钟，短的单项检查超时不适用于该矩阵。

## 未覆盖与验收边界

- real-host 环境未配置，SSH/代理/相关 AI 的真实主机用例按条件跳过；
  CTest 通过不等于真实 SSH、SFTP 和代理主机验收通过。
- 运行时检查使用本机实际窗口，但仍是合成输入；用户真实设备上的手感、
  IME、跨显示器混合 DPI、长时间远端终端会话需要人工验收。
- 首轮回归未发布远端版本；不把自动检查记录视为用户手动验收确认。

## 0.5.0 发布前复核

所有者明确要求标签 `0.5.0`、推送 GitHub，并仅发布静态单 EXE。
因此本次不构建/上传 MSI 或 ZIP，不将其验收计为通过。
目标仓库为 `0x468/ZTermy`，分支为 `main`。

- 修复源提交：`c580efb`；后续文档提交不改变二进制源码。
- Debug CTest：129/129，41.47 秒；静态 Release CTest：129/129，41.24 秒。
  日志：`build/release-050-debug.log`、`build/release-050-static.log`。
- 静态 Release 的发布静态分析日志：`build/release-050-quality.log`。
- 最新窗格关闭运行时检查退出 0，两次记录
  `Closing detached pane preserves displayed main tab: true`；
  数据在 `build/test-data/v05-detached-close-terminal-render/`。
- 上传候选：`build/release-0.5.0/ztermy-0.5.0-windows-x64-static.exe`，50,997,760 字节。
  FileVersion / ProductVersion 均为 0.5.0。
- SHA-256：`c6fd81da5ac6474c1761710849d2ca93841a093d98e701c93588307c5f71d9b1`。
- PE 导入检查仅见 Windows 系统 DLL，无外部 Qt/MSVC 运行库 DLL 依赖。
  在仅放置此 EXE 的目录中正常启动 `--window-runtime-smoke`，退出 0；
  数据隔离在 `build/test-data/release-050-single-exe/`。
- 前述真实主机、混合 DPI 及长期使用的人工验收边界仍然有效。
