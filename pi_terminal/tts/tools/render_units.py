#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""用 GPT-SoVITS 批量渲染音节单元语料（GPU / CPU 通用）。

关键技巧：把 N 个提示字用「。」连成一句、一次请求（batch_size=N），
模型会并行合成；返回一整段音频，再用能量分段切回 N 个单元。
切不准就退回逐条合成，保证不写坏单元。

用法：
  python3 render_units.py --character kasumi --limit 20 --batch 10
  python3 render_units.py --character kasumi --batch 10          # 全量 1,358
  python3 render_units.py --all --batch 10                       # 六个角色
"""
from __future__ import annotations

import argparse
import csv
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent
VOICE_ROOT = Path(os.environ.get("VOICE_ROOT", "~/gpt-sovits")).expanduser()
REPO = VOICE_ROOT / "repo"
CONFIG = Path(os.environ.get("VOICE_CONFIG",
                             VOICE_ROOT / "configs" / "tts_infer_base_v2proplus_rocm.yaml")).expanduser()
REF_DIR = Path(os.environ.get("VOICE_REF_DIR", "~/voice-work/ref/audio_reference")).expanduser()
OUT_ROOT = Path(os.environ.get("VOICE_OUT", "~/voice-work/voices")).expanduser()
PROMPTS = ROOT / "lexicon" / "units_prompts.tsv"
ZH_AUX = Path("~/.local/share/ModularTerminal/OrangePiTerminal/voice-packs/zh-xiaoxiao/preview.wav").expanduser()

CHARACTERS = ["kasumi", "tae", "rimi", "saaya", "arisa", "aya"]
RATE = 32000

sys.path.insert(0, str(VOICE_ROOT / "shims"))
sys.path.insert(0, str(REPO))
sys.path.insert(0, str(REPO / "GPT_SoVITS"))
os.chdir(REPO)

import numpy as np                                              # noqa: E402
import soundfile as sf                                          # noqa: E402
import fast_langdetect                                          # noqa: E402
fast_langdetect.detect = lambda text, **kwargs: [{"lang": "zh"}]

from GPT_SoVITS.TTS_infer_pack.TTS import TTS, TTS_Config        # noqa: E402


def load_prompts(limit: int) -> list[dict]:
    rows = list(csv.DictReader(PROMPTS.open(encoding="utf-8"), delimiter="\t"))
    return rows[:limit] if limit else rows


def synthesize(tts, ref: Path, prompt: str, text: str, batch: int, aux: list) -> np.ndarray:
    request = {
        "text": text, "text_lang": "zh",
        "ref_audio_path": str(ref), "prompt_text": prompt, "prompt_lang": "ja",
        "top_k": 15, "top_p": 1.0, "temperature": 1.0,
        "text_split_method": "cut5", "batch_size": max(1, batch),
        "speed_factor": 1.0, "seed": 42, "media_type": "wav",
        "streaming_mode": False,
        "parallel_infer": os.environ.get("VOICE_PARALLEL", "1") == "1",
        "repetition_penalty": 1.35,
        "return_fragment": True,
        "return_sentences": True,      # 本项目对 TTS.py 的补丁：逐句返回音频
        "aux_ref_audio_paths": aux,
    }
    pieces = []
    for _rate, chunk in tts.run(request):
        if chunk is None or len(chunk) == 0:
            continue
        if chunk.dtype != np.int16:
            chunk = (np.clip(chunk, -1.0, 1.0) * 32767.0).astype(np.int16)
        pieces.append(chunk)
    if not pieces:
        raise RuntimeError("no audio produced")
    return pieces


def split_by_silence(audio: np.ndarray, expected: int) -> list[np.ndarray]:
    """按能量切分，并用“合并/再切”保证段数正好等于 expected。

    合并策略：相邻段间隔最小的先合并；段数不足时把最长段在最低能量处再切一刀。
    任何一段长度落在 [0.08, 1.5] s 之外就判为失败，交给逐条兜底。
    """
    if expected <= 1:
        return [audio]
    samples = audio.astype(np.float32) / 32768.0
    frame = int(RATE * 0.01)
    frames = len(samples) // frame
    if frames < expected * 4:
        return []
    rms = np.sqrt(np.mean(samples[:frames * frame].reshape(frames, frame) ** 2, axis=1) + 1e-12)
    voiced = 20 * np.log10(rms + 1e-12) > -50.0
    segments, start = [], None
    for index, flag in enumerate(voiced):
        if flag and start is None:
            start = index
        elif not flag and start is not None:
            if index - start >= 3:
                segments.append([start, index])
            start = None
    if start is not None and frames - start >= 3:
        segments.append([start, frames])
    if not segments:
        return []
    while len(segments) > expected:
        gaps = [(segments[i + 1][0] - segments[i][1], i) for i in range(len(segments) - 1)]
        _, index = min(gaps)
        segments[index] = [segments[index][0], segments[index + 1][1]]
        del segments[index + 1]
    while len(segments) < expected:
        index = max(range(len(segments)), key=lambda k: segments[k][1] - segments[k][0])
        begin, end = segments[index]
        span = end - begin
        if span < 8:
            return []
        low, high = begin + max(1, span // 5), end - max(1, span // 5)
        cut = low + int(np.argmin(rms[low:high])) if high > low else (begin + end) // 2
        segments[index:index + 1] = [[begin, cut], [cut, end]]
    pieces = []
    for begin, end in segments:
        lo = max(0, begin * frame - int(RATE * 0.06))
        hi = min(len(audio), end * frame + int(RATE * 0.06))
        piece = audio[lo:hi]
        if not (0.08 * RATE <= len(piece) <= 1.5 * RATE):
            return []
        pieces.append(piece)
    return pieces


def write_unit(audio: np.ndarray, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    raw = destination.with_suffix(".raw.wav")
    sf.write(str(raw), audio, RATE, subtype="PCM_16")
    command = [
        "ffmpeg", "-nostdin", "-y", "-loglevel", "error", "-i", str(raw),
        "-af", "silenceremove=start_periods=1:start_silence=0.015:start_threshold=-45dB,"
               "areverse,silenceremove=start_periods=1:start_silence=0.015:start_threshold=-45dB,"
               "areverse,loudnorm=I=-20:TP=-2:LRA=7",
        "-ar", "16000", "-ac", "1", "-c:a", "pcm_s16le",
        "-map_metadata", "-1", "-fflags", "+bitexact", str(destination),
    ]
    subprocess.run(command, check=True, timeout=60)
    raw.unlink(missing_ok=True)


def character_jobs(character: str):
    transcripts = json.loads((REF_DIR / "dialog.json").read_text(encoding="utf-8"))
    ref = REF_DIR / f"{character}.mp3"
    prompt = re.sub(r"[A-Za-z]+", " ", str(transcripts.get(character) or "")).strip()
    if not ref.is_file() or not prompt:
        raise SystemExit(f"缺少参考音频或转写: {character}")
    return ref, prompt


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--character", action="append", default=[])
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--batch", type=int, default=10)
    parser.add_argument("--no-resume", action="store_true")
    args = parser.parse_args()

    characters = CHARACTERS if args.all else (args.character or ["kasumi"])
    prompts = load_prompts(args.limit)
    aux = [str(ZH_AUX)] if ZH_AUX.is_file() else []
    print(f"config : {CONFIG}")
    print(f"units  : {len(prompts)} 条 × {len(characters)} 角色，batch={args.batch}")

    started = time.time()
    cfg = TTS_Config(str(CONFIG))
    for attr in ("bert_base_path", "cnhuhbert_base_path", "t2s_weights_path", "vits_weights_path"):
        value = getattr(cfg, attr, None)
        if value and not Path(str(value)).is_absolute():
            setattr(cfg, attr, str(REPO / str(value)))
    tts = TTS(cfg)
    import torch
    if torch.cuda.is_available():
        print(f"gpu    : {torch.cuda.get_device_name(0)}  hip {torch.version.hip}  "
              f"half={getattr(cfg, 'is_half', None)}")
    print(f"loaded in {time.time() - started:.1f}s")

    index = []
    for character in characters:
        ref, reference_prompt = character_jobs(character)
        out_dir = OUT_ROOT / character / "units"
        out_dir.mkdir(parents=True, exist_ok=True)
        pending = [row for row in prompts
                   if args.no_resume or not (
                       (out_dir / f"{row['syllable']}.wav").exists()
                       and (out_dir / f"{row['syllable']}.wav").stat().st_size > 2000)]
        done = fallback = 0
        for start in range(0, len(pending), args.batch):
            group = pending[start:start + args.batch]
            text = "。".join(row["char"] for row in group) + "。"
            try:
                pieces = synthesize(tts, ref, reference_prompt, text, len(group), aux)
            except Exception as error:
                pieces = []
                print(f"  batch fail: {type(error).__name__}", flush=True)
            if len(pieces) == len(group):
                for row, piece in zip(group, pieces):
                    write_unit(piece, out_dir / f"{row['syllable']}.wav")
                    index.append({"character": character, "syllable": row["syllable"], "status": "batch"})
                done += len(group)
            else:
                fallback += len(group)
                for row in group:
                    try:
                        single = synthesize(tts, ref, reference_prompt, row["char"], 1, aux)[0]
                    except Exception as error:
                        index.append({"character": character, "syllable": row["syllable"],
                                      "status": f"fail:{type(error).__name__}"})
                        continue
                    write_unit(single, out_dir / f"{row['syllable']}.wav")
                    index.append({"character": character, "syllable": row["syllable"], "status": "single"})
                    done += 1
            if done and done % 50 == 0:
                elapsed = time.time() - started
                print(f"  {character:8s} {done:5d}/{len(pending)}  "
                      f"{elapsed / max(done, 1):.2f}s/条  兜底 {fallback}", flush=True)
        print(f"{character}: 完成 {done}/{len(pending)}（兜底 {fallback}），用时 {time.time() - started:.0f}s")

    (OUT_ROOT / "render_index.json").write_text(
        json.dumps(index, ensure_ascii=False, indent=1) + "\n", encoding="utf-8")
    ok = [i for i in index if i["status"] in ("batch", "single")]
    if ok:
        total = time.time() - started
        print(f"总计 {len(ok)} 条，用时 {total:.0f}s，平均 {total / len(ok):.2f}s/条")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
