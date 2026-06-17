# 工作区说明

为避免 clangd / Devicetree 等语言服务混用不同平台的编译数据库，**请每次只打开一个项目工作区**：

| 平台 | 工作区文件 |
|------|------------|
| ESP32-S3 | [`ESP32-S3.code-workspace`](../ESP32-S3.code-workspace) |
| STM32F407 | [`STM32F407ZGT6.code-workspace`](../STM32F407ZGT6.code-workspace) |
| CH32V307 | [`CH32V307.code-workspace`](../CH32V307.code-workspace) |

不要再用本文件所在的「综合项目」工作区打开整个 `can_project` 目录。

若误开了 `can_project` 根目录，已提供 `can_project/.vscode/settings.json` 作为三平台 Devicetree 兜底；仍建议改用上表中的单平台工作区，以免 clangd 混用编译数据库。

修改 Devicetree / clangd 配置后，执行 **Developer: Reload Window** 或 **Devicetree: Restart Language Server** 使 LSP 重新加载。
