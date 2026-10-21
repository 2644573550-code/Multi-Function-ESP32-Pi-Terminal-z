# ESP、通信与 C ABI

## 先读与安全

读两个工程的 `main.c` / `Kconfig.projbuild`、`TransportPolicy.h`、`ConnectionManager.cpp` 和相关传输代码。
烧录、Flash 读取、修改私有 sdkconfig、停止 Pi 服务等需当前任务明确授权；核对真实 USB by-id、芯片、容量、分区并验证备份。
不把产品 token、密码、私钥或 Flash dump 写入 skill。默认配置的空 token 是待私有 provisioning，不可填共享测试凭据。

## 固定传输模式

- 模块启动上报 `module-transport-v1`，模式只接受 `fixed_ble` 或 `fixed_hotspot_lan`。
- 模块配置只更新 host profile 的传输/包通道字段，不能覆盖身份、token、地址等产品配置。
- LAN 的模块配置仅 `package_transport=data`；BLE 可选 `data` 或 `hotspot_lan`。
- host profile 兼容 `package_transport=http`，不要错误声称模块握手也接受该值；以 `applyModuleConfig()` 与 `fromProfile()` 分别判断。
- PBR1 是控制面，桥接 ping/status 不是业务命令。业务使用认证 BLE/TCP；不跨模式自动回退。
- BLE + 热点 HTTP 只临时租用热点下载包，业务仍留 BLE。按需持有/释放热点，不在启动/状态查询时启用。
- UART 打开、物理 attach 或 TCP 连接都不等于 ready。校验协议、身份、token、序号和 `session_ready`，再验证真实数据。

## 包和插件

- 插件包可由 ESP 固件嵌入并经 BLE/TCP 包 API 或受限 HTTP 提供；ESP 不执行主机 `.so`。
- `package_tool.py` 用 Ed25519 校验 descriptor，核内容哈希、文件集合、路径、大小、ELF 架构、Qt major、ABI 和 `synchronous-stop-v1`。
- 不允许绕过信任/签名/架构验证。缓存存在也要校验；不可任意路径解压或接受符号链接。
- `ModuleManager` 使用 `QLibrary::ResolveAllSymbolsHint`，校验所有 `pi_module_*` 导出与嵌入 manifest。
- 跨 DSO 的契约是 `pi_module_api.h` 的 C ABI；`IModulePlugin.h` 只是可选 C++ 辅助接口。
- 关闭经 stop/session_close、QML 页面释放、插件同步 detach/stop/destroy，再卸载库和释放传输/热点。
- 同一文件的多实例共享 QLibrary 原生状态，必须等所有实例清理完成才卸载。最小化不关闭业务会话。

## 构建

先 export ESP-IDF，再读 `build_scripts/build_esp32.sh` / `build_pi_module_variant.sh`。
BLE、LAN 使用不同构建目录；变体脚本要求私有 `PI_MODULE_SDKCONFIG` 和签名包 `PI_MODULE_PACKAGE_DIR`，输出私有 sdkconfig 权限 0600。
`build_all_esp32.sh` 在缺少这些输入时会跳过模块变体，不代表已经构建全部固件。不能用编译成功冒充烧录或现场验证。

## 板级事实以源码为准

控制桥 UART0 当前为 115200、GPIO43/44；LinkIt 在 UART1，pin 定义可能来自工程外组件，修改前追踪 include/CMake。
传感器默认 BME SDA/SCL 为 38/39，BH1750 为 4/5，BH 有探测/备选逻辑，不能只看默认宏推断实际接线。
BMP280 不提供湿度；无效读数必须通过 valid/null 表达，不伪造数据。间歇 I2C timeout 仍需实体总线/电源验证。
