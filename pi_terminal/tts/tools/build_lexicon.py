#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把三类来源整理成统一的语音词库包。

来源：
  1. 开源输入法/拼音词库：mozillazg/pinyin-data (MIT)、phrase-pinyin-data (MIT)
  2. Ekho 中文数据：friso 分词词库 + 拼音音节→音素表（Ekho 本体 GPL-2.0+）
  3. 本项目真正会播报的句子：QML announce/speak 调用点 + 已渲染语音包 manifest

输出（本目录）：
  zh_chars.tsv          字 -> 读音（带调/数字调），多音字保留全部
  zh_common_reading.tsv 字 -> 常用读音（kHanyuPinlu 频序）
  zh_phrases.tsv        词 -> 读音（412k 词条，多音字按词消歧）
  zh_phrase_overrides.tsv 人工纠正的读音
  ekho_symbols.tsv      拼音音节 -> Ekho 音素码（2061 条，引擎实际能发的音）
  ekho_lex_main.tsv     Ekho 分词词库
  project_phrases.tsv   本项目播报语句 + 英文对照 + 出处
  provenance.json       来源 URL / commit / 许可证 / SHA256
"""
from __future__ import annotations

import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SRC = ROOT / "src"

TONES = {
    "ā": ("a", 1), "á": ("a", 2), "ǎ": ("a", 3), "à": ("a", 4),
    "ē": ("e", 1), "é": ("e", 2), "ě": ("e", 3), "è": ("e", 4),
    "ī": ("i", 1), "í": ("i", 2), "ǐ": ("i", 3), "ì": ("i", 4),
    "ō": ("o", 1), "ó": ("o", 2), "ǒ": ("o", 3), "ò": ("o", 4),
    "ū": ("u", 1), "ú": ("u", 2), "ǔ": ("u", 3), "ù": ("u", 4),
    "ǖ": ("v", 1), "ǘ": ("v", 2), "ǚ": ("v", 3), "ǜ": ("v", 4),
    "ü": ("v", 5),
    "ń": ("n", 2), "ň": ("n", 3), "ǹ": ("n", 4),
    "ḿ": ("m", 2),
}


def to_numbered(syllable: str) -> str:
    """把带调拼音转成 Ekho 风格的数字调（ü -> v，轻声记 5）。"""
    out, tone = [], 0
    for ch in syllable:
        if ch in TONES:
            base, tone = TONES[ch]
            out.append(base)
        elif ch == "ê":
            out.append("e")
            tone = tone or 5
        else:
            out.append(ch)
    if not out:
        return ""
    return "".join(out) + str(tone or 5)


def read_lines(path: Path):
    with path.open(encoding="utf-8", errors="replace") as handle:
        for line in handle:
            line = line.strip()
            if line and not line.startswith("#"):
                yield line


def build_chars() -> int:
    pattern = re.compile(r"^U\+[0-9A-Fa-f]+:\s*([^#]+)#\s*(\S.*)$")
    rows = 0
    with (ROOT / "zh_chars.tsv").open("w", encoding="utf-8") as out:
        out.write("char\treadings\tnumbered\tprimary\n")
        for line in read_lines(SRC / "pinyin-data" / "pinyin.txt"):
            match = pattern.match(line)
            if not match:
                continue
            readings = [r.strip() for r in match.group(1).split(",") if r.strip()]
            char = match.group(2).strip()
            if not readings or not char:
                continue
            numbered = ",".join(to_numbered(r) for r in readings)
            out.write(f"{char}\t{','.join(readings)}\t{numbered}\t{readings[0]}\n")
            rows += 1
    return rows


def build_common_reading() -> int:
    pattern = re.compile(r"^U\+[0-9A-Fa-f]+:\s*([^#]+)#\s*(\S.*)$")
    rows = 0
    with (ROOT / "zh_common_reading.tsv").open("w", encoding="utf-8") as out:
        out.write("char\tcommon\tcommon_numbered\n")
        for line in read_lines(SRC / "pinyin-data" / "kHanyuPinlu.txt"):
            match = pattern.match(line)
            if not match:
                continue
            # kHanyuPinlu 会把多个读音写在一行（按频率排序），只取最高频那个
            alternatives = [r.strip() for r in match.group(1).split(",") if r.strip()]
            reading = alternatives[0] if alternatives else ""
            char = match.group(2).strip()
            if not reading or not char:
                continue
            out.write(f"{char}\t{reading}\t{to_numbered(reading)}\n")
            rows += 1
    return rows


def build_phrases() -> int:
    rows = 0
    seen = set()
    with (ROOT / "zh_phrases.tsv").open("w", encoding="utf-8") as out:
        out.write("phrase\treadings\tnumbered\n")
        for path in ("large_pinyin.txt", "pinyin.txt"):
            for line in read_lines(SRC / "phrase-pinyin-data" / path):
                if ":" not in line:
                    continue
                phrase, _, reading = line.partition(":")
                phrase, reading = phrase.strip(), reading.strip()
                if not phrase or not reading:
                    continue
                if phrase in seen:
                    continue
                seen.add(phrase)
                syllables = reading.split()
                out.write(f"{phrase}\t{reading}\t{' '.join(to_numbered(s) for s in syllables)}\n")
                rows += 1
    return rows


def build_overrides() -> int:
    rows = 0
    with (ROOT / "zh_phrase_overrides.tsv").open("w", encoding="utf-8") as out:
        out.write("phrase\treadings\tnumbered\n")
        for line in read_lines(SRC / "phrase-pinyin-data" / "overwrite.txt"):
            if ":" not in line:
                continue
            phrase, _, reading = line.partition(":")
            phrase, reading = phrase.strip(), reading.split("#")[0].strip()
            if not phrase or not reading:
                continue
            syllables = [s for s in reading.split() if s]
            out.write(f"{phrase}\t{reading}\t{' '.join(to_numbered(s) for s in syllables)}\n")
            rows += 1
    return rows


def build_ekho_symbols() -> int:
    source = SRC / "ekho" / "src" / "zh_symbol_map.cpp"
    rows = 0
    seen = set()
    with (ROOT / "ekho_symbols.tsv").open("w", encoding="utf-8") as out:
        out.write("syllable\tcode\n")
        found = set()
        for match in re.finditer(r'\{"([a-z]+[1-5])",\s*(\d+)\}', source.read_text(encoding="utf-8", errors="replace")):
            found.add((match.group(1), match.group(2)))
        for syllable, code in sorted(found, key=lambda item: (item[0][:-1], item[0][-1])):
            if syllable in seen:
                continue
            seen.add(syllable)
            out.write(f"{syllable}\t{code}\n")
            rows += 1
    return rows


def build_ekho_lexicon() -> int:
    rows = 0
    with (ROOT / "ekho_lex_main.tsv").open("w", encoding="utf-8") as out:
        out.write("word\tfreq\n")
        for line in read_lines(SRC / "ekho" / "ekho-data" / "friso-dict" / "lex-main.lex"):
            word, _, freq = line.partition("/")
            word = word.strip()
            if not word:
                continue
            freq = (freq.strip() or "0")
            if freq == "null":
                freq = "0"
            out.write(f"{word}\t{freq}\n")
            rows += 1
    return rows


def i18n_pairs(repo: Path) -> dict:
    path = repo / "pi_terminal" / "qml" / "I18n.qml"
    if not path.exists():
        return {}
    text = path.read_text(encoding="utf-8", errors="replace")
    return {zh: en for zh, en in re.findall(r'"([^"]+)":"([^"]*)"', text)}


def build_project_phrases(repo: Path) -> int:
    """本项目会播报的句子：QML 调用点 + 已渲染语音包。"""
    english = i18n_pairs(repo)
    spoken = [
        ("便笺已保存", "qml:Main.qml:602 announce()"),
        ("最小化应用", "qml:Main.qml:706 announce()"),
        ("正在退出", "qml:Main.qml:752 announce()"),
        ("关闭当前应用", "qml:Main.qml:1289 announce()"),
        ("请求断开模块", "qml:Main.qml:1324 announce()"),
        ("控制桥已断开，请检查USB连接", "qml:Main.qml:1077 speak()"),
        ("切换到", "qml:Main.qml:741 announce() 动态前缀"),
        ("存储设备已接入", "voicepack:final/*/phrases.tsv"),
        ("存储设备已移除", "voicepack:final/*/phrases.tsv"),
        ("网络连接已断开", "voicepack:final/*/phrases.tsv"),
        ("网络连接已恢复", "voicepack:final/*/phrases.tsv"),
        ("即将关机，请确认", "voicepack:final/*/phrases.tsv"),
        ("即将重启，请确认", "voicepack:final/*/phrases.tsv"),
        ("语音播报已切换到当前声音。", "voicepack:final/*/phrases.tsv"),
    ]
    with (ROOT / "project_phrases.tsv").open("w", encoding="utf-8") as out:
        out.write("zh\ten\tsource\n")
        for zh, source in spoken:
            out.write(f"{zh}\t{english.get(zh, '')}\t{source}\n")
    return len(spoken)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git_head(repo: Path) -> str:
    try:
        return subprocess.check_output(
            ["git", "-C", str(repo), "rev-parse", "HEAD"], text=True
        ).strip()
    except Exception:
        return "unknown"


def main() -> int:
    repo = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else Path(
        "/home/yuanzingo/wark/Multi-Function-ESP32-Pi-Terminal"
    )
    counts = {
        "zh_chars": build_chars(),
        "zh_common_reading": build_common_reading(),
        "zh_phrases": build_phrases(),
        "zh_phrase_overrides": build_overrides(),
        "ekho_symbols": build_ekho_symbols(),
        "ekho_lex_main": build_ekho_lexicon(),
        "project_phrases": build_project_phrases(repo),
    }

    outputs = [
        "zh_chars.tsv", "zh_common_reading.tsv", "zh_phrases.tsv",
        "zh_phrase_overrides.tsv", "ekho_symbols.tsv", "ekho_lex_main.tsv",
        "project_phrases.tsv",
    ]
    provenance = {
        "generated_by": "build_lexicon.py",
        "counts": counts,
        "sources": [
            {
                "name": "pinyin-data",
                "url": "https://github.com/mozillazg/pinyin-data",
                "commit": git_head(SRC / "pinyin-data"),
                "license": "MIT (c) 2016 mozillazg",
                "used_for": "zh_chars.tsv, zh_common_reading.tsv",
            },
            {
                "name": "phrase-pinyin-data",
                "url": "https://github.com/mozillazg/phrase-pinyin-data",
                "commit": git_head(SRC / "phrase-pinyin-data"),
                "license": "MIT (c) 2017 mozillazg",
                "used_for": "zh_phrases.tsv, zh_phrase_overrides.tsv",
            },
            {
                "name": "ekho",
                "url": "https://github.com/hgneng/ekho",
                "commit": git_head(SRC / "ekho"),
                "license": "GPL-2.0-or-later (engine + data)",
                "used_for": "ekho_symbols.tsv, ekho_lex_main.tsv",
            },
            {
                "name": "project sources",
                "url": "https://github.com/Axe665/Multi-Function-ESP32-Pi-Terminal",
                "commit": git_head(repo),
                "license": "repository has no root LICENSE",
                "used_for": "project_phrases.tsv",
            },
        ],
        "outputs": {name: {"sha256": sha256(ROOT / name), "bytes": (ROOT / name).stat().st_size} for name in outputs},
    }
    (ROOT / "provenance.json").write_text(
        json.dumps(provenance, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    print(json.dumps(counts, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
