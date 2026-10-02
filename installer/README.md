# Ztermy Setup 构建

此目录是产品唯一安装器配置源。修改产品版本、品牌或行为后提交 product.toml。
工具版本和摘要固定在 zinstaller-tools.lock.json；本机 SDK、staging、输出路径不提交。

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
Builder 不会自动删除它们。当前源码版本为 0.5.2，若使用其他版本已验证 staging，
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
