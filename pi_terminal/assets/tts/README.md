# 中文词典（zh_dict.bin）

`zh_dict.bin` 是中文语音引擎的运行时词典（4.2 MB，二进制）。引擎每次播报都要启动一次进程，
所以词典用**排序索引 + 字符串池**，载入只需一次读文件 + 二分查找（约 4 ms）。

## 内容

| 段 | 条数 | 用途 |
| :--- | ---: | :--- |
| 字表 | 44,440 | 单字兜底读音（多音字取常用读音） |
| 词表 | 86,698 | 只保留“读音与字默认读音不同”的词，用于多音字消歧 |

格式（小端）：

```
magic 4B 'ZHD1' | version u32 | n_chars u32 | n_phrases u32 | pad u32
chars_off u64 | phrases_off u64 | pool_off u64 | pool_len u64
每个索引项 16B：key_off u32 | key_len u32 | val_off u32 | val_len u32
池：key 与 value 均以 NUL 结尾
```

## 数据来源

| 来源 | 许可证 | 用途 |
| :--- | :--- | :--- |
| [`mozillazg/pinyin-data`](https://github.com/mozillazg/pinyin-data) | MIT | 单字读音、常用读音（`kHanyuPinlu`） |
| [`mozillazg/phrase-pinyin-data`](https://github.com/mozillazg/phrase-pinyin-data) | MIT | 词组读音与人工纠正表 |
| `ekho`（`hgneng/ekho`） | GPL-2.0-or-later | 只用于核对“引擎实际能发的音节表”，产物不包含其代码或录音 |

## 重新生成

`pi_terminal/tts/tools/` 里有生成脚本，运行目录需要放上游词库（脚本按相对路径读取）：

```sh
# 在包含 pinyin-data/、phrase-pinyin-data/ 的目录里
python3 build_lexicon.py        # 产出 zh_chars.tsv / zh_phrases.tsv / ...
python3 build_binary_dict.py    # 产出 zh_dict.bin，复制到本目录
```

脚本只是复现工具，不参与终端构建；构建只需要本目录的 `zh_dict.bin`。
