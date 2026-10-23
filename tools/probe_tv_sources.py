#!/usr/bin/env python3
"""批量检测 china.json 里的电视源是否真的可用。

判定标准（贴近播放器的真实行为）：
  1. 取 m3u8（自动跟随 302），必须是 #EXTM3U 的播放列表；
  2. 主播放列表（#EXT-X-STREAM-INF）先挑一路变体再取媒体列表；
  3. 从媒体列表里取最后一个分片（失败再试第一个），用 Range 只拉前 32KB，
     必须返回 200/206 且内容是二进制（不是错误页）；
  4. 能拿到 Content-Length 与 #EXT-X-TARGETDURATION 时估算码率（kbps）。

用法：
  python3 tools/probe_tv_sources.py --input china.json --output report.json --workers 6
"""

import argparse
import concurrent.futures
import json
import re
import time
import urllib.error
import urllib.parse
import urllib.request

UA = ("Mozilla/5.0 (X11; Linux aarch64) AppleWebKit/537.36 (KHTML, like Gecko) "
      "QtWebEngine/6.4.2 Chrome/102.0.5005.115 Safari/537.36")


def fetch(url, timeout, max_bytes, byte_range=False):
    headers = {"User-Agent": UA, "Accept": "*/*"}
    # 播放列表不要带 Range：部分源会回 206，看起来像“取列表失败”
    if byte_range:
        headers["Range"] = f"bytes=0-{max_bytes - 1}"
    request = urllib.request.Request(url, headers=headers)
    with urllib.request.urlopen(request, timeout=timeout) as response:
        data = response.read(max_bytes)
        return response.status, data, response.headers, response.geturl()


def parse_playlist(text, base_url):
    """返回 (kind, info)。kind: master / media / unknown"""
    if not text.lstrip().startswith("#EXTM3U"):
        return "unknown", {}
    lines = [line.strip() for line in text.splitlines()]
    if any(line.startswith("#EXT-X-STREAM-INF") for line in lines):
        variants = []
        for index, line in enumerate(lines):
            if not line.startswith("#EXT-X-STREAM-INF"):
                continue
            for candidate in lines[index + 1:]:
                if candidate and not candidate.startswith("#"):
                    variants.append(candidate)
                    break
        return "master", {"variants": variants}

    target = 0.0
    segments = []
    for line in lines:
        if line.startswith("#EXT-X-TARGETDURATION:"):
            try:
                target = float(line.split(":", 1)[1])
            except ValueError:
                target = 0.0
        elif line and not line.startswith("#"):
            segments.append(line)
    return "media", {"segments": segments, "target": target}


def download_segment(url, timeout, max_seconds, max_bytes):
    """完整/限时下载分片，返回 (收到字节, 耗时秒, 总长度)。"""
    started = time.monotonic()
    received = 0
    total = 0
    request = urllib.request.Request(url, headers={"User-Agent": UA, "Accept": "*/*"})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        if response.status not in (200, 206):
            return 0, 0.0, 0
        length = response.headers.get("Content-Length")
        content_range = response.headers.get("Content-Range")
        if content_range and "/" in content_range:
            try:
                total = int(content_range.rsplit("/", 1)[1])
            except ValueError:
                total = 0
        elif length:
            try:
                total = int(length)
            except ValueError:
                total = 0
        while True:
            elapsed = time.monotonic() - started
            if elapsed > max_seconds or received > max_bytes:
                break
            chunk = response.read(64 * 1024)
            if not chunk:
                break
            received += len(chunk)
    return received, time.monotonic() - started, total


