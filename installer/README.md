# Ztermy Setup 构建

此目录是产品唯一安装器配置源。修改产品版本、品牌或行为后提交 product.toml。
工具版本和摘要固定在 zinstaller-tools.lock.json；本机 SDK、staging、输出路径不提交。

安装前请求退出使用 `z-series-safe-close-v1`，不是普通 `WM_CLOSE`。
Ztermy 接收 `ZSeries.SafeClose.v1` 后走托盘“退出”的完整清理流程，隐藏主窗口和
独立窗口也会退出；平时关闭到托盘的设置保持不变。安装器发送请求后仍须重新检测
进程确实消失才开始安装，不自动强制终止。旧版 Ztermy 不支持该请求时，需手动从
托盘退出后重新检测。当前锁定 SDK 已支持协议，不需要重新构建或修改安装器仓库。
运行时验收：`scripts/verify_window_restore.ps1 -InstallerExit`，以及加 `-WithoutTray` 的对照；
两者分别覆盖显示/隐藏主窗口、活动本地会话、多独立窗口、退出清理与下一次启动恢复。
详情见 [有序退出约定](../docs/adr/0036-orderly-application-shutdown.md)。

先由产品侧完成 CMake 编译、windeployqt 部署与最终模块精简，然后执行：

动态构建使用 `/MD`，当前 CMake 部署关闭了 compiler runtime 自动复制。
打包前需将对应 MSVC Redistributable 的 x64 `Microsoft.VC143.CRT` DLL
复制到 staging 根目录；不能依赖开发机系统目录已经安装的运行库。
产品配置检查主要 CRT 文件存在。完整干净系统依赖验证仍需 Windows Sandbox。

~~~powershell
$env:ZINSTALLER_SDK = 'C:\SDK\zinstaller-builder-sdk'
& "$env:ZINSTALLER_SDK\bin\zinstaller-build.exe" validate --project installer/product.toml
& "$env:ZINSTALLER_SDK\bin\zinstaller-build.exe" inspect --project installer/product.toml --payload build/msvc-dynamic-release/package/dynamic
& "$env:ZINSTALLER_SDK\bin\zinstaller-build.exe" build --project installer/product.toml --payload build/msvc-dynamic-release/package/dynamic --output build/setup
~~~

staging 不应含 smoke-data、portable.flag、调试 DLL 或 .zinstaller。
Builder 不会自动删除它们。当前源码版本为 0.5.3，若使用其他版本已验证 staging，
可用 --version 覆盖，仍会核对 PE ProductVersion。

工具锁 sdk_sha256 固定解压 SDK 文件清单，ZIP 的传输摘要单独随 SDK 提供。
维护程序与产品图标仅注入临时副本。Setup 通过 headless smoke 后才发布版本目录。
原来的 MSI、portable/static 发布职责仍由产品侧负责，不属于 Builder。
安装、升级、卸载、scope 切换和 Windows Sandbox 的人工验收仍需作为发布门禁。

2026-09-27：锁文件已更新为修复 ledger v1 兼容性的 SDK。
旧 SDK 将 manifest v2 与 ledger 版本耦合，导致从 0.5.1 升级时报 exit code 30。
修复后的 0.5.2 已在授权下实际覆盖升级本机旧安装（Upgrade 返回 0），
并核验 139 个安装文件、系统注册版本及安装后隔离启动。
本轮产物在 `build/setup-ledger-fix/Ztermy-0.5.2/`，不是旧 `build/setup/` 中的失败包。

2026-10-01：产品 descriptor 升到 schema 3，工具锁固定为支持传统 Explorer 菜单、
登录启动和升级选项记忆的 SDK。传统菜单默认开启，登录启动默认关闭。
新增字段的默认值由此处的产品配置决定，不是安装器对 Ztermy 写死。

Win11 新菜单使用 `ztermy_explorer_command` 原生 DLL 和外部位置身份包。
`scripts/build-explorer-identity.ps1` 可用 `-SigningThumbprint` 指定个人证书库身份，
签名 MSIX 并导出公开 CER；不导入信任、不注册菜单，不读取/复制 PFX 到 staging。
Publisher 必须传入真实签名证书的 Subject。私钥/PFX 不得提交 Git 或放入 payload、SDK。
按所有者要求，本机开发身份保存在项目根目录的 `.signing/`（整个目录 Git 忽略）：
`zseries-development.cer` 是可分发的公开证书，`zseries-development.pfx` 是无密码私钥文件，
`identity.json` 记录 Subject、指纹、个人证书库位置和到期时间。
该目录只允许当前用户、SYSTEM 和管理员访问；PVE 同步由所有者负责，不能公开共享 PFX。
安装器的 forbidden 清单额外拒绝 `.signing/**`、PFX 和 P12 文件。

