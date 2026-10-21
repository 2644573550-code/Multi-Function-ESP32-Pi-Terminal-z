# 桌宠彩蛋与运行设备转换

## 接入与边界

- 备选 `pi-terminal-Arkpets` 与项目 `pi_terminal` 可处于不同源码状态；先核对当前工作目录及关键文件，不能把一边测试结果直接归到另一边。入口与限制见 `README-Arkpets.md`、`docs/arkpets-local.md`。
- `Main.qml` 的会话级开关默认 false；五次点击“显示与个性化”标题，相邻不超过 1.8 秒，弹出启动/关闭与管理菜单。标题 MouseArea 同时处理原生双击。关闭时 Loader 卸载 DesktopPet，管理仅改偏好，不启动人物。
- `DesktopPet.qml` 管行为/重力/拖拽，`PetSpriteItem` 是固定透明画布：OpenGL 下每帧在 FBO 中清透明并完整绘制当前帧，软件后端走完整 Image 绘制；每动作偏移在画布内应用，根节点不因动作切换移动，alpha 命中映射回原图集。不要恢复多 AnimatedSprite 或仅保留 QSGSimpleTextureNode/sourceRect 的方案；Pi 实拍曾复现残像。只覆盖终端内部普通页面，不覆盖系统其他窗口；安全模态框优先，全屏视频隐藏任务栏时人物隐藏。
- 移动方向约定 `direction > 0` 向右、`< 0` 向左；源角色朝右，仅向左时镜像。回归应验证移动方向和 facing/mirrored 映射一致。
- `PetManager.qml` 角色→模型 list-detail；宽屏双栏、窄屏返回导航，Theme 风格、无标题应用图标、非模态且不遮任务栏。角色栏按角色聚合已安装模型，并提供“已下载角色/未下载角色”筛选；模型栏只负责模型分类、搜索与状态。已下载所选模型展示待机动作图集预览，并注明当前是图集预览而非实时角色渲染。使用现有 FloatingKeyboard，出现键盘时让出区域；下载授权确认仍为模态。运行时启用待机、行走、点击互动及模型支持的坐下/躺下动作。

## 大小、质量与转换

- 角色大小偏好 50%～200%、步进 5%，小窗口只限制实际比例，不覆盖存储偏好。按待机可见高度中位数归一化。参考 Ark-Pets 的固定画布/FBO 两阶段渲染：加载时测量动作轮廓，计算每动作画布内偏移和透明留白；动作切换只改变画布内绘制位置，根节点保持不动，每帧清除完整透明画布后重绘。不改写缓存，旧图集也重新测量。新方案已在软件及主机 OpenGL 回归；Pi 部署后需桌宠重新启用再实拍确认。
- 转换 v4，流畅/标准/高清对应固定 128/256/384px 及 64/128/256 MiB 解码预算。质量不是显示插值；改档位后更新模型重新转换。同一提交/模型/版本/档位独立缓存；长动作在预算内降低时间采样，不降低固定空间分辨率，最多 120 秒、每动作 240 帧，小数 FPS 保留完整时长。
- `PetLibrary` 管 QSettings、缓存、单一后台工作进程及失败保留旧模型；工具在 `tools/arkpets`。内置阿米娅标准图集离线可用。只支持固定 Ark-Models 的基建角色及 Spine 3.8 动态立绘，不支持任意 Cubism `.moc3/.model3.json`。
- 按需使用运行设备的 JDK17、系统 Python/Pillow、OpenGL 或 Xvfb，JAR 通过 runtime-lock 按架构/哈希锁定。不自动 sudo/apt/pip；离线/缺依赖清楚失败。当前 Pi 已有 JDK17/Pillow，目录 API 回退实测成功，17 个 ARM64 锁定 JAR 校验且导出器编译通过；没有代表任意单个模型已端到端转换，也不能将高清预算冒充低内存设备性能保证。
- 模型素材非商业限制与 Spine 工具许可独立保留，确认框不是授权。不能移除 NOTICE、来源哈希或许可说明，也未移植 Ark-Pets Java 主程序。

## 长期回归

