#!/usr/bin/env python3
"""把 probe_tv_sources.py 的结果合并回频道数据。

规则：
  * 探测失败的源直接删除；
  * 一个源都不剩的电视台整个删除；
  * 探测到的码率（kbps）写回源的 bitrate 字段，供播放页的线路弹窗显示。

用法：
  python3 tools/apply_tv_probe.py --channels china.json --report report.json [--dry-run]
"""

import argparse
import json
import os


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--channels", required=True)
    parser.add_argument("--report", required=True)
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    with open(args.channels, encoding="utf-8") as handle:
        data = json.load(handle)
    with open(args.report, encoding="utf-8") as handle:
        report = json.load(handle)

    removed_sources = 0
    removed_channels = []
    with_bitrate = 0
    for category in data.get("categories", []):
        kept_channels = []
        for channel in category.get("channels", []):
            sources = []
            for source in channel.get("sources", []):
                result = report.get(source["url"])
                if result is None:
                    print(f"  ? no report for {source['url']} (kept)")
                    sources.append(source)
                    continue
                if not result.get("ok"):
                    removed_sources += 1
                    print(f"  - drop source {channel['name']}: {source['url']} "
                          f"({result.get('error')})")
                    continue
                if result.get("bitrate", 0) > 0:
                    source["bitrate"] = int(result["bitrate"])
                    with_bitrate += 1
                sources.append(source)
            if sources:
                channel["sources"] = sources
                kept_channels.append(channel)
            else:
                removed_channels.append(f"{category.get('title')}/{channel.get('name')}")
        category["channels"] = kept_channels

    data["categories"] = [category for category in data.get("categories", [])
                          if category.get("channels")]

    print(f"removed sources: {removed_sources}")
    print(f"removed channels: {len(removed_channels)} -> {', '.join(removed_channels)}")
    print(f"bitrate written: {with_bitrate}")

    if args.dry_run:
        return
    temporary = args.channels + ".tmp"
    with open(temporary, "w", encoding="utf-8") as handle:
        json.dump(data, handle, ensure_ascii=False, indent=2)
        handle.write("\n")
    os.replace(temporary, args.channels)


if __name__ == "__main__":
    main()
