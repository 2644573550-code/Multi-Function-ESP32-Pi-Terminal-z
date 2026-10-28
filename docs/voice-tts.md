# 语音播报：本地 flite 前端 + 单文件声库

## 架构

```
文本 → 词典分词/多音字/数字/时间/UTC/字母 → 带调音节
     → voice.zhvox（单文件声库，内存加载）→ 5 ms 交叉淡化拼接
     → 临时 WAV（播完即删）→ QSoundEffect → 系统默认/用户指定输出设备
```

- 纯英文文本走 flite 的英文前端（`cmulex` + `usenglish`）+ `cmu_us_slt.flitevox`，同样离线。
- **不再使用 clustergen（cg）中文音色**：此前把 1,493 个整音节当音素训练，实测输出宽带噪声，
  该路径与训练产物已移除；英文 slt 是上游预训练 cg 语音，不受影响。

## 数据

| 内容 | 位置 | 体积 |
| :--- | :--- | :--- |
| 中文声库（9 套） | `assets/voices/<id>/voice.zhvox` | 11–22 MB/套 |
| 词典 | `assets/tts/zh_dict.bin` | 4.2 MB |
| 英文音色 | `assets/tts/cmu_us_slt.flitevox` | 11 MB |

`voice.zhvox` 格式（`pi_terminal/tts/tools/build_voicebank.py` 生成）：

```
header: 'ZHV1' | version u32 | sample_rate u32 | count u32
index : count × 24B = name[16] | offset u32 | samples u32
payload: s16le PCM
```

- 仓库**不含任何 wav**；运行时临时 wav 在播完/取消/超时/析构时删除。
- 想换音色：替换同名 `voice.zhvox` 即可；开发时也可用 `FLITECN_UNITS=<units 目录>` 直接读 wav。

## 引擎选择

| 条件 | 行为 |
| :--- | :--- |
| 含汉字且 `voice.zhvox` 存在 | 内存拼接 → 临时 wav → 播放 |
| 含汉字但只有 units 目录 | 开发模式：直接读 units（仅本机） |
| 纯英文且 `cmu_us_slt.flitevox` 存在 | flite 英文前端 + slt |
| 以上都不可用 | espeak-ng（最后兜底） |

## 行为契约（不变）

- 单条 **300 字**上限；**单槽合并**（新播报覆盖待播内容，不排队）
- **15 s 超时**；超时杀进程、停播放、删临时文件、释放降音
- 播报期间音乐/视频增益 **0.2**（120 ms 渐变）；铃声是第三音源，不降音
- 「中文语音播报」总开关关闭时任何引擎都不出声

## 设置界面

- 「声音与语音」：语音音色（3 套微软 + 角色 6 套，角色在测试模式里）、播报输出设备、降音开关。
- **section 13** 音色页：试听 / 使用中；测试模式分类与关闭入口。
- **彩蛋**：连续点击五次「声音与语音」标题 → 确认弹窗「即将开启语音播报测试模式，此类声音为测试版效果较差，可能降低使用体验」→ 解锁 BanG Dream 测试音色；页内可关闭并自动切回晓晓。已开启时五击**仍先弹确认**，确认后进入音色页。

### 设计合规

- 复用 `SettingsGroup`/`CarButton`/`CarText`/`MaterialIcon` 与 `Theme` token，热区 ≥ `Theme.hit`
- 弹窗是 `Overlay` 上的 `Popup`，不新建 Window；无新模糊算法；文案全走 `I18n.t()`

## 给后续开发的语音接口

新增播报点统一走宿主接口（自动处理总开关、300 字、单槽、降音、超时），不要直连引擎：

```qml
win.announce(I18n.t("已保存"))                 // 当前音色
win.announceWithVoice("hello", "yunxi")        // 指定音色
if (!systemTools.speechReady) console.log("语音不可用")
```

| API | 用途 |
| :--- | :--- |
| `speak(text)` / `speakWithVoice(text,id)` | 播报（C++/QML） |
| `previewVoice(id)` | 固定试听句 |
| `speechReady()` | 引擎/词典/声库是否就绪 |
| `voiceId` / `voiceName` / `voiceOptions` / `setVoice` | 音色状态与选择 |
| `audioOutputs` / `audioOutputDevice` / `setAudioOutputDevice` | 播报输出设备（空 = 跟随系统） |
| `speechActive` / `mediaGain` / `duckMediaForSpeech` | 降音联动 |

## 构建

vendored flite（`third_party/flite`）构建 `libflite` + 英文前端，**免安装、免联网**：

```sh
cmake -S pi_terminal -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
```

交叉编译可用 `-DFLITE_ROOT=<已构建 flite>` 覆盖。

## 验证现状

- 从零构建通过；`runtime_test` 覆盖声库环境、降音生命周期、输出设备、音色闸门
- 声库输出与原单元拼接 **bit-identical**（实测 RMSE 差 0），实时合成 ~9 ms/句
- 截图场景：音色页中英、声音设置、测试模式
- 未验证：Pi 实机时延/音质、真实听感（cg 已废弃，不涉及）

## 与作者边界的对照（复核清单）

| 作者约束 | 本实现的处理 |
| :--- | :--- |
| 不新建全局 UI 层 / Window | 音色页是普通 section 13；弹窗全部挂在 `Overlay.overlay` |
| 复用组件与 Theme token、热区 ≥48px | 只用 `SettingsGroup`/`CarButton`/`CarText`/`MaterialIcon`；`Theme.hit` 下限 |
| 不新增模糊/渲染算法 | 未触碰 `CachedBlur`/`BlurCache`/窗口背景 |
| 文案全走 I18n | 新增字符串中英均有；`win.announce` 不拼 shell |
| 播报降音范围 | 仅 `MusicPlayer`/`VideoPlayer` 乘 `mediaGain`；铃声不受影响 |
| 用户文本安全 | 引擎用 `QProcess` 参数数组传递，无 shell 拼接；声库路径来自程序目录 |
| 设置归属 | 音色/输出设备/测试模式存于 `SystemTools` 自己的 `settings_`，不占用页面 `settingsWrite` 前缀 |
| 可测试性 | `runtime_test` 断言声库环境/降音/设备/接口；截图覆盖音色页、彩蛋确认、声音设置 |
| 最小改动 | 主要改动集中在 `SystemTools` + 新 `VoiceSettings.qml`；`Main.qml` 只加行/入口/弹窗 |

后续新增播报点请只用 `win.announce` / `win.announceWithVoice`，不要直接起进程或读写声库。

## 已知限制

- 声库是 16 kHz 单声道 PCM 拼接，不做 F0 变换；语调平直
- 140 个音节（多音/轻声/CJK 扩展区）仍是 eSpeak 占位，音色机械
- 角色音色仅非商业使用；替换同名 `voice.zhvox` 即可换成已授权声音
- 英文 slt 为美式女声，与中文音色风格不一致
