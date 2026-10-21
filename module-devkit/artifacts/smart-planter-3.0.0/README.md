# 本机比赛模块产物

- `signed-package/`：Pi 当前公钥可验证的模块包，包含 ARM64 插件及所有 QML 页面。
- `firmware/module_firmware.bin`：已烧录模块应用。
- `firmware/partition-table.bin`：本次构建分区表，现场已与原设备比对一致；本次没有刷写它。
- `validation.json`：哈希与验证范围。

适用目标：序列号 `5CF8047843` 的现有 ESP32-S3 模块板、16MB Flash、0x10000应用起址、0x177000应用分区。不可刷控制桥或未核对的其他设备。

此固件带有本机私有 provision 配置，目录仅供本机比赛联调，已加入 gitignore，不公开分发。SDK示例源码不含这些私有配置。

完整功能与未接线的供电检测限制见 `../../examples/smart-planter/README.md` 和 `VALIDATION.md`。软件休眠不是物理断电。
