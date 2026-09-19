# UI V2 设计系统交付记录（分支 `ui/v2-design-system`）

状态：六章全部落地并通过全量自动验证；等待所有者人工验收与合并决定。未合并，未推送。

计划、各章进度与基准细节见 [../UI_V2_PLAN.md](../UI_V2_PLAN.md)；不把本记录当作所有者已验收的证明。

## 候选身份

| 字段 | 值 |
| --- | --- |
| 日期 | 2026-09-19 |
| 分支 / HEAD | `ui/v2-design-system` @ `5ab2aa8`，领先 `main`（`713ee66`）32 个提交 |
| 二进制源提交 | `7619587`（`5ab2aa8` 仅改文档） |
| 版本 | `0.4.6` |
| 平台 | Windows 11 25H2 x64，MSVC 14.44，Qt 6.8.3 msvc2022_64 动态链接，D3D11 |
| 二进制 | `build/msvc-dynamic-release/ztermy.exe`，12,819,456 字节，2026-09-19 06:38:47 |
| SHA-256 | `1c2c631ff45fceb81d68eba3ef28c08e3a5777da40aa979558b3b67664687b2c` |
| 部署包 | `build/msvc-dynamic-release/package/dynamic/`，132 个文件，111.5 MB；其中 `ztermy.exe` 与上者逐字节一致 |

`build/` 不入库；以上哈希与源提交共同标识交付物。

## 交付范围

| 章 | 内容 | 契约 |
| --- | --- | --- |
| 1 | 材质只留在标题栏与终端工作区，内容页、弹层、菜单、提示不透明；持久化特效层级 `full / reduced / off`，跟随 Windows「减少动画」 | ADR 0120 |
| 2 | `AppSurface` 层级库（圆角、填充、发丝线、阴影），移除硬编码圆角 | UI_DESIGN_SYSTEM.md |
| 3 | `Motion` 单例与角色化过渡（feedback、colour、relocate、enter、exit、reveal），层级可缩放或关闭 | UI_DESIGN_SYSTEM.md |
| 4 | `Theme` 拆为终端调色板与 chrome 皮肤；十个内置主题、JSON 自定义主题、Windows Terminal / Ghostty 导入、带悬停预览的选择器；运行中会话即时换色 | ADR 0121 |
| 5 | 设置页分组导航、搜索、逐行重置，外观页以主题条为首 | ADR 0122 |
| 6 | 标题栏 Tab、chrome 动作、标题按钮、窗格头、拖拽预览与落点指示统一到共享库 | ADR 0123 |

截图证据：`docs/design/ui-v2/ch1`（2）、`ch2`（2）、`ch4`（2）、`ch5`（4）、`ch6`（7）。第 3 章为动效，无静态截图，证据为运行时 smoke。

## 交付阶段的修复

- `dcbe1d5`：`saveApplicationSettings` 从参数重建整个设置结构，从不复制 `effectsTier` 与 `terminalTheme`，每次设置页 Apply 都把终端主题重置为默认。改为从已存设置出发只覆盖页面编辑的字段，新增 `app-controller` 回归测试。
- `91e4cc6`：终端渲染 smoke 在 Release 下约十分之一失败。根因是合成点击在按下与松开之间转了事件循环，脱离窗口刚从最小化被 present 时 Windows 在该间隙投递 `WM_MOUSELEAVE`，Qt 清掉 MouseArea 的 hover，松开不再算点击。产品代码无问题；按下与松开改为背靠背投递后连续 12/12 通过。
- `7619587`：分支此前未跑全量 `clang-tidy` 门禁。只在新代码上失败：内置主题与 `SurfaceAlphas` 的聚合初始化、图标缓存预算的隐式加宽、`qreal` 到 `float` 的窄化，以及两处测试（`qsizetype` 计数、`find()` 结果的未检查访问）。全部修复且无行为变化；主题字面量逐个比对一致。`main.cpp` 4778 → 4739 行。

## 自动验证与证据（最终一轮，`build/_verify_final6.log`，07:17–07:32）

| 门禁 | 结果 | 证据 |
| --- | --- | --- |
| Release / Debug 全部默认目标 | 通过，0 警告 | `build/_build_release_final6.log`、`build/_build_debug_final6.log` |
| C++ 格式、QML 格式与 qmllint | 通过 | `build/_quality_final6.log` |
| Release CTest | 129/129，113.4 s | `build/_ctest_release_final6.txt` |
| Debug CTest | 129/129，118.8 s | `build/_ctest_debug_final6.txt` |
| 隔离动态部署 smoke | 通过 | `build/_deploy_smoke_final6.log` |
| 六个真实窗口 smoke（layout、keyboard、title-navigation、terminal-render、lifecycle、window-appearance） | 全部退出 0 | `build/_smoke-final6/<检查名>/logs/ztermy.log` |
| 全量 clang-tidy，303 个翻译单元，警告即错误 | 通过 | `build/_tidy_final6.log` |
| 代码健康门禁（预算、依赖方向、窗口状态归属） | PASS | `scripts/code_health_report.ps1 -Check` |

真实 SSH 主机未配置，`ssh-real-host`、`port-forwarding-real-host`、`ai-agent-real-host` 在用例内部跳过，不算远程验收。终端渲染 smoke 本轮读数：completion 1545 ms，心跳最大间隔 16 ms，paint P50/P95 2 ms，上传 397.3 MB，脱离窗口标题按钮往返 `restore: true`；外观 smoke 的 ADR 0120 表面契约 `acrylicContract: true`。

