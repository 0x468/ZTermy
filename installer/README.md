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
