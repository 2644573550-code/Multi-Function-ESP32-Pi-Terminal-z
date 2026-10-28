# 语音音色声库（NOTICE）

本目录每套音色是**一个单文件声库** `voice.zhvox`（16 kHz 单声道 PCM + 索引），不是零散 wav：

```
assets/voices/<voice>/voice.zhvox
```

## 来源

| voice | 来源 | 说明 |
| :--- | :--- | :--- |
| `xiaoxiao`、`yunyang`、`yunxi` | Microsoft Edge 神经网络语音（晓晓/云扬/云希） | 逐音节合成后打包 |
| `kasumi`、`tae`、`rimi`、`saaya`、`arisa`、`aya` | 本地 GPT-SoVITS v2ProPlus 零样本克隆 | 参考音频为第三方版权素材，逐音节生成 |

## 授权与分发

- 本项目为**非盈利**项目，上述音色仅用于测试与演示；原音色/角色版权不属于本仓库。
- **商业分发前必须替换**：用 `pi_terminal/tts/tools/build_voicebank.py` 把已获授权的声音
  重新打包成同名 `voice.zhvox` 即可，引擎与代码无需改动。
- Microsoft 合成音受其服务条款约束；GPT-SoVITS 角色音色来自第三方版权素材，仅限非商业使用。

## 质量分层

- 微软三套：1,353 个由 Edge 直接合成（其中 177 个生僻字待抽听）。
- 六套角色：同样 1,353 个；其余 140 个（多音字、轻声、CJK 扩展区）用 eSpeak-ng 拼音占位，
  声调正确但音色机械。替换方式一致：重新打包同名 `voice.zhvox`。

## 复现

生成脚本与步骤见 `docs/voice-tts.md` 与 `pi_terminal/tts/tools/`；词典来源见
`pi_terminal/assets/tts/README.md`。
