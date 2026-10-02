# 本地 Shell 测试隔离

2026-10-02 09:38:08（本机 UTC+8）再次出现 `clink_x64.exe 0xc0000142`。
该时间落在 Windows-only PATH 下运行的全量 CTest 内；此前包依赖检查的
受限 PATH 被错误地沿用于终端回归。检查发现集成 PowerShell 和默认本地
终端测试仍可能加载用户 profile，CMD 的生产启动参数也允许 AutoRun。
时间关联与隔离缺口已确认，不把它当成已经证明的 Clink 内部根因。

10:00:22 原生事件门禁再次抓到 Clink 错误：此时终端启动已隔离，但并行
clang-tidy 构建仍运行 CMake 自动生成的 `cmd.exe /C`。本机 HKCU AutoRun
引用 Clink。构建与终端是两条不同入口，不能只修后者就宣布解决。

## 强制规则

- CTest 的每个测试追加 `ZTERMY_TEST_ISOLATED_SHELLS=1`，不覆盖已有环境属性。
- 应用私有 smoke / benchmark 入口在创建控制器前启用同一隔离模式，后续
  本地 Shell 和第二进程继承。直接运行终端/助手测试也显式启用隔离。
- 仅测试模式使用 PowerShell `-NoProfile`、CMD `/D`、Nushell
  `--no-config-file --no-history`、Bash `--noprofile --norc`。
- 集成 PowerShell 测试使用 PSReadLine `SaveNothing`，不写入真实历史。
- 正常启动不修改用户 profile、AutoRun、Shell 参数或历史偏好。
- Windows-only PATH 仅用于检查部署依赖，不能沿用于 Shell 功能回归。
- 在 MSVC 开发环境中通过 `scripts/run_isolated_build.ps1` 构建。它先按
  preset 配置，再机械规范化当前构建树生成的 Ninja CMD wrapper 为 `/D /C`，
  同时设置并恢复进程局部 `CLINK_NOAUTORUN`。不修改源码、用户注册表或永久
  环境；每次 configure 都必须重新规范化，不能直接继续旧的裸构建入口。
- Debug 与静态 Release 的最终测试矩阵顺序运行，避免跨矩阵争用剪贴板。
- 验收读取 Windows System / Application Popup 26，新 Shell DLL 初始化错误
  判为失败。不得通过隐藏 Windows 弹窗、结束无关 Shell 或禁用用户 Clink
  来使测试通过。需要真实 profile/Clink 兼容性测试时，应单独在 Sandbox
  配置合成环境，不在开发者主机上加载真实配置。

## 复现与验证入口

通过 CMake preset 构建后执行：

```powershell
pwsh -NoProfile -File scripts/run_isolated_build.ps1 -Preset msvc-dynamic-debug
pwsh -NoProfile -File scripts/run_isolated_shell_regression.ps1 -Preset msvc-dynamic-debug
pwsh -NoProfile -File scripts/run_isolated_shell_regression.ps1 -Preset msvc-static-release
```

`-TestRegex '.*'` 可执行整个矩阵。包装脚本恢复原环境并检查新增原生错误事件，
只记录事件时间，不输出可能包含用户数据的完整事件消息。
参数回归检查正常启动不变、CMD 大小写及重复 `/D`、两种 PowerShell、
Nushell/Bash 和未知程序不变；集成 PowerShell 检查正常/隔离两种路径。

Clink 的进程局部禁用变量见其[官方实现说明](https://github.com/chrisant996/clink/blob/master/clink/app/src/loader/inject.cpp)。
生成 CMD wrapper 的 `/D` 才是避免启动 AutoRun 程序的主隔离措施，不能
只依赖 Clink 进程已经启动后再读取环境变量。
