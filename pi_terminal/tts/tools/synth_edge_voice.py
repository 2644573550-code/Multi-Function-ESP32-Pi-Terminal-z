#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""用 Edge TTS 生成一套微软音色的音节单元（免 key）。

输出与基础单元集同格式：<out>/<音节>.wav（16 kHz 单声道、去静音、loudnorm、无 LIST 块），
另外生成 26 个字母单元 ltr_a..ltr_z 供拉丁拼读用。

用法：
  python3 synth_edge_voice.py --voice zh-CN-YunyangNeural --name yunyang
  python3 synth_edge_voice.py --voice zh-CN-YunxiNeural   --name yunxi
"""
from __future__ import annotations

import argparse
import asyncio
import csv
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
PROMPTS = ROOT / "lexicon" / "units_prompts.tsv"
OUT_ROOT = ROOT / "voices"


async def one(char: str, destination: Path, voice: str, sem: asyncio.Semaphore) -> str:
    import edge_tts
    if destination.exists() and destination.stat().st_size > 2000:
        return "cached"
    async with sem:
        try:
            mp3 = destination.with_suffix(".mp3")
            await edge_tts.Communicate(char, voice).save(str(mp3))
        except Exception as error:
            return f"fail:{type(error).__name__}"
    command = [
        "ffmpeg", "-nostdin", "-y", "-loglevel", "error", "-i", str(mp3),
        "-af", "silenceremove=start_periods=1:start_silence=0.015:start_threshold=-45dB,"
               "areverse,silenceremove=start_periods=1:start_silence=0.015:start_threshold=-45dB,"
               "areverse,loudnorm=I=-20:TP=-2:LRA=7",
        "-ar", "16000", "-ac", "1", "-c:a", "pcm_s16le",
        "-map_metadata", "-1", "-fflags", "+bitexact", str(destination),
    ]
    try:
        subprocess.run(command, check=True, timeout=60)
    except Exception as error:
        return f"ffmpeg:{type(error).__name__}"
    finally:
        mp3.unlink(missing_ok=True)
    return "ok"


async def run(voice: str, out_dir: Path, concurrency: int) -> None:
    rows = list(csv.DictReader(PROMPTS.open(encoding="utf-8"), delimiter="\t"))
    rows += [{"syllable": f"ltr_{c.lower()}", "char": c} for c in "ABCDEFGHIJKLMNOPQRSTUVWXYZ"]
    out_dir.mkdir(parents=True, exist_ok=True)
    sem = asyncio.Semaphore(concurrency)
    counts = {}
    for start in range(0, len(rows), 100):
        batch = rows[start:start + 100]
        results = await asyncio.gather(*(one(r["char"], out_dir / f"{r['syllable']}.wav", voice, sem) for r in batch))
        for status in results:
            counts[status] = counts.get(status, 0) + 1
        print(f"  {min(start + 100, len(rows))}/{len(rows)} {counts}", flush=True)
    print("完成:", counts)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--voice", required=True, help="例如 zh-CN-YunyangNeural")
    parser.add_argument("--name", required=True, help="输出目录名，例如 yunyang")
    parser.add_argument("--concurrency", type=int, default=6)
    args = parser.parse_args()
    out_dir = OUT_ROOT / args.name / "units"
    print(f"voice={args.voice}  out={out_dir}")
    asyncio.run(run(args.voice, out_dir, args.concurrency))
    total = len(list(out_dir.glob("*.wav")))
    print(f"{args.name}: 共 {total} 个单元")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
