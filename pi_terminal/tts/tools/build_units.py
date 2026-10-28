#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""定稿 1,493 个带调音节的“提示字”，并用 Edge TTS 批量合成单元音频。

为什么用提示字：老师（微软/角色模型）只吃文本，不吃拼音。选字的规则是
“单独念出来一定是目标读音”——即该字的常用读音等于目标音节（kHanyuPinlu），
或者该字只有这一个读音。两者都不满足的进 risky，留到第二阶段处理。

用法：
  python3 build_units.py                      # 只生成 syllable_units.tsv
  python3 build_units.py --synth              # 生成后调用 Edge TTS 合成
  python3 build_units.py --synth --classes safe_common,safe_mono
"""
from __future__ import annotations

import argparse
import asyncio
import csv
import json
import os
import subprocess
import collections
from pathlib import Path

ROOT = Path(__file__).resolve().parent
VOICE = "zh-CN-XiaoxiaoNeural"


def load_tables():
    readings, common = collections.defaultdict(list), {}
    with (ROOT / "zh_chars.tsv").open(encoding="utf-8") as handle:
        for row in csv.DictReader(handle, delimiter="\t"):
            if len(row["char"]) == 1:
                readings[row["char"]] = row["numbered"].split(",")
    with (ROOT / "zh_common_reading.tsv").open(encoding="utf-8") as handle:
        for row in csv.DictReader(handle, delimiter="\t"):
            common[row["char"]] = row["common_numbered"]
    freq = collections.Counter()
    with (ROOT / "zh_phrases.tsv").open(encoding="utf-8") as handle:
        for row in csv.DictReader(handle, delimiter="\t"):
            for ch in row["phrase"]:
                if "\u4e00" <= ch <= "\u9fff":
                    freq[ch] += 1
    return readings, common, freq


def build_units() -> list[dict]:
    readings, common, freq = load_tables()
    syllables = sorted({s for rs in readings.values() for s in rs if s})
    units = []
    for syllable in syllables:
        candidates = [ch for ch in readings if syllable in readings[ch]]
        default_match = [ch for ch in candidates if common.get(ch) == syllable]
        monophonic = [ch for ch in candidates if len(readings[ch]) == 1]
        safe = default_match or monophonic
        if default_match:
            char, kind = max(default_match, key=lambda c: freq.get(c, 0)), "safe_common"
        elif monophonic:
            char, kind = max(monophonic, key=lambda c: freq.get(c, 0)), "safe_mono"
        else:
            char, kind = max(candidates, key=lambda c: freq.get(c, 0)), (
                "risky_neutral" if syllable.endswith("5") else "risky_poly"
            )
        if kind.startswith("safe") and freq.get(char, 0) == 0:
            kind = "obscure"
        units.append({
            "syllable": syllable,
            "tone": syllable[-1],
            "char": char,
            "class": kind,
            "char_freq": freq.get(char, 0),
            "readings": ",".join(readings.get(char, [])),
        })
    return units


def write_units(units: list[dict]) -> None:
    path = ROOT / "syllable_units.tsv"
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, delimiter="\t",
                                fieldnames=["syllable", "tone", "char", "class", "char_freq", "readings"])
        writer.writeheader()
        writer.writerows(units)
    counts = collections.Counter(u["class"] for u in units)
    print(f"{path.name}: {len(units)} 音节 -> " + ", ".join(f"{k}={v}" for k, v in sorted(counts.items())))


async def synth_one(unit: dict, out_dir: Path, sem: asyncio.Semaphore) -> dict:
    import edge_tts

    wav = out_dir / f"{unit['syllable']}.wav"
    if wav.exists() and wav.stat().st_size > 1000:
        return {"syllable": unit["syllable"], "status": "cached"}
    mp3 = out_dir / f"{unit['syllable']}.mp3"
    async with sem:
        try:
            communicate = edge_tts.Communicate(unit["char"], VOICE)
            await communicate.save(str(mp3))
        except Exception as error:  # 网络/限流/服务端错误都记下来，不中断整批
            return {"syllable": unit["syllable"], "status": f"fail:{type(error).__name__}"}
    # mp3 -> 16 kHz 单声道 16 bit，去首尾静音并统一响度
    command = [
        "ffmpeg", "-nostdin", "-y", "-loglevel", "error", "-i", str(mp3),
        "-af", "silenceremove=start_periods=1:start_silence=0.015:start_threshold=-45dB,"
               "areverse,silenceremove=start_periods=1:start_silence=0.015:start_threshold=-45dB,"
               "areverse,loudnorm=I=-20:TP=-2:LRA=7",
        "-ar", "16000", "-ac", "1", "-c:a", "pcm_s16le", str(wav),
    ]
    try:
        subprocess.run(command, check=True, timeout=60)
    except Exception as error:
        return {"syllable": unit["syllable"], "status": f"ffmpeg:{type(error).__name__}"}
    finally:
        mp3.unlink(missing_ok=True)
    return {"syllable": unit["syllable"], "status": "ok", "bytes": wav.stat().st_size}


async def synth(units: list[dict], classes: set[str], concurrency: int = 4) -> None:
    out_dir = ROOT / "units"
    out_dir.mkdir(exist_ok=True)
    todo = [u for u in units if u["class"] in classes]
    sem = asyncio.Semaphore(concurrency)
    results = []
    for index in range(0, len(todo), 100):
        batch = todo[index:index + 100]
        results.extend(await asyncio.gather(*(synth_one(u, out_dir, sem) for u in batch)))
        print(f"  {min(index + 100, len(todo))}/{len(todo)}", flush=True)
    counts = collections.Counter(r["status"] for r in results)
    print("合成结果:", dict(counts))
    index_path = ROOT / "units_index.json"
    index_path.write_text(json.dumps(results, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--synth", action="store_true")
    parser.add_argument("--classes", default="safe_common,safe_mono,obscure")
    parser.add_argument("--concurrency", type=int, default=4)
    args = parser.parse_args()

    units = build_units()
    write_units(units)
    if args.synth:
        if not os.environ.get("VIRTUAL_ENV") and "edge_tts" not in sys_modules():
            print("提示：请用 ~/voice-work/tts-venv/bin/python 运行本脚本")
        asyncio.run(synth(units, set(args.classes.split(",")), args.concurrency))
    return 0


def sys_modules() -> set:
    import sys
    return set(sys.modules)


if __name__ == "__main__":
    raise SystemExit(main())
