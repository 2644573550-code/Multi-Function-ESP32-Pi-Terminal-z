#!/usr/bin/env python3
"""生成终端内置的中国网络电视频道表。

数据来源是第三方公开聚合列表（见 pi_terminal/assets/tv/SOURCE.md），
本脚本只做拉取、归类、去重和排序，不保证任何流长期有效。

用法:
    python3 tools/build_tv_channels.py [--offline]

输出:
    pi_terminal/assets/tv/china.json
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import urllib.request
from datetime import date
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "pi_terminal" / "assets" / "tv" / "china.json"
CACHE = ROOT / "pi_terminal" / "assets" / "tv" / ".cache"

# 单一来源：每日检测过的完整中国频道表
SOURCE_URL = "https://raw.githubusercontent.com/best-fan/iptv-sources/main/cn_all_status.m3u8"

CATEGORIES = [
    ("cctv", "央视"),
    ("local", "地方省市"),
    ("other", "其他"),
]

ATTR = re.compile(r'([a-zA-Z0-9_-]+)="([^"]*)"')
QUALITY_PAREN = re.compile(r'\((1080p|720p|576i|540p|480p|[0-9]+i)\)', re.IGNORECASE)
QUALITY_TAG = re.compile(r'\[(\d{3,4})\]')
QUALITY_WORD = re.compile(r'\[(HD|SD|S|UHD)\]', re.IGNORECASE)
BRACKET_TAG = re.compile(r'\[[^\]]*\]')
CCTV_CANON = re.compile(r'^\s*CCTV[\s_-]*0*(\d{1,2})\s*(\+)?\s*$', re.IGNORECASE)
IS_CNTV = re.compile(r'CCTV|CGTN|央视', re.IGNORECASE)


def fetch(url: str) -> str:
    request = urllib.request.Request(url, headers={"User-Agent": "orange-pi-terminal-tv-build/1.0"})
    with urllib.request.urlopen(request, timeout=30) as response:
        return response.read().decode("utf-8", "replace")


def parse_extinf(line: str) -> dict:
    head, _, tail = line.partition(",")
    attributes = dict(ATTR.findall(head))
    return {
        "name": attributes.get("tvg-name", "").strip(),
        "logo": attributes.get("tvg-logo", "").strip(),
        "group": attributes.get("group-title", "").strip(),
        "title": tail.strip(),
    }


def parse(text: str) -> list[dict]:
    entries: list[dict] = []
    pending: dict | None = None
    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue
        if line.startswith("#EXTINF"):
            pending = parse_extinf(line)
        elif line.startswith("#"):
            continue
        elif pending is not None:
            if line.startswith(("http://", "https://")):
                pending["url"] = line
                entries.append(pending)
            pending = None
    return entries


def clean_name(entry: dict) -> str:
    name = entry["name"] or entry["title"]
    name = BRACKET_TAG.sub("", name)
    name = QUALITY_PAREN.sub("", name).strip()
    canon = CCTV_CANON.match(name)
    if canon:
        return f"CCTV-{int(canon.group(1))}{'+' if canon.group(2) else ''}"
    return name


def quality_rank(entry: dict) -> int:
    """排序权重：越小越优先。Pi 3B 硬解 1080p 吃力，低分辨率更稳。"""
    blob = f'{entry.get("title", "")} {entry.get("name", "")}'
    tag = QUALITY_TAG.search(blob)
    if tag:
        height = int(tag.group(1))
        if height <= 576:
            return 0
        if height <= 720:
            return 1
        return 3
    paren = QUALITY_PAREN.search(blob)
    if paren:
        value = paren.group(1).lower()
        if value.endswith("i") or value in ("576i", "540p", "480p"):
            return 0
        if value == "720p":
            return 1
        return 3
    word = QUALITY_WORD.search(blob)
    if word and word.group(1).lower() in ("sd", "s"):
        return 0
    return 2


def stream_height(entry: dict) -> int:
    """从名称标签里推断纵向分辨率；未知返回 0。"""
    blob = f'{entry.get("title", "")} {entry.get("name", "")}'
    tag = QUALITY_TAG.search(blob)
    if tag:
        return int(tag.group(1))
    paren = QUALITY_PAREN.search(blob)
    if paren:
        value = paren.group(1).lower()
        if value.endswith("i"):
            return int(value[:-1])
        return int(value[:-1]) if value[:-1].isdigit() else 0
    return 0


def category_of(entry: dict) -> str:
    """台名优先：CCTV/CGTN 归央视，卫视归地方省市，其余进其他。"""
    name = clean_name(entry)
    group = entry.get("group", "")
    if IS_CNTV.search(name) or "央视" in group:
        return "cctv"
    if "卫视" in group or "卫视" in name:
        return "local"
    return "other"


def channel_sort_key(name: str) -> tuple:
    canon = re.match(r"^CCTV-(\d+)(\+)?$", name, re.IGNORECASE)
    if canon:
        return (0, int(canon.group(1)), 0 if canon.group(2) else 1, name)
    if IS_CNTV.search(name):
        return (1, 0, 0, name)
    return (2, 0, 0, name)


def ranked_sources(channel: dict, limit: int = 4) -> list[dict]:
    """按分辨率从低到高排序，并显式带上高度，供前端按设备能力筛选。"""
    best: dict[str, tuple[int, int]] = {}
    for rank, url, height in channel["items"]:
        if url not in best or rank < best[url][0]:
            best[url] = (rank, height)
    ordered = sorted(best.items(), key=lambda item: item[1][0])
    return [{"url": url, "height": meta[1]} for url, meta in ordered][:limit]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--offline", action="store_true", help="只用本地缓存重建，不访问网络")
    args = parser.parse_args()

    CACHE.mkdir(parents=True, exist_ok=True)
    cache_file = CACHE / "cn_all_status.m3u"

    try:
        if args.offline:
            text = cache_file.read_text(encoding="utf-8")
        else:
            text = fetch(SOURCE_URL)
            cache_file.write_text(text, encoding="utf-8")
    except Exception as error:
        print(f"error: 拉取失败 ({error})", file=sys.stderr)
        return 1

    buckets: dict[str, dict[str, dict]] = {key: {} for key, _ in CATEGORIES}

    for entry in parse(text):
        name = clean_name(entry)
        if not name:
            continue
        bucket = buckets[category_of(entry)]
        channel = bucket.setdefault(name, {"name": name, "logo": entry["logo"], "items": []})
        if not channel["logo"] and entry["logo"]:
            channel["logo"] = entry["logo"]
        channel["items"].append((quality_rank(entry), entry["url"], stream_height(entry)))

    payload = {
        "generated": date.today().isoformat(),
        "source": SOURCE_URL,
        "note": "第三方公开聚合列表，链接不保证长期有效；仅个人使用。",
        "categories": [],
    }

    for key, title in CATEGORIES:
        channels = sorted(buckets[key].values(), key=lambda c: channel_sort_key(c["name"]))
        payload["categories"].append({
            "id": key,
            "title": title,
            "channels": [
                {"name": c["name"], "logo": c["logo"], "sources": ranked_sources(c)}
                for c in channels
            ],
        })

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps(payload, ensure_ascii=False, indent=1), encoding="utf-8")

    total = sum(len(c["channels"]) for c in payload["categories"])
    for category in payload["categories"]:
        print(f"  {category['title']}: {len(category['channels'])} 个频道")
    print(f"  合计 {total} 个频道 -> {OUT.relative_to(ROOT)} ({OUT.stat().st_size // 1024} KiB)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