def probe_source(url, timeout, deep=True):
    started = time.monotonic()
    result = {"url": url, "ok": False, "ms": 0, "bitrate": 0, "error": ""}
    try:
        status, data, headers, final_url = fetch(url, timeout, 64 * 1024)
        if status not in (200, 206):
            result["error"] = f"playlist http {status}"
            return result
        kind, info = parse_playlist(data.decode("utf-8", "replace"), final_url)
        if kind == "unknown":
            result["error"] = "not a m3u8"
            return result

        playlist_url = final_url
        if kind == "master":
            if not info["variants"]:
                result["error"] = "empty master playlist"
                return result
            variant_url = urllib.parse.urljoin(final_url, info["variants"][0])
            status, data, headers, final_url = fetch(variant_url, timeout, 64 * 1024)
            if status not in (200, 206):
                result["error"] = f"variant http {status}"
                return result
            kind, info = parse_playlist(data.decode("utf-8", "replace"), final_url)
            if kind != "media" or not info["segments"]:
                result["error"] = "variant not media playlist"
                return result
            playlist_url = final_url

        segments = info["segments"]
        target = float(info.get("target") or 0)
        # 直播常用最后一个分片；个别源会 404，再退回第一个
        for segment in (segments[-1], segments[0]):
            segment_url = urllib.parse.urljoin(playlist_url, segment)
            try:
                if deep:
                    # 真正下整段（限时 12s / 24MB）：既验证内容，也验证“能不能跑满实时”
                    received, elapsed, total = download_segment(segment_url, timeout, 12.0, 24 * 1024 * 1024)
                    if received < 512:
                        result["error"] = f"segment empty ({received}B)"
                        continue
                    if total and target:
                        result["bitrate"] = int(total * 8 / target / 1000)
                    complete = total and received >= total
                    # 一个直播分片的时长是 target，下载耗时必须明显短于它，播放才追得上
                    if target > 0:
                        limit = max(1.0, target * 0.8)
                    else:
                        limit = 6.0
                    if complete and elapsed > limit:
                        result["error"] = f"too slow: {received / max(elapsed, 0.001) / 1024:.0f}KB/s, {elapsed:.1f}s > {limit:.1f}s"
                        continue
                    if not complete and received / max(elapsed, 0.001) < 200 * 1024:
                        result["error"] = f"too slow: {received / max(elapsed, 0.001) / 1024:.0f}KB/s"
                        continue
                    result["ok"] = True
                    result["error"] = ""
                    result["speed"] = round(received / max(elapsed, 0.001) / 1024)
                    break
                status, data, headers, _ = fetch(segment_url, timeout, 32 * 1024, byte_range=True)
            except (urllib.error.URLError, TimeoutError, OSError) as error:
                result["error"] = f"segment: {error}"
                continue
            if status not in (200, 206) or len(data) < 512:
                result["error"] = f"segment http {status} len {len(data)}"
                continue
            head = data[:512].lstrip().lower()
            if head.startswith(b"<html") or head.startswith(b"<!doctype") or head.startswith(b"{"):
                result["error"] = "segment is html/json"
                continue
            result["ok"] = True
            result["error"] = ""
            total = 0
            content_range = headers.get("Content-Range") if headers else None
            content_length = headers.get("Content-Length") if headers else None
            if content_range and "/" in content_range:
                try:
                    total = int(content_range.rsplit("/", 1)[1])
                except ValueError:
                    total = 0
            elif content_length and status == 200:
                try:
                    total = int(content_length)
                except ValueError:
                    total = 0
            if total and info.get("target"):
                result["bitrate"] = int(total * 8 / info["target"] / 1000)
            break
    except Exception as error:  # noqa: BLE001 - 探测脚本，任何异常都算不可用
        result["error"] = f"{type(error).__name__}: {error}"
    finally:
        result["ms"] = int((time.monotonic() - started) * 1000)
    return result


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--workers", type=int, default=6)
    parser.add_argument("--timeout", type=float, default=8.0)
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--shallow", action="store_true", help="只检查可达性，不下载完整分片")
    args = parser.parse_args()

    with open(args.input, encoding="utf-8") as handle:
        data = json.load(handle)

    urls = []
    for category in data.get("categories", []):
        for channel in category.get("channels", []):
            for source in channel.get("sources", []):
                if source["url"] not in urls:
                    urls.append(source["url"])
    if args.limit:
        urls = urls[: args.limit]
    print(f"probing {len(urls)} unique sources with {args.workers} workers", flush=True)

    results = {}
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        futures = {pool.submit(probe_source, url, args.timeout, not args.shallow): url for url in urls}
        for done, future in enumerate(concurrent.futures.as_completed(futures), 1):
            url = futures[future]
            results[url] = future.result()
            mark = "OK " if results[url]["ok"] else "BAD"
            print(f"[{done}/{len(urls)}] {mark} {results[url]['ms']:5d}ms "
                  f"{results[url]['bitrate']:5d}kbps {url} {results[url]['error']}", flush=True)

    alive = sum(1 for item in results.values() if item["ok"])
    print(f"alive {alive}/{len(results)}", flush=True)
    with open(args.output, "w", encoding="utf-8") as handle:
        json.dump(results, handle, ensure_ascii=False, indent=2)


if __name__ == "__main__":
    main()