## 性能基准（Release，acrylic，1120×800，DPR 1，各 24 次热身运行，取中位数）

| 指标 | 同日 `main` 713ee66 | `ui/v2` | 变化 |
| --- | ---: | ---: | --- |
| completionMs | 1580 | 1678 | 一个 100 ms 搜索刻度 |
| 心跳最大间隔 ms | 18.5 | 18 | 持平 |
| paint P50 / P95 / max ms | 2 / 2 / 7.15 | 2 / 2 / 7.22 | 持平 |
| 上传 MB | 405.5 | 425.3 | +5 % |
| 快照更新 / 帧交换 | 145 / 301 | 151.5 / 302 | 持平 |

completion 被 100 ms 标记搜索量化：`main` 有 16/24 次落在 1570–1595 ms 刻度，分支 10/24。同一构建上特效层级 `off` 为 1582 ms、`full` 为 1684 ms（各 5 次种子运行，406.6 vs 429.5 MB），残余开销在层级门控的 chrome，终端绘制路径没有变慢。今日 `main` 也复现不出 CHANGELOG 09-18 的 1471 ms / 366.5 MB，因此证据规则按同日 `main` 对比。原始运行数据在 `build/perf-evidence/ui-v2/` 与 `build/_perf_ab/`（不入库）。

## 已知限制与合并前事项

- paint P95 落到 4 ms 桶的次数分支 11/24，`main` 4/24；P50 与 max 不变。
- 合并前建议：在输出突发期间剖析 `full` 层级 chrome（标题栏颜色 Behavior、页面 reveal），并决定电池供电时是否默认 `reduced`。
- qmllint 对 `Main.qml` 的 `startCopyMode` missing-property 警告在 `main` 上同样存在，非本分支引入。
- 只构建了 `msvc-dynamic-release` 与 `msvc-dynamic-debug`；静态 Release、安装包与发布包不在本次目标内。
- 窗格头默认隐藏，与合并前一致。

## 所有者第一轮反馈的修复（2026-09-19，`b9d8561`..`ccb7c68`）

| 反馈 | 根因 | 修复 | 证据 |
| --- | --- | --- | --- |
| 部分主题下会话小工具栏（IP、资源）看不见 | 工作区填充跟随终端调色板，文字与图标却取 app 皮肤的墨色；深色皮肤 + 浅色终端主题时同色 | `Theme.workspace*` 墨色族跟随终端调色板明暗，`AppIconButton.onWorkspace` 选用；工具栏、窗格头、遥测条、滚动条改用 | `docs/design/ui-v2/review-1/before-*.png` 与 `dark-skin-solarized-light-split.png` |
| 设置左栏与新字符串没有中文 | `qsTr` 上下文按文件划分，移入 `SettingsCategoryRail` 的字符串失去 `SettingsPane` 的译文；新字符串从未跑 lupdate；门禁只检查已有条目 | 重新生成目录（+91 / −14，0 未完成）；`verify_translations.ps1` 对源码跑 lupdate，缺失即失败，`ctest translation-catalog` 接入 `Qt6::lupdate` | `ctest -R translation-catalog` 通过 |
| 新建 tab 不切换、跳回上一个终端 tab | 先切页再建 tab，切页把焦点给旧视口，旧视口的焦点变化又激活旧窗格 | 先建 tab 再 `activateMainTerminal` | 标题栏导航 smoke 从主机页新建并断言 `activeTerminalTabId` |
| 分割线上没有拉伸光标 | 全窗口窗格拖拽捕获层只是 `enabled: false`，Qt 光标查找只跳过不可见项，`MouseArea` 永远带箭头光标 | 改为 `visible` 门控；`SplitView` 把手本已带分割光标 | resize smoke 断言导航把手与两个分割把手的窗口光标，12/12 通过 |
| 终端与工具栏割裂；单窗格有蓝紫边框；多窗格四面包边压住窗口边 | 叶子矩形画 1–2 px 边框与圆角，工具栏底部有发丝线 | 去掉边框、圆角与视口内缩，去掉发丝线；多窗格时把强调色放到活动窗格旁的分割线（`AppSplitView.emphasized`） | `review-1/ztermy-dark-single-pane.png`、`ztermy-dark-split.png` |

本轮复验：Release 全量 CTest 129/129，`ztermy_format_check`、`ztermy_qml_quality_check`、全量 clang-tidy、代码健康门禁 PASS；八个真实窗口 smoke（含 resize-interactions、pane-scrollbar）退出 0。多窗格强调方式的其他候选见本轮回复，等所有者定夺。

## 所有者人工验收（未执行）

- [ ] 在 acrylic / mica / solid 下切换工作台、主机、设置、AI 抽屉，确认只有标题栏与终端工作区透出材质。
- [ ] 在设置 → 外观切换内置主题并导入一个 Windows Terminal 或 Ghostty 主题，确认运行中的终端即时换色、重启后保留。
- [ ] 把特效层级切到 `reduced` 与 `off`，再打开 Windows「减少动画」，确认动效与阴影按层级退化。
- [ ] 在设置页搜索并逐行重置，确认只重置该行。
- [ ] 拖出窗格成独立窗口，最大化、最小化、从任务栏恢复，再拖回主窗口。
- [ ] 8 个以上终端 Tab 下的溢出菜单、关闭反馈与拖拽预览。
