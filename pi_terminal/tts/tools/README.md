# 语料与词典/声库生成脚本（复现用，不参与构建）

这些脚本是 `assets/voices/` 与 `assets/tts/zh_dict.bin` 的生成来源，**构建终端时不需要运行它们**。
排序按实际生成顺序：

| 脚本 | 用途 | 前置 |
| :--- | :--- | :--- |
| `build_lexicon.py` | 把 pinyin-data / phrase-pinyin-data 整理成 TSV | 工作目录含两个上游仓库的克隆 |
| `build_binary_dict.py` | TSV → `zh_dict.bin` | 上一步产物 |
| `build_units.py` | 逐音节提示字 → Edge TTS 单元 wav | `edge-tts`、ffmpeg；工作目录含音节表 |
| `fill_units_espeak.py` | 多音/轻声/CJK 扩展区用 eSpeak-ng 占位补齐 | `espeak-ng`、ffmpeg |
| `synth_edge_voice.py` | 同一套提示字生成任一微软音色（晓晓/云扬/云希…） | `edge-tts`、ffmpeg |
| `render_units.py` | 用本地 GPT-SoVITS 生成角色音色单元 | GPT-SoVITS 仓库、参考音频、ROCm/CPU 环境 |
| `build_voicebank.py` | units/*.wav → 单文件 `voice.zhvox`（引擎运行时用） | 无 |

脚本里的相对路径按“工作目录 = 数据目录”写的（例如 `units/`、`zh_chars.tsv` 在同级），
放到仓库里只作为**复现参考**；具体输入与授权说明见：

- `docs/voice-tts.md`（整体方案与边界）
- `pi_terminal/assets/voices/NOTICE.md`（音色来源与分发限制）
- `pi_terminal/assets/tts/README.md`（词典格式与来源）