~~~powershell
# 重用同一身份；不会自动导入信任库。无密码导出必须明确指定开关。
pwsh -NoProfile -File scripts/new-development-signing-certificate.ps1 -ExportPrivateKeyWithoutPassword
~~~

schema 4 的通用安装器已支持当前用户身份包的注册、升级注销、回滚和卸载。
产品配置声明签名 MSIX、公开 CER 和 SHA-256；升级先注销再替换外部 DLL，
失败回滚先恢复旧文件再恢复旧注册。旧 manifest/ledger 1–3 和 journal 1–2 可读。
新菜单默认关闭，没有独立的“允许信任”开关。开启新菜单后，后台先做只读检查；
已经信任则直接继续，未信任才在安装开始前显示确认页，列出 Publisher、完整 SHA-256、
本机 TrustedPeople 范围及其他用户影响。选择“继续并授权”后仍通过 Windows 管理员授权；
只导入校验过的非 CA 公开代码签名证书，绝不导入 Root。
选择“返回修改选项”不创建事务、不卸载旧版，保留当前路径和选项。授权不记住到下次安装。
共享证书信任卸载时保留。已有可信身份无需重复授权；未授权的新电脑仍由 Windows 拒绝并回滚。
当前身份包只支持当前用户安装；所有用户安装需关闭新菜单，避免注册到 UAC 管理员账户。

2026-10-01 合并菜单授权流程的测试包：`build/setup-menu-consent-20261001/Ztermy-0.5.2/Ztermy-0.5.2-Setup.exe`。
Debug / static Release 各 130 项 CTest、全量 static Release clang-tidy、格式/QML 门禁通过。
实际 Sandbox 安装验证 140 个文件哈希、传统目录/背景/驱动器菜单、可选登录启动、
重装和卸载，并运行安装版系统集成 UI/native smoke。未授权身份明确收到 0x800B0109
并恢复此前 ledger，不添加信任；单独授权后，公开证书加入 VM 本机 TrustedPeople，
新菜单身份包注册及真实 COM 激活通过。无再次授权的重装、版本变化的后端升级通过；
故意改变新包 Publisher 并修改受管文件，使升级在注册阶段失败，旧文件、ledger 和
旧身份注册全部恢复。卸载删除新旧菜单、启动项和受管文件，保留未知文件和共享信任。
安装器选项页面和条件确认页已经用 Windows MCP 截图审查：不再提供独立证书开关，
只有新菜单缺少信任时显示授权页，实际范围和完整指纹可见。只读原生预览使用
`identity_consent_preview` 无测试 harness 的主线程入口，不启动安装执行器；普通主机安全跳过。
验收中的证书导入和系统注册仅在 Sandbox 执行，不对宿主机执行信任导入或删除。
证据：`build/system-integration-sandbox/results/installed-integration-trust-result.json`。
应用内安装器集成维护入口尚未实现；实际登录和多显示器行为仍未验收。本包不是正式发布。

Sandbox 验收脚本必须明确按 UTF-8 读取安装记录，并为每次 UI/native smoke 创建独立
数据目录，避免旧截图目录里的设置恢复窗口状态。结果包含开始时间、Setup SHA-256
和运行时证据路径；不能用旧结果文件或编译成功代替本次安装验收。
合并流程的最终干净 Sandbox 验收开始于 `2026-10-01T14:47:58Z` 并通过所有断言；
交付 Setup SHA-256 为 `a20278bf1ae856bded39536d206a5ad4fedba6d4133003bf83b630d84ab901c2`。
本轮安装器相关 65 项测试、Clippy、格式检查通过；3 项会修改系统的测试留给隔离验收。

`scripts/test-explorer-identity-sandbox.ps1` 仅允许在带验收映射的 WDAGUtilityAccount 中运行：
临时私钥只在 VM 内生成，分别验证未信任拒绝、可信签名注册、原生 COM 激活、
中文/空格/元字符目录传递与包卸载。此测试以独立 argv 接收程序隔离扩展协议，
不代替完整 Qt 安装包验收。本轮另用 Windows MCP 在干净 Sandbox 的真实
Explorer 中验收了选中文件夹与文件夹背景两种新菜单：菜单显示及点击后的目录参数均通过。
当前 Sandbox build 26100 上 CurrentUser/TrustedPeople 不足以完成 Appx 部署，
LocalMachine/TrustedPeople 才通过。安装器必须明确显示所需信任范围和权限，
不能自动提升到 Trusted Root，也不能把默认开启新菜单当成证书信任同意。

