# 历史设备索引：不是在线状态

以下只用于查找候选设备。连线、IP、账号、系统版本、进程和部署目录必须在当前任务现场复核；不据此直接刷写、重启服务或覆盖文件。

| 项目 | 历史索引 |
| --- | --- |
| PC 仓库 | `/home/axe665/桌面/Multi-Function-ESP32-Debugger-master` |
| 控制板 / 模块板 | ESP32-S3 USB 序列号 `5CF8045563` / `5CF8047843` |
| Pi | `raspberrypi@192.168.77.1`；曾为 Debian 12 aarch64、Qt 6.4 |
| Pi 仓库 | `/home/raspberrypi/Multi-Function-ESP32-Debugger-master` |
| 用户服务 | `pi-module-terminal.service` |
| 历史安装候选（2026-10-18 核过） | `~/.local/opt/orange-pi-terminal/20261018/`；入口为 `bin/orange_pi_terminal`，仍需现场核 service/exe/maps/SHA256 |
| 管理网 | Pi `192.168.77.1/24`、PC `192.168.77.2` |

启动桌面预览时使用当前有效 DISPLAY / XAUTHORITY，不把开发机曾用 :1 与 Pi 曾用 :0 当成无条件通用值。
配置和凭据保留在其私有文件中，不复制进此表或日志。
2026-10-18 的历史检查报告曾保存在两端 `~/esp32_workspace/checkpoints/deploy-20261018/`；不代表已现场复核或后续主机源码已同步。有效设备发布记录不等于应删除的旧程序/源码归档。
