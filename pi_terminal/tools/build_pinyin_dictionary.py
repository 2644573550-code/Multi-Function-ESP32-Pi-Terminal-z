#!/usr/bin/env python3
"""Build a tone-free, ranked phrase table from bundled Rime/PyPinyin data."""
from __future__ import annotations

import json
import math
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets/pinyin"
PHRASES = ASSETS / "phrases_dict.json"
CHARACTERS = ASSETS / "pinyin_simp.dict.yaml"
OUTPUT = ASSETS / "phrases.tsv"
LIMIT_PER_PINYIN = 24

tone_mapping = {}
for source, replacement in (("āáǎà", "a"), ("ēéěè", "e"), ("īíǐì", "i"),
                            ("ōóǒò", "o"), ("ūúǔù", "u"), ("ǖǘǚǜü", "v"),
                            ("ńňǹ", "n"), ("ḿ", "m")):
    tone_mapping.update({char: replacement for char in source})
tone_map = str.maketrans(tone_mapping)

character_frequency: dict[str, int] = {}
for line in CHARACTERS.read_text(encoding="utf-8").splitlines():
    fields = line.split("\t")
    if len(fields) < 3 or not re.fullmatch(r"[a-zv]+", fields[1]):
        continue
    try:
        frequency = int(fields[2])
    except ValueError:
        continue
    character_frequency[fields[0]] = max(character_frequency.get(fields[0], 0), frequency)

phrase_data = json.loads(PHRASES.read_text(encoding="utf-8"))
ranked: dict[str, dict[str, int]] = {}
for word, syllables in phrase_data.items():
    if len(word) < 2 or not all("\u3400" <= char <= "\u9fff" for char in word):
        continue
    pinyin = "".join(reading[0] for reading in syllables if reading)
    pinyin = pinyin.translate(tone_map).lower()
    if not pinyin or len(pinyin) > 24 or not re.fullmatch(r"[a-zv]+", pinyin):
        continue
    # Frequency-ranked character evidence plus a lexical-entry bonus keeps
    # real dictionary phrases ahead of arbitrary homophone combinations.
    weight = 500 + round(sum(math.log1p(character_frequency.get(char, 1)) * 100
                             for char in word))
    bucket = ranked.setdefault(pinyin, {})
    bucket[word] = max(bucket.get(word, 0), weight)

with OUTPUT.open("w", encoding="utf-8", newline="\n") as stream:
    count = 0
    for pinyin in sorted(ranked):
        candidates = sorted(ranked[pinyin].items(), key=lambda item: (-item[1], item[0]))
        for word, weight in candidates[:LIMIT_PER_PINYIN]:
            stream.write(f"{pinyin}\t{word}\t{weight}\n")
            count += 1

print(f"Wrote {count} phrase candidates for {len(ranked)} pinyin strings to {OUTPUT}")
