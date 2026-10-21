# 桌宠彩蛋：实现说明与验证入口

本文对应本项目当前 `pi_terminal` 源码。运行、测试与限制应以当前代码为准；不把备选构建或旧运行日志当成当前构建结果。

## 功能

- 沿任务栏行走、坐下、躺下、边界转向、点击互动；自动活动在待机、行走和模型实际支持的坐下/躺下之间切换。右键/长按人物可手动选择姿势、切换自动活动。
- 行走方向为正表示向右；源角色默认朝右，向左行走时才水平镜像。
- 拖拽释放后根据重力加速度与释放速度下落，支持水平惯性、左右边界反向衰减、顶部/任务栏碰撞；落地速度归零，不穿透任务栏。采用至多 1/120s 子步处理慢帧，不把瞬间吸附当作自由落体。暂停行走不暂停重力；不模拟终端外任意窗口边缘或多屏物理世界。
- `Main.qml` contentItem 中独立高层，人物脚底绑定任务栏顶边；置顶仅指终端内普通页面，不覆盖其他 Linux 窗口。启动遮罩、模态弹窗优先，全屏视频隐藏任务栏时人物一并隐藏。
- 精确按当前动画帧 alpha 命中，人物周围透明区域穿透，不抢键盘焦点。
- **渲染方案**：参考 [Ark-Pets](https://github.com/isHarryh/Ark-Pets) 的固定画布两阶段渲染（[`SpineRenderPass`](https://github.com/isHarryh/Ark-Pets/blob/v3.x/core/src/cn/harryh/arkpets/render/SpineRenderPass.java)、[`PostProcessRenderPass`](https://github.com/isHarryh/Ark-Pets/blob/v3.x/core/src/cn/harryh/arkpets/render/PostProcessRenderPass.java)）：角色动作在固定透明画布内按姿势偏移；每次重绘先清空完整画布，再绘制当前帧，OpenGL 路径使用 FBO，软件后端使用同样的完整透明画布绘制。动作切换不移动画布根节点，也不遗留上一帧像素。角色文件先全部解码、验证后原子切换；无效/空白帧拒绝加载，旧人物保留。
- 普通模式 38px/s、节能模式 26px/s，动画按实际时间推进，不参与壁纸模糊缓存。
- 管理窗口标题栏的放大镜按钮打开“大小与画质”：大小 50%～200%（每步 5%），100% 是原自适应默认大小。滑动即时生效，提供恢复默认；设置通过 QSettings 持久保存，换角色和重启均保留。小窗口只限制实际显示上限，不覆盖用户偏好；缩放会同步限制水平/拖动边界及任务栏接地点，不重新转换素材。

## 角色下载和目标机自动转换

入口：完整终端 **设置 → 连续点击“显示与个性化”标题 5 次 → 桌宠管理**；独立预览任务栏也有“桌宠管理”。加载目录后，可按中文/英文名或模型 ID 搜索，选择人物下载。

管理界面采用 Material 3 的 list-detail 结构：宽屏左侧角色列表、右侧模型列表；窄屏逐级浏览并提供返回。模型按“全部／默认模型／服装模型／动态立绘”分类，区分“使用中／已下载／未下载”。先浏览选择，再通过底部唯一主操作下载或使用本地模型；许可确认在下载前的独立对话框中完成。启动优先显示缓存目录与内置模型，不强制联网；刷新后更新目录。角色名称、服装名称和模型 ID 保持原文。

管理窗口标题不再显示应用图标。窗口含最大化/恢复，但上下边界始终限制在任务栏上方；非模态的管理窗口不拦截任务栏操作。搜索旁的键盘按钮聚焦搜索并调用终端现有 `FloatingKeyboard`，键盘显示时管理窗口让出键盘区域，关闭键盘后恢复原高度；不会新建外部键盘窗口。只有下载授权确认对话框仍为模态。

管理页复用系统 Theme 配色、字体、圆角及 Material SVG；列表采用角色/模型层级，短屏许可说明可滚动。布局测试包含中英文及多种窗口尺寸，但不以已有测试源码或其他构建结果代替当前版本的图形验收。

规范参考：[Lists](https://m3.material.io/components/lists/overview)、[Adaptive design](https://m3.material.io/foundations/layout/applying-layout/window-size-classes)、[Buttons](https://m3.material.io/components/buttons/overview)、[Accessibility](https://m3.material.io/foundations/accessible-design/overview)。

流程：

1. 从固定的 Ark-Models 仓库获取目录并固定提交，只下载所选基建角色 `.atlas/.skel/.png`，不下载整个模型库。`raw.githubusercontent.com` 不通时，转换器将使用固定仓库/提交的 GitHub Contents raw API 回退；仍须联网。
2. 后台 Python 工作进程在**当前运行设备**检查转换依赖，按机器架构下载哈希锁定的转换 JAR。
3. 使用 Spine 3.8 在目标机后台渲染 `Relax/Idle`、`Move`、`Interact/Attack`、`Sit`、`Sleep/Lie`。统一尺寸/共同边界，逐帧记录接地点，按所选质量预算自适应采样并处理 PMA。缺少动作回退为待机并标记不支持；不把待机冒充坐下或躺下。
4. 验证全部动画帧非空、纹理总量不超过所选质量预算（64/128/256 MiB）后，原子发布本地 PNG 图集；Qt 成功解码全部图集才显示新人物。转换期间旧人物继续运行。
5. 保存在 `QStandardPaths::AppLocalDataLocation/arkpets/characters`，重启复用；转换工具按架构缓存。可恢复内置阿米娅。进程结束/失败不替换旧角色，关闭终端会终止其工作进程。

动作转换器当前版本为 4，质量、尺寸与逐帧接地规则见下文。支持最长 120 秒完整动画，每动作最多 240 个采样帧；旧下载模型可在本机重新转换，失败保留旧角色。

Ark-Models `models_illust` 是 **Spine 3.8 动态立绘**，并非 Cubism Live2D；没有 Move/Sit/Sleep 的立绘标记为不支持对应动作。不支持任意 `.model3.json/.moc3` 的 Cubism 导入。当前转换器时长上限为 120 秒，不能把旧版 10 秒限制当作当前行为。

当前支持 **Ark-Models 的基建角色和 Spine 动态立绘选择下载**，不是任意网址/任意 Spine 版本导入器。超过 120 秒、超大纹理和无法识别的模型仍会明确失败；不保证全库每个角色都已测试。下载需联网，转换在本机，不上传模型到云端。

### 目标机一次性前提

- Linux x86_64 或 ARM64，JDK17（`java` 与 `javac`）。
- `/usr/bin/python3` 与 Pillow；可用 `ARKPETS_PYTHON` 指定已安装 Pillow 的解释器，避免误用 Qt SDK 的 Python 虚拟环境。
- 有可用 OpenGL 图形会话；没有 DISPLAY 时需 Xvfb。转换时程序启动一个不显示的本地 GL 窗口。
- 首次使用需明确确认角色素材限制和适用的 Spine 工具许可。确认框不是授权替代物。

程序不会自动 sudo/apt/pip、更改系统软件或启动系统服务。Pi 已安装 JDK 17、Python 3/Pillow；本次在 Pi 验证了 3197 条目录记录（1046 个可用模型条目）、GitHub raw API 回退，以及 17 个 ARM64 哈希锁定 JAR 的校验与 `ExportSprites.java` 编译。当前程序部署包含角色工作进程与许可文件。尚未在未选择并确认具体角色许可的情况下下载模型素材或做端到端角色转换；这不等于所有模型均转换通过。

打包器以 NumPy（可选）向量化解除 PMA，NumPy 不可用时退回 Pillow `ImageMath` 的原生通道运算；不再对每个像素执行 Python 循环，并以 PNG level 3 代替 `optimize=True` 的额外慢速压缩。Pi 实测 256×256 PMA 单帧转换：旧路径 10 帧约 12 秒，新路径 100 帧约 7.6 秒；这是转换内核微基准，不代表真实模型全程时间。打包阶段超时从 240 秒放宽到 900 秒，仍保留硬上限。

## 本机运行

```sh
bash pi_terminal/scripts/run-arkpets-preview.sh
```

完整终端：

```sh
source pi_terminal/scripts/env.sh
cmake -S pi_terminal -B build-host -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON -DTERMINAL_REPAIR_AUDIT=OFF
cmake --build build-host --target orange_pi_terminal --parallel 4
env -u ORANGE_PI_CONTROL_DEVICE \
  XDG_CONFIG_HOME=/tmp/opencode/arkpets-main-local/config \
  XDG_DATA_HOME=/tmp/opencode/arkpets-main-local/data \
  XDG_CACHE_HOME=/tmp/opencode/arkpets-main-local/cache \
  build-host/orange_pi_terminal --window-size 1024x600
```

完整终端读取本机系统状态和偏好，预览不初始化系统连接对象；点击角色目录时才发起下载请求。

## 测试入口与实现注意事项

长期测试源码包括 `desktop_pet_test.cpp`、`pet_manager_test.cpp`、`character_worker_test.py` 和 `screenshot_regression.py`。下列项目描述测试覆盖，不代表当前工作树已重新运行通过。

- 在 software 和 Xvfb/OpenGL 下检查绘制、鼠标点击、真实拖动、松手不触发点击、缩放、透明输入穿透。
- 逐帧检查五动作及两个朝向的绘制与接地，对照去除人物的底图检查空白帧。此断言不替代用户长时间体验。
- 物理测试检查加速度导致连续时间段位移递增、落地速度归零、拖拽不触发点击、缩放后边界约束以及坐下/躺下状态；动作截图检查实际姿势位于任务栏上方。
- 大小专项检查真实滑块实时改变人物宽度、恢复默认、重建设置对象后数值保留、非法数值/上下限、缩放后接地/窗口边界，以及改变大小后的下落。

桌宠内联 `Loader.sourceComponent` 在 Qt 6.4 上不能可靠解析外层 `win` 等 id；Loader 现在在 `onLoaded` 显式传入窗口、任务栏和弹窗对象，Pi 彩蛋诊断验证五击、管理不启动、启停卸载均通过。完整彩蛋诊断仍对菜单项使用固定短等待，冷启动时可能早于管理窗口 opened 检查；需要结合实际弹窗状态判断。当前源码已修正共享控件的字体单位冲突、SettingsGroup 高度依赖及 GlassSurface 背景查找循环；电脑审计验证范围与残余 Qt 初始化警告见 `pi-ui-regression.md`，不将电脑结果冒充 Pi 部署结果。

## 隐藏彩蛋入口

完整终端每次启动默认关闭桌宠，不持久保存启用状态；关闭时 Loader 卸载人物及图集。设置 → 连续点击“显示与个性化”标题五次（相邻点击不超过 1.8 秒）→ 弹出两项菜单：启动/关闭桌宠、桌宠管理。单独管理角色不启动桌宠，角色/大小/画质偏好照常保存。普通设置页面不再展示常驻桌宠按钮。

标题点击由 `MouseArea` 处理 `clicked` 与 `doubleClicked`。完整交互需在实际设置页验证；独立设置组件测试不能替代 Flickable 页面输入链路。诊断选项需要启用 `TERMINAL_REPAIR_AUDIT`，且当前诊断对菜单项使用固定等待时间。

## 渲染质量、统一尺寸与逐帧接地（当前 v4）

管理标题栏放大镜 → **大小与画质**：

| 档位 | 固定纹理帧尺寸 | 解码图集预算上限 |
|---|---|---|
| 流畅 | 128×128 | 64 MiB |
| 标准（默认） | 256×256 | 128 MiB |
| 高清 | 384×384 | 256 MiB |

这些是转换产物的实际分辨率，不是显示缩放或插值开关。选择档位后通过模型列表“更新质量/动作”转换；同一角色/版本/质量独立缓存，失败或转换期间保留旧人物。已完成的目标质量可直接使用。内置阿米娅已重新渲染为标准 256px，不再是先前 192px 版本。

空间清晰度不再因动画时长自动下降。为避免无上限内存，每个动作的采样数按质量预算和图集边长约束；FPS 可为小数，`frames/fps` 对应原动作完整时长。长动作选高清会消耗更多内存，也可能降低采样流畅度；不是保证所有质量下恒定 24 FPS。原始模型纹理清晰度仍是上限。

人物显示比例依据 **待机可见轮廓高度的中位数**（alpha >32）计算，默认参考高度 176 逻辑像素再乘宿主缩放/用户大小；不再依据含透明留白、特效或躺姿的联合画布。所有动作沿用同一角色比例，躺下不会被拉高成站立高度。

Qt 读取图集时会为旧缓存重新测量逐帧可见像素底边和参考高度；以待机中位接地点为统一锚点，在内存计算每个动作在共同画布内的偏移，并为上下极值保留透明空间，不改写用户图集。动作切换只改变画布内精灵位置，根节点保持固定；每次完整清透明画布并重绘当前帧，OpenGL 下使用 FBO，软件后端保留相同的完整画布绘制。

- 检查真实下载角色可渲染/互动、Qt→Python 工作进程的缓存交接、下载失败保留原选择、路径逃逸拒绝和许可未确认时不下载。
- ARM64 转换分支存在不代表 Pi 实机转换、内存或性能已经验收。

```sh
source pi_terminal/scripts/env.sh
cmake --build build-host --parallel 4
ctest --test-dir build-host --output-on-failure
xvfb-run -a build-host/desktop_pet_test
# 可选：使用已实际下载的图集补测导入/工作进程交接（不联网）
ARKPETS_TEST_MODEL=/absolute/path/to/converted/character xvfb-run -a build-host/desktop_pet_test
```

## 授权

Ark-Pets 作为交互参考，未引入其 Java 程序。角色来源与原版权限制、Spine 独立许可保留于 `pi_terminal/assets/arkpets/`；`tools/arkpets/runtime-lock.json` 固定转换依赖 SHA256。终端正常动画不加载 Java/Spine，只在用户请求下载未转换角色时启动本机转换子进程。此原型不授予角色素材商业使用权。