## 2026-10-02：可配置 Shell 菜单

应用设置的「Windows 集成」提供单一入口或一级 Shell 子菜单，只展示本机检测到的
可用 Shell，可选择单项入口使用的 Shell 或子菜单成员；未保存的设置草稿不会应用。
传统菜单与 Windows 11 菜单共享原生 `IExplorerCommand` DLL，设置只更新当前用户、
当前安装目录的有界快照，不改注册表或证书。开发/便携/自定义数据目录运行不覆盖安装版菜单。

产品 descriptor、manifest 和 ledger 采用 schema 5；旧 1–4 可读。新增 typed handler
声明由安装器管理 CLSID/InprocServer32 与三个目录菜单位置，沿用归属检查、回滚及卸载。
SDK 固定到 `97074544270bfdb0833de832b249d1985ced7ab596a882049b6326d909636a0d`。
Win11 的签名 sparse identity 仍是安装器的独立可选集成；仅改变菜单样式无需重新安装。

本轮 owning-module Rust 测试 73 项、Clippy、SDK 校验通过；新的 Windows Sandbox 验证
传统菜单旧版升级、失败回滚、卸载，以及真实签名身份/COM 子菜单枚举与调用。
只枚举配置的 Command Prompt，目录含中文、空格和 `& % !` 仍为一个字面参数，
`--local-shell commandPrompt` 原样传递。沙箱测试结果见
`build/system-integration-sandbox/results/menu-transaction-20261002.txt` 和 `identity-result.json`。
Qt 设置页键盘切换、草稿恢复、保存及截图验证见
`build/system-integration-settings-smoke-verified-20261002/`。
以上扩展协议阶段尚未生成新的完整 Setup；扩展协议测试不等同整包安装验收。

2026-10-02 整包验收补充：带安装界面的测试包为
`build/setup-shell-menu-20261002/setup/Ztermy-0.5.2/Ztermy-0.5.2-Setup.exe`，
大小 47,407,743 字节，SHA-256 为
`2f6d9078ec7cad1379a65449bc00093b4f639f92d6b5ba01a915f48fb7dc9e03`。
该包包含当前工作区代码，版本仍为 0.5.2，不是正式发布，也未上传 GitHub。
Debug 和静态 Release 各 130 项 CTest、全量静态 Release clang-tidy、格式/QML、
翻译和资产门禁通过。Debug 标题栏测试起初被旧测试状态拖慢而超时；保留并移走其
专用历史数据后，全量重跑通过，未放宽超时或绕过断言。

新鲜动态 Release 部署补齐 x64 CRT，以 Windows-only PATH 启动验证通过；
菜单身份包使用已有开发身份签名，只将公开 CER 放入 payload，未修改宿主机信任。
干净 Sandbox 整包验收从 `2026-10-01T23:59:00.1333742Z` 开始，结果摘要匹配上述
Setup 哈希：140 个受管文件哈希、传统 handler 注册、未授权身份拒绝与回滚、
授权后新菜单注册、重装、版本升级、注入失败后旧文件/ledger/身份恢复、安装版
快捷键与菜单设置 UI/native smoke、卸载全部通过。卸载保留用户未知文件和共享信任；
只关闭本轮 Sandbox，未在宿主机实际安装。证据为
`build/system-integration-sandbox/results/installed-integration-trust-result.json`。

## 2026-10-02：Shell 图标与外部启动最终验收

验收范围更正（2026-10-03）：下述 10-02 包的 Silent/后台安装和安装后产品 UI
验收通过，但当时没有验收最终 Setup 的默认安装界面启动。Owner 随后报告的
启动失败证实这是一项遗漏，不能将上述证据表述为安装器前端已通过。

原生 Shell 子菜单使用各 Shell 的内置图标，一级入口保留应用图标；资源来自
已接受的图标依赖。命令行支持目录/指定 Shell、SSH URL 或已保存主机、认证
参数和主窗口/独立窗口启动，详见 `docs/COMMAND_LINE.md`。IPC 保留旧版本
兼容性；不支持明文密码 argv，凭据文件有界异步读取，临时连接不写入主机库。

本轮最终带界面 Setup 位于
`build/setup-launch-final-20261002/setup/Ztermy-0.5.2/Ztermy-0.5.2-Setup.exe`，
大小 47,533,701 字节，SHA-256 为
`b50510a6b7de01b6852d0c52da02801e00c84b286de56a948e5739097e993b0a`。
staging 的主 EXE 摘要与最终动态 Release 完全相同，未包含 PFX/P12、私钥、
`.signing`、portable.flag 或用户数据。本包仍为 0.5.2 本地测试包，未发布。

