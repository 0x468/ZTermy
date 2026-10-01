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
