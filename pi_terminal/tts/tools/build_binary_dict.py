#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把两份 TSV 词典编译成可 mmap/直接读取的二进制，省掉每次进程启动的解析开销。

原因：`flite_zh` 每次播报都要启动一次进程，之前要解析 8.7 万条词 + 4.4 万字
（约 130 ms；树莓派上会到秒级）。改成排序索引 + 字符串池后，载入只剩一次读文件 + 二分查找。

格式（小端）：
  magic  4s   'ZHD1'
  version u32
  n_chars u32
  n_phrases u32
  pad     u32
  chars_off   u64
  phrases_off u64
  pool_off    u64
  pool_len    u64
  每个索引项 16 字节：key_off u32, key_len u32, val_off u32, val_len u32
  池：key 与 value 都以 NUL 结尾
"""
from __future__ import annotations

import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parent
OUT = ROOT / "zh_dict.bin"
HEADER = struct.Struct("<4sIIIIQQQQ")
ENTRY = struct.Struct("<IIII")


def read_tsv(path: Path) -> list[tuple[bytes, bytes]]:
    rows = []
    with path.open(encoding="utf-8") as handle:
        for line in handle:
            line = line.rstrip("\n")
            if not line or line.startswith("#"):
                continue
            key, _, value = line.partition("\t")
            if not key or not value:
                continue
            rows.append((key.encode("utf-8"), value.encode("utf-8")))
    return rows


def main() -> int:
    chars = read_tsv(ROOT / "char_dict.txt")
    phrases = read_tsv(ROOT / "engine_dict.txt")
    chars.sort(key=lambda item: item[0])
    phrases.sort(key=lambda item: item[0])

    pool = bytearray()
    char_index, phrase_index = [], []
    for entries, index in ((chars, char_index), (phrases, phrase_index)):
        for key, value in entries:
            key_off = len(pool)
            pool += key + b"\0"
            val_off = len(pool)
            pool += value + b"\0"
            index.append((key_off, len(key), val_off, len(value)))

    chars_off = HEADER.size
    phrases_off = chars_off + len(char_index) * ENTRY.size
    pool_off = phrases_off + len(phrase_index) * ENTRY.size

    with OUT.open("wb") as handle:
        handle.write(HEADER.pack(b"ZHD1", 1, len(char_index), len(phrase_index), 0,
                                 chars_off, phrases_off, pool_off, len(pool)))
        for entry in char_index + phrase_index:
            handle.write(ENTRY.pack(*entry))
        handle.write(pool)

    print(f"{OUT.name}: 字 {len(char_index)} 条、词 {len(phrase_index)} 条，"
          f"{OUT.stat().st_size / 1024:.0f} KiB")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
