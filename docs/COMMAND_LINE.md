# 命令行与第三方终端调用

当前启动接口不会执行任意本地命令。目录、Shell 和 SSH 连接参数可转交同一数据目录下
已运行的 Ztermy；未运行时正常启动。`--window main` 是 Ztermy 主窗口内的新 Tab，
`--window detached` 是单工作区独立窗口（内部仍可分 Pane），不是嵌入 WinSCP 的窗口。
独立窗口调用不会主动唤醒已隐藏的主窗口；需要认证或确认主机指纹时会显示主窗口。

## 本地目录

```powershell
ztermy.exe --open-directory "D:\Work" --local-shell powerShellCore
ztermy.exe --open-directory "D:\Work" --local-shell commandPrompt --window detached
```

Shell ID：`automatic`、`powerShellCore`、`windowsPowerShell`、`commandPrompt`、`gitBash`、
`nushell`、`wsl`。只允许本机可用的内置 Shell；明确指定不可用 Shell 时不会悄悄换成其它 Shell。
未指定时沿用 Windows 集成的单入口 Shell 选择，`automatic` 使用应用的默认本地 Shell。

## SSH

```powershell
# 密码认证：弹出凭据输入，不在命令行中传密码
ztermy.exe --ssh "ssh://developer@example.org:2222/srv/project" --window detached
ztermy.exe --ssh example.org --user developer --port 2222

# 私钥；有口令时输入口令，未加密私钥留空后连接
ztermy.exe --ssh developer@example.org --identity "C:\Keys\id_ed25519"

# OpenSSH 用户证书与其匹配的私钥（不是 TLS 证书或安装器签名证书）
ztermy.exe --ssh developer@example.org --identity "C:\Keys\id_ed25519" --certificate "C:\Keys\id_ed25519-cert.pub"

# 使用 Windows SSH agent，不扫描或偷偷加载其它私钥
ztermy.exe --ssh developer@example.org --auth agent

# 复用已保存主机：使用 profiles.json 内稳定的 id，而不是可能重名的标题
ztermy.exe --profile "SAVED-HOST-ID" --window detached
```

`--auth` 支持 `password`、`private-key`、`agent`。默认密码认证；提供 `--identity` 时
默认私钥认证。已保存主机的认证、代理、跳板及凭据继续使用原配置，不允许同时覆盖
主机/用户/密钥等连接身份。临时连接不自动保存为主机，不写入明文凭据。
首次/变化的主机指纹仍通过正常确认流程，命令行没有跳过校验的开关。

`--remote-directory` 是远程 **POSIX Shell** 的目录请求；路径作为单个引号参数
构造 `cd`，空格、单引号、`$()`、`&` 都不会变成额外命令。它会先于已保存的启动命令执行。
不是任意远程命令接口；Windows cmd/PowerShell 远程 Shell 不适用该目录切换语法。
SSH URL 路径可代替此参数；IPv6 地址使用 `[::1]` 形式。用户和 URL 中有路径时不要重复指定。

## 自动化凭据

`--password-file "C:\Private\credential.txt"` 可提供密码或私钥口令：本地普通 UTF-8 文件，
仅一行，最多 4096 字节，可有末尾 CRLF/LF。文件路径进入启动请求，**内容不进入 argv/IPC、
日志或设置**，由工作线程读取并用敏感字节缓冲传递。文件应限制为本人可读；Ztermy 不删除
调用方文件，临时文件由调用方清理。UNC、设备和命名管道不属于此接口。

不支持 `--password`、PuTTY `-pw` 或 URL 内明文密码；不支持 PuTTY 注册表会话 `-load`。
PPK 等不受当前 libssh2 后端支持的密钥格式需要先转换为兼容格式；启动接口不伪装 PuTTY。

## WinSCP 配置

在“首选项 → 集成 → 应用程序”的终端客户端路径填写：

```text
"C:\Users\YOURNAME\AppData\Local\Ztermy\ztermy.exe" --ssh "!@" --user "!U" --port !# --remote-directory "!/" --window main
```

改成 `--window detached` 可打开独立弹窗。使用兼容私钥时可追加 `--identity "!K"`；
使用 agent 时追加 `--auth agent`。不要追加 `!P`，并取消“记住会话密码并传给 PuTTY”。
密码认证由 Ztermy 提示输入；要免输入，可通过已保存主机/系统凭据启动，或由受控调用方
创建凭据文件。WinSCP 对非 PuTTY 客户端可能注入明文 `-pw`，本接口会明确拒绝，不会静默忽略。

WinSCP 的参数占位符及自动密码传递规则见其
[官方集成说明](https://winscp.net/eng/docs/ui_pref_integration_app)。`!@` 避免自动加入 PuTTY 的 `-load`。
本文示例按官方调用协议验证；不意味着自动兼容其它软件的 PuTTY 专用行为。

`ztermy.exe --help` 输出启动帮助；没有控制台的 GUI 调用会显示原生帮助对话框。
