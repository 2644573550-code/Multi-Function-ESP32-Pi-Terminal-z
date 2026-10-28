#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""用 eSpeak-ng 的拼音音色补齐缺口音节。

缺口来自两类：
  - risky_poly / risky_neutral（135 个）：找不到“单独念一定是这个读音”的汉字
  - obscure 合成失败（5 个）：CJK 扩展区字，Edge TTS 返回 NoAudioReceived

eSpeak-ng 的 `cmn-latn-pinyin` 音色直接吃带调数字的拼音，所以不需要汉字，
声调绝对可控。音质是机械音，作为**占位单元**，之后可用角色模型替换。

用法：python3 fill_units_espeak.py [--dry-run]
"""
from __future__ import annotations

import argparse
import csv
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
UNITS = ROOT / "units"
TARGET_FILTER = "-20"

def convert(source: Path, destination: Path) -> None:
    command = [
        "ffmpeg", "-nostdin", "-y", "-loglevel", "error", "-i", str(source),
        "-af", "silenceremove=start_periods=1:start_silence=0.015:start_threshold=-45dB,"
               "areverse,silenceremove=start_periods=1:start_silence=0.015:start_threshold=-45dB,"
               f"areverse,loudnorm=I={TARGET_FILTER}:TP=-2:LRA=7",
        "-ar", "16000", "-ac", "1", "-c:a", "pcm_s16le", str(destination),
    ]
    subprocess.run(command, check=True, timeout=60)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    rows = list(csv.DictReader((ROOT / "syllable_units.tsv").open(encoding="utf-8"), delimiter="\t"))
    missing = [row for row in rows if not (UNITS / f"{row['syllable']}.wav").exists()]
    print(f"缺少单元 {len(missing)} 个：" + ", ".join(f"{r['syllable']}({r['class']})" for r in missing[:12]) + " ...")
    if args.dry_run:
        return 0

    UNITS.mkdir(exist_ok=True)
    filled = failed = 0
    with tempfile.TemporaryDirectory(prefix="espeak-units-") as temporary:
        raw = Path(temporary) / "raw.wav"
        for row in missing:
            syllable = row["syllable"]
            try:
                subprocess.run(
                    ["espeak-ng", "-v", "cmn-latn-pinyin", "-b", "1", "-s", "150",
                     "-w", str(raw), syllable],
                    check=True, timeout=30, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                )
                convert(raw, UNITS / f"{syllable}.wav")
                filled += 1
            except Exception as error:
                failed += 1
                print(f"  失败 {syllable}: {type(error).__name__}")
    print(f"补齐 {filled}，失败 {failed}；现共 {len(list(UNITS.glob('*.wav')))} 个单元")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
