#!/usr/bin/env python3
"""把 units/*.wav 打包成单文件声库 .zhvox（无零散 wav，引擎内存加载）。

格式（小端）：
  header: magic 'ZHV1' | u32 version | u32 sample_rate | u32 count | u32 reserved
  index : count × 24B = name[16] (NUL 填充) | u32 offset(样本) | u32 samples
  payload: s16le PCM
"""
import struct, sys, wave
from pathlib import Path

def build(units: Path, out: Path) -> None:
    files = sorted(units.glob("*.wav"))
    entries, payload = [], bytearray()
    for path in files:
        with wave.open(str(path)) as w:
            if (w.getframerate(), w.getnchannels(), w.getsampwidth()) != (16000, 1, 2):
                continue
            data = w.readframes(w.getnframes())
        try:
            name = path.stem.encode("ascii")[:15]
        except UnicodeEncodeError:
            continue  # 忽略非 ASCII 的临时/异常文件
        entries.append((name, len(payload) // 2, len(data) // 2))
        payload += data
    with out.open("wb") as f:
        f.write(struct.pack("<4sIII", b"ZHV1", 1, 16000, len(entries)))
        for name, off, n in entries:
            f.write(struct.pack("<16sII", name, off, n))
        f.write(payload)
    print(f"{out}: {len(entries)} units, {out.stat().st_size/1048576:.1f} MB")

if __name__ == "__main__":
    build(Path(sys.argv[1]), Path(sys.argv[2]))
