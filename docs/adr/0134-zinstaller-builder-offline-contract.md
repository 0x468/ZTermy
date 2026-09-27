# ADR 0134: 离线 ZInstaller Builder 接入

状态：已实现构建接入，Windows Sandbox 发布验收待执行。

产品侧负责最终动态 staging 的编译、部署和精简；安装器侧通过锁定 SDK 的统一 CLI
完成 manifest、审计、维护程序注入、压缩、smoke 和原子发布。唯一产品配置是
installer/product.toml，工具锁提交到同目录。本机 staging/output 不进入配置。

不提供构建 hook，不在线下载工具，不负责签名、上传或 portable/static。
产品主程序图标继续用于快捷方式和系统应用列表；Setup PE 保持 ZInstaller 图标。
旧 MSI 迁移显式配置 UpgradeCode，运行时需确认，不在构建时操作旧安装。

此接入不替代尚未被统一 CLI 完全覆盖的 MSI/portable 发布门禁。