- 最终 Debug 全量 CTest 130/130，89.55 秒；静态 Release 130/130，75.61 秒。
  归属及相邻六项测试也通过。两套最终矩阵顺序运行。
- 全量静态 Release clang-tidy、C++ 格式、QML 质量、翻译/图标门禁通过；
  运行时头文件最后增量改动后补跑 main 翻译单元分析、格式和打包检查。
- 静态 portable ZIP 与 MSI 已重新生成，MSI 结构契约通过；WiX ICE 沿用
  已有构建缓存的跳过配置，未将 ICE 验证计为通过。
- 真实回环 SSH 验证密码、加密私钥、OpenSSH 用户证书、正常主机密钥确认、
  远端目录字面量转义及独立窗口。Windows MCP 实际桌面启动的系统集成
  smoke 通过，截图确认终端内容，避免把首帧空白当验收证据。
- 最终 Sandbox 验收开始于 `2026-10-02T02:37:31.6974422Z`，结果匹配上述
  Setup 哈希：140 文件哈希、传统菜单、未信任拒绝与回滚、授权后身份注册、
  重装、真实版本变化升级、注入失败后恢复文件/ledger/注册、安装版原生
  窗口/设置/IPC、真实 COM 和卸载全部通过；未知文件与共享信任保留。
  证据：`build/setup-launch-final-20261002/sandbox-verified/results/installed-integration-trust-result.json`。
  所有证书信任和系统注册操作仅在 VM 内执行。

Clink 错误不能只依赖一次测试未复现：现已隔离测试 Shell 的 profile/AutoRun，
并通过 `scripts/run_isolated_build.ps1` 规范化 CMake/Ninja 的 CMD wrapper 为
`/D /C`。回归脚本将新增 Windows Shell DLL-init 错误判失败。自本轮构建入口
隔离后，后续全量分析、编译、回归和实际桌面验收无新增 Clink `0xc0000142`。
正常用户启动配置不变；具体复发经过及强制规则见 `docs/testing/SHELL_TEST_ISOLATION.md`。

现存代码结构预算门禁仍失败：main 4900（基线 4898，但 HEAD 原为 4918，本轮
减少 18 行）、TitleBarRuntimeSmoke 418、WorkbenchRuntimeSmoke 665、
SettingsPane 3466。未扩大基线或绕过该门禁，不宣称所有结构检查已通过。
WinSCP 配置模板基于官方调用契约，未计为 WinSCP GUI 实测；跨屏混合 DPI
及长时间真实远端使用仍需要设备验收。

## 2026-10-03：安装界面与占用扩展更新修复

Owner 的启动失败来自此前重装的第 279 个零基操作：
`ztermy-explorer-command.dll` 提交、回滚均拒绝访问。不是显卡或材质配置错误；
旧安装器在创建界面前强制恢复该事务，使硬件/软件 child 都退出 1，根因日志又
没有被带入启动诊断。

更新安装器 SDK 锁并修复上游：打开选项页不修改安装和 journal；显式开始安装
才进入既有恢复门禁。同字节恢复跳过替换；已加载的旧 EXE/DLL 可改名至带
ownership marker 的事务目录，提交新文件；释放后再次清理。无强制结束 Explorer、
宿主机证书/注册表变更或删除失败 journal。跨卷和不可改名锁仍按真实错误失败，
不会伪造安装成功。旧 Explorer 对象可继续使用旧映射，直到自然卸载。

新本地测试包（仍为 0.5.2，未发布）：
`build/setup-mapped-images-20261003/setup/Ztermy-0.5.2/Ztermy-0.5.2-Setup.exe`，
47,543,681 字节，SHA-256
`1e0fd904b4f14d1306ed3c532baf756c3511f707bdcf7eec76d51b2d29a2b9d2`。
Qt 主程序代码和 EXE 未改变，摘要仍为
`c0856676b435bbd55021697ff3bcc09604d5f6b1ab3f2db78f017113076ead91`，
沿用上一轮同代码的 Debug/静态 Release 130/130 与全量静态分析证据；本轮
不将其描述为重跑。安装器 workspace 134 项通过、3 项跳过，Debug/Release
clippy 全目标、格式检查通过。包校验、锁校验和 headless smoke 通过。

