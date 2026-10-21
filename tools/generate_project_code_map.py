#!/usr/bin/env python3
"""Generate a complete first-party source inventory and architecture map."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "docs" / "project-code-map.md"
CODE_SUFFIXES = {
    ".c", ".cc", ".cpp", ".h", ".hh", ".hpp", ".ino", ".js", ".mjs",
    ".cjs", ".ts", ".tsx", ".jsx", ".vue", ".qml", ".py", ".sh", ".bash",
    ".cmake", ".html", ".css", ".rs",
}
ROOTS = ("build_scripts", "docs", "esp", "pi_terminal", "third_party", "tools")
SKIP_PARTS = {".git", "node_modules", "__pycache__"}


def source_files() -> list[str]:
    files = []
    for path in ROOT.rglob("*"):
        if not path.is_file() or any(part in SKIP_PARTS for part in path.parts):
            continue
        if path.name == "CMakeLists.txt" or path.suffix.lower() in CODE_SUFFIXES:
            files.append(path.relative_to(ROOT).as_posix())
    return sorted(files)


def main() -> None:
    files = source_files()
    grouped = {name: [p for p in files if p.startswith(name + "/")] for name in ROOTS}
    lines = [
        "# 项目代码结构图（QML / 源码总览）",
        "",
        "> 自动生成。重新盘点并更新本文：`python3 tools/generate_project_code_map.py`。",
        "> 文件清单包含仓库内维护的代码/构建脚本（含 `third_party/YesPlayMusic`），排除 `.git`、`node_modules`、`__pycache__`、构建输出及非代码素材/配置。",
        "",
        "## 架构简图",
        "",
        "```mermaid",
        "flowchart TB",
        "    User[用户 / 触控] --> QML[Qt Quick / QML 界面]",
        "    QML --> App[Qt 应用生命周期与路由]",
        "    App --> Core[连接、会话、模块与页面模型]",
        "    Core --> Protocol[PiBridge 协议 / BLE / LAN / Pogo 控制]",
        "    Core --> Plugin[外部 ESP 模块 C ABI]",
        "    QML --> UI[terminal_ui 原生 UI 库]",
        "    UI --> Music[terminal_music：MusicService / Qt Multimedia]",
        "    Music --> API[按需启动的 Node 音乐 API]",
        "    Music --> Cache[整曲磁盘缓存 → 本地定位播放]",
        "    QML --> System[terminal_system：系统工具 / 模糊 / 文件 / 音量]",
        "    System --> Linux[Linux：音频、USB、输入与设备]",
        "    Firmware[ESP32-S3 固件：桥接器 / Pi 模块] --> Shared[共享 LinkIt / 业务数据 / 传感器组件]",
        "    Protocol <--> Firmware",
        "    Tests[Qt / Python / Node 回归测试] -.验证.-> App",
        "    Tests -.验证.-> Firmware",
        "    Reference[third_party/YesPlayMusic 算法参考] -.仅参考，不构建部署.-> Music",
        "```",
        "",
        "## QML 界面与原生代码关系",
        "",
        "```mermaid",
        "flowchart LR",
        "    Main[qml/Main.qml] --> Pages[播放器 / 文件 / 编辑器 / 启动遮罩]",
        "    Main --> SharedQml[Theme / I18n / MaterialIcon / 通用控件]",
        "    Main --> Blur[BlurCache → CachedBlur → BlurItem]",
        "    Pages --> AppCpp[src/app：Application / Runtime / Options / Diagnostics]",
        "    Pages --> UiCpp[src/ui：TerminalUi / SystemTools / Storage / Volume]",
        "    Pages --> MusicCpp[MusicPlayer → MusicService → Qt Network / MediaPlayer]",
        "    AppCpp --> CoreCpp[src/core：连接 / 状态机 / 模块 / 包接收]",
        "    CoreCpp --> Wire[src/transport + src/protocol]",
        "    Wire --> Esp[esp：桥接器 / Pi 模块 / 共享组件]",
        "    MusicCpp --> Node[music_api：第三方 API adapter]",
        "```",
        "",
        "## 所有代码文件",
        "",
        f"共 **{len(files)} 个**代码/构建脚本文件（不含依赖目录 `node_modules`）。下列清单按仓库目录完整列出：",
        "",
    ]
    for root, paths in grouped.items():
        if not paths:
            continue
        lines.extend((f"### `{root}/`（{len(paths)} 个）", "", "```text", *paths, "```", ""))
    OUTPUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"Wrote {OUTPUT.relative_to(ROOT)}: {len(files)} source/code files")


if __name__ == "__main__":
    main()