- 保留 `desktop_pet_test.cpp`、`pet_manager_test.cpp`、`character_worker_test.py` 及预览入口；真实已转换角色可用 `ARKPETS_TEST_MODEL=/absolute/path` 补测，默认用例不联网。
- `DiagnosticRunner.cpp` 的 `petEasterEgg` 场景覆盖默认关闭、标题计数、菜单、管理、启动与关闭卸载；当前实现对菜单项固定等待约 250ms，没有等待 `opened`。冷启动较慢时可能过早检查管理窗口；应先等待实际状态再判断，不能用过早失败断定产品功能失效，也不能把无状态断言的菜单点击算通过。
- Qt 6.4/Pi 的 inline `Loader.sourceComponent` 不应假定能解析 Main.qml 外部 `id: win` 等 ID。通过 Loader `onLoaded` 显式注入宿主/任务栏/弹窗对象；Pi 日志的 `ReferenceError: win is not defined` 是旧写法的现场根因。CMake 中桌宠 converter/licence 安装项必须归属 `TerminalRuntime` component，否则组件化运行时部署会漏掉 Python/JAR 工具。
- 某些网络环境会重置 `raw.githubusercontent.com`，但保留 `api.github.com`；`character_worker.download()` 只对固定 Ark-Models 仓库的 raw URL 回退 GitHub Contents raw media，仍校验 HTTPS/主机/仓库提交与下载体积，不使用任意镜像。Pi 已实际获取模型目录；模型原始素材下载/转换仍需用户在管理 UI 对具体模型确认许可。
- 图集打包避免 Python 逐像素 `putdata`：NumPy 可选向量化路径，Pillow-only 时用 `ImageMath` 通道运算；PNG 仍无损但用 compress level 3 且禁用 `optimize=True`。Pi 的旧路径基准约 1.2 秒/256²帧，新路径约 0.076 秒/帧（测试图内核微基准）。pack subprocess timeout 独立放宽到 900 秒；仍是有界任务，实际模型还需现场转换验收。
- 体验/正式构建关闭 TERMINAL_REPAIR_AUDIT；本机验证与 Pi 实测分开报告，不擅自部署、复制用户下载/配置或删除独立副本。

## 已失败方案与修改结论

以下是本项目实际遇到的失败，不是所有 Qt/Spine 程序的通用结论；维护时核对当前实现，不复活旧补丁。

| 失败方案或现象 | 当前结论与防回归 |
| --- | --- |
| 多个 AnimatedSprite 通过显隐/重启切动作，切换闪烁 | 保留一个 PetSpriteItem，但每帧重绘完整固定画布；不以多个精灵显隐切换，也不只更新旧 QSG 节点的 sourceRect。检查动作/朝向转换和实机残像。 |
| QSG 单纹理节点切 sourceRect、依赖动作锚点/节点重建或 QML layer 消除残像 | Pi 连续实拍仍复现动作旧位置碎片。改用固定透明画布、动作内容在画布内偏移，每帧全画布清透明并重绘；采用 Ark-Pets 的 FBO 两阶段流程。新版本需在 Pi 开启桌宠后连续实拍验收，主机 OpenGL 测试不替代该验收。 |
| 固定 10 秒时长限制拒绝水月 13.33 秒动作 | 允许最长 120 秒完整循环，受预算限制采样数量，使用小数 FPS 保留时长；不截取前十秒、不把旧限制误报为模型损坏。 |
| 按总帧量压低到 96～192px，长动作变模糊 | v4 固定质量的空间分辨率；只自适应时间采样。高清更耗内存/可能更低 FPS，不承诺原模型没有的细节。 |
| 按含透明留白或躺姿的联合画布缩放，不同角色大小失衡 | 以待机可见高度中位数归一化，所有姿态共用比例；用户大小偏好与纹理分辨率分离。 |
| 每动作只记一个最大 footY，切动作/帧时悬空或下陷 | 逐帧测 alpha>32 的可见底边；Qt 对旧缓存也重算，不能只更新新下载图集。 |
| 标题 TapHandler 的独立用例通过，但完整 Flickable 页鼠标点击计数全零 | 当前用 MouseArea 的 clicked/doubleClicked；须用完整页面真实鼠标验证五击、双击、超时、隐藏重置，不能只调用信号模拟成功。 |
| 冷启动诊断固定等约 250ms 检查管理 | 当前代码仍固定等待，时序不足会误判；改诊断时等待 startupComplete、菜单和管理的 opened 状态。未应用此修正的代码树，不得写成已修复。 |
| 预览改成默认关闭后，旧点击/物理/绘制测试失败 | 测试先断言默认关闭，再显式启用；不为让旧测试通过而恢复产品自动启动。 |
| 透明人物放在 Popup Overlay 中依赖其他弹窗可见 | 人物 Loader 位于 contentItem 独立高层；菜单/管理仍在 Overlay，安全弹窗优先；关闭卸载而不是仅把 opacity 设零。 |

## 当前验证边界

- 不保存已被替换源码的测试/合并结论；布局、字体、模糊取源与诊断时序均须读当前代码并通过新日志核对。当前源码已修复共享控件字号单位、SettingsGroup 高度及 GlassSurface 背景查找循环；电脑普通/1.5× DPI 审计各 61 场景通过。日志仍有两条无 QML 位置的 Qt 初始化 recursive-rearrange 警告，不宣称完整日志无警告，也不将主机验证冒充 Pi 已部署；范围见项目 `docs/pi-ui-regression.md`。
- 基建模型和动态立绘的转换应按角色、版本、档位和平台分别验证，不能从目录可达或编译导出器推断所有模型转换通过。ARM64 JAR/编译器已有 Pi 检查，真实模型渲染、缓存发布及角色交互仍需在用户选择并确认授权的具体模型上验收。
- 缓存键、图集和已下载模型是有效运行数据；清理只删确认无用的截图/日志/中间文件，不删除用户模型或 JAR 有效依赖。临时证据路径不是维护命令的固定输入。
