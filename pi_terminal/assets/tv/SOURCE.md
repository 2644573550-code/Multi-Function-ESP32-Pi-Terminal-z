# 网络电视频道数据来源

`china.json` 由 `tools/build_tv_channels.py` 生成，**不是手写维护的**。

## 数据来源

| 项 | 值 |
| --- | --- |
| 上游列表 | <https://github.com/best-fan/iptv-sources> |
| 具体文件 | `cn_all_status.m3u8`（每日检测后仍存活的条目） |
| 拉取时间 | 见 `china.json` 的 `generated` 字段 |

生成器会：解析 `#EXTINF` 的 `tvg-name` / `tvg-logo` / `group-title`，统一 CCTV 命名
（`CCTV1`/`CCTV-1` → `CCTV-1`），按台名去重合并多条源，并按分辨率排序
（576/540 优先，其次 720，最后 1080）——因为目标设备是树莓派 3B，1080p 硬解吃力。

## 重新生成

```sh
python3 tools/build_tv_channels.py            # 联网拉取最新
python3 tools/build_tv_channels.py --offline  # 用 .cache/ 里的副本重建
```

## ⚠️ 边界与风险

- 这些是**第三方公开聚合的直连流**，不是央视/各卫视的官方授权分发。**仅限个人设备使用，不要对外分发或商用。**
- 很多地址是运营商 IPTV 接口（形如 `?key=txiptv`），**可能限省网、限运营商**，换网络就失效。
- 链接随时可能失效，没有可用性保证。应用侧必须做「同频道多源自动重试」，并且**失败时给出明确提示**，不能静默卡住。
- 本目录的 `.cache/` 是生成过程的中间产物，已在 `.gitignore` 中忽略。
- 台标 URL 同样来自第三方（gitee/imgur），用途仅为显示。**当前 UI 会按 `channel.logo` 异步加载远程台标，加载失败时回退到内置图标**（见 `qml/TvPlayer.qml`）；因此离线或域名不可达时只会缺图，不影响播放。
