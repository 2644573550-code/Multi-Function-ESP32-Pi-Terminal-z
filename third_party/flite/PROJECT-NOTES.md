# flite（vendored）：本项目的裁剪与构建说明

本目录是 CMU **flite 2.2** 的上游源码（上游 `README.md` 原样保留），按项目既有约定放在
`third_party/`，由 `pi_terminal/CMakeLists.txt` 在构建时自动编译，**不需要预先安装 flite，也不需要联网**。

- 上游：<https://github.com/festvox/flite>，tag `v2.2`
- 许可证：见本目录 `COPYING` 与 `ACKNOWLEDGEMENTS`（BSD 风格，允许再分发）
- 用途：提供 `libflite` 核心库与英文前端（`cmulex` + `usenglish`），
  供 `pi_terminal/tts/flite/` 链接：中文走自有音节前端 + 声库，英文走 flite 英文前端 + slt

## 与上游的差异

只做了**裁剪**，没有改源码：

| 保留 | 说明 |
| :--- | :--- |
| `configure`、`configure.in`、`config/`、`config.guess`、`config.sub`、`install-sh`、`missing`、`Makefile` | 上游自带，可直接 `./configure` |
| `include/`、`src/` | 构建 `libflite.a` |
| `lang/cmulex`、`lang/usenglish` | 英文前端与词典（供 `cmu_us_slt.flitevox` 使用） |
| `COPYING`、`ACKNOWLEDGEMENTS`、`README.md` | 许可证与来源说明 |

| 移除 | 原因 |
| :--- | :--- |
| `lang/` 其余目录（英文音色、其他语种） | 只保留英文前端；不需要合成器自带音色 |
| `main/`、`tools/`、`doc/`、`testsuite/`、`android/`、`wince/`、`sapi/` | 终端不解包 flite CLI，不需要 |

## 构建方式

`pi_terminal/CMakeLists.txt` 在构建阶段把本目录复制到构建树，然后：

```sh
./configure -q --with-audio=none     # 音频播放交给 Qt，避免依赖 libasound 头文件
make -C src -j                       # libflite.a
make -C lang/cmulex -j               # libflite_cmulex.a
make -C lang/usenglish -j            # libflite_usenglish.a
```

想复用系统里已构建好的 flite（例如交叉编译到 Pi）：

```sh
cmake -S pi_terminal -B build -DFLITE_ROOT=/path/to/flite
```

## 升级

替换成新版本的上游源码树（保留 `configure`/`include`/`src` 等），重新构建即可；
`pi_terminal/tts/flite/zh_engine.c` 只用到 `flite.h`、`cst_wave.h`、`cst_utterance.h` 等稳定接口。