已在 Owner 桌面实际打开最终 EXE 的默认选项页并截图：没有点击安装，两份
失败 journal 哈希不变。Sandbox 实际加载产品扩展并添加有效 PE overlay，
重现旧包退出 30、rollback-failed/recovery-required；新包实际界面就绪且
journal 不变，恢复安装 0、140 文件哈希一致、重装 0、释放后退休文件清理和
卸载 0。证据置于该包的 `sandbox/results-recovery-passed/`。前两轮测试脚本
因轻量 bootstrapper/监督器 PID 匹配、PS 5.1 无 BOM 中文正则误判 ready，
保留失败记录，改为新日志范围的 ASCII ready 检测。完整矩阵第一次因少复制
`package.toml` 夹具停止，保留错误，不计作产品验收通过。

复现入口：`scripts/test-installer-ui-recovery-sandbox.ps1`（仅 WDAG 用户且存在
专用 `probe.ps1` 映射）。输入目录需要旧失败包 `Old-Setup.exe`、新
`Ztermy-0.5.2-Setup.exe`、新 `package.toml`、本脚本与既有完整集成脚本。
先 `-Phase Prepare`，实际观察安装界面，再 `-Phase Finish`，最后运行完整
`test-installed-system-integration-sandbox.ps1`；结果仅写 VM 的专用映射。

最终自包含脚本复跑的 `sandbox/results/installer-ui-recovery-result.json`
亦通过。完整矩阵开始于 `2026-10-03T02:49:25.8474492Z`，结果摘要匹配新包：
首装/重装/升级/安装版原生 UI/卸载均退出 0，未信任拒绝与注入升级失败均
退出 30，原文件/ledger/菜单身份恢复、140 文件校验、未知用户文件和共享
信任保留通过。证据为 `sandbox/results/installed-integration-trust-result.json`；
证书与系统注册均仅在 VM 中修改，宿主机无新增 Clink/DLL-init 弹窗事件。
卸载后的干净 VM 另以 `--renderer software` 打开最终 EXE，ready 和真实
安装选项页截图通过，见 `sandbox/results/software-ui-ready.json`。上游修复
提交为 `a92b459`，未推送；本轮未改变版本号或发布 Release。

## 2026-10-03：托盘安全退出验收包

本次重新编译动态 Release 并部署 Qt/CRT，不沿用旧主程序。新包位于
`build/setup-safe-close-20261003/setup-verified/Ztermy-0.5.2/Ztermy-0.5.2-Setup.exe`，
47,542,713 字节，SHA-256
`f6e6e96148aab0e079cfedb16b3e6cc20089e81583f873988deff68bde1d056a`。
包中主 EXE 与本次动态编译摘要一致：
`100027a2f2763e6a98fe5185d4ef245d411281dc30b7ab8b66c7bb66d14ef0f0`。
版本仍为 0.5.2，仅本地验收，未推送或发布。

Debug / 静态 Release 全量 CTest 分别 130/130；全量静态 Release clang-tidy、
C++ 格式、98 个 QML 质量、2401 条翻译及资产门禁通过。Debug 首轮标题栏测试
在并发分析负载与旧测试状态下超时；保留现场并移走专用测试状态后，原并行数、
原超时全量重跑通过，不将首轮计为通过。静态 portable ZIP / MSI 重生成、MSI 结构契约
通过；沿用 ICE 跳过配置，不宣称 ICE 验证通过。

新 payload 的 Windows-only PATH 启动通过；真实安全退出消息验证有/无托盘 ×
主窗口显示/隐藏四组合，包含三个干净 CMD 会话、多独立窗口、无残留子进程和重启拓扑恢复。
测试数据已移出 payload，无 PFX/P12、私钥或用户数据。

干净 Sandbox 整包矩阵通过且摘要匹配新包：140 文件、首装/重装/升级/卸载、
未信任身份拒绝与回滚、授权后身份注册、注入升级失败后原文件/ledger/菜单身份恢复、
安装版原生 UI 通过。证据：
`build/setup-safe-close-20261003/sandbox/results-verified/installed-integration-trust-result.json`。
宿主机未安装、导入信任或变更系统注册。

最终 Setup 的默认界面在 VM 中发送 ready，但真实界面的“请求安全退出”点击验收
被自动审批拒绝：Windows MCP 无法将 VM 内的坐标与可访问按钮关联。未绕过拒绝，不将
原生消息测试或 ready 记录冒充鼠标验收。随后 Owner 于 2026-10-03 确认整包实机
手动验证通过，并授权提交本轮修复；这份人工验收与上述自动测试证据分别记录。
首次输出目录发布拒绝访问和首次 VM 测试读取活动日志共享冲突的记录均保留；
新绝对路径输出与共享读取的整包重跑通过。
