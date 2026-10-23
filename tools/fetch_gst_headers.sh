#!/usr/bin/env bash
# 拉取 GStreamer 开发文件（头文件 + 链接期 .so 符号链接）到 third_party/gst/<arch>/。
# 只下载 .deb 并解压，不需要 root、不安装系统包。
#   ./tools/fetch_gst_headers.sh          # 当前架构
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ARCH="$(dpkg --print-architecture)"
MULTIARCH="$(dpkg-architecture -qDEB_HOST_MULTIARCH 2>/dev/null || gcc -print-multiarch)"
DEST="$ROOT/third_party/gst/$ARCH"
PKGS=(libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev libglib2.0-dev)

if [[ -f "$DEST/include/glib-arch/glibconfig.h" && -L "$DEST/lib/libgstapp-1.0.so" &&
      "$(readlink "$DEST/lib/libgstapp-1.0.so")" == /usr/lib/* ]]; then
    echo "已就绪: $DEST"
    exit 0
fi

TMP="$(mktemp -d)"; trap 'rm -rf "$TMP"' EXIT
cd "$TMP"
echo "== 下载 GStreamer 开发文件（$ARCH）=="
apt-get download "${PKGS[@]}"
mkdir -p ext
for deb in ./*.deb; do dpkg-deb -x "$deb" ext; done

mkdir -p "$DEST/include" "$DEST/lib"

# 1) 公共头文件
cp -a ext/usr/include/. "$DEST/include/"

# 2) glibconfig.h：位于 <multiarch>/glib-2.0/include，平铺到 include/glib-arch
for d in ext/usr/lib/*/glib-2.0/include; do
    [[ -d "$d" ]] || continue
    mkdir -p "$DEST/include/glib-arch"
    cp -a "$d"/. "$DEST/include/glib-arch/"
done

# 3) 链接期 .so：指向系统运行时库，不要复制实体 .so.0。
# 复制 libgstreamer 会让 GStreamer 按自身位置重定位插件目录（third_party/.../lib），
# 运行时找不到 /usr/lib/<arch>/gstreamer-1.0 里的插件，所有 element 都会 "no element"。
rm -rf "$DEST/lib"
mkdir -p "$DEST/lib"
while IFS= read -r -d '' link; do
    name="$(basename "$link")"
    case "$name" in
        libgst*.so|libgobject-2.0.so|libglib-2.0.so) ;;
        *) continue ;;
    esac
    soname="$(readlink "$link")"
    [[ -n "$soname" ]] || continue
    target="/usr/lib/$MULTIARCH/$soname"
    [[ -e "$target" ]] || { echo "系统缺少 GStreamer 运行时库: $target" >&2; exit 1; }
    ln -s "$target" "$DEST/lib/$name"
done < <(find ext/usr/lib -maxdepth 2 -name 'lib*.so' -type l -print0)

[[ -f "$DEST/include/glib-arch/glibconfig.h" ]] || { echo "缺少 glibconfig.h" >&2; exit 1; }
[[ -f "$DEST/include/gstreamer-1.0/gst/app/gstappsink.h" ]] || { echo "缺少 gst/app/gstappsink.h" >&2; exit 1; }
# -e 会跟随符号链接：只解压 -dev 包时这些链接是悬空的，必须一起检查。
for lib in libgstreamer-1.0.so libgstapp-1.0.so libgobject-2.0.so libglib-2.0.so; do
    [[ -e "$DEST/lib/$lib" ]] || { echo "缺少可解析的链接库: $lib" >&2; exit 1; }
done
echo "完成: $DEST（$(du -sh "$DEST" | cut -f1)）"
