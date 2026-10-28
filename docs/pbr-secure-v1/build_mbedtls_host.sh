#!/usr/bin/env bash
# 把 ESP-IDF 自带的 mbedtls 编成主机库，用于跑 mbedtls_check.c。
#
# 背景：规范的实现基线是 ESP-IDF 5.5.4 / mbedtls 3.6.5，但主机的
# libmbedtls-dev 是 2.28。F 的 PR-NOTES 记了"用 IDF 源码主机编译 7/7 PASS"，
# 却没留方法，别人无法照做。本脚本就是那个方法。
#
# 用法：
#   ./build_mbedtls_host.sh [IDF_PATH] [OUT_DIR]
#   IDF_PATH 默认取 $IDF_PATH，其次 ~/esp/esp-idf
#   OUT_DIR  默认 /tmp/mbedtls-host-3.6.5
#
# 产物：$OUT_DIR/library/libmbedcrypto.a 与 $OUT_DIR/libmbedcrypto.a.sha256
# 之后：
#   cc -O2 -I"<idf>/components/mbedtls/mbedtls/include" mbedtls_check.c \
#      "$OUT_DIR/library/libmbedcrypto.a" -o check365
set -euo pipefail

IDF="${1:-${IDF_PATH:-$HOME/esp/esp-idf}}"
OUT="${2:-/tmp/mbedtls-host-3.6.5}"
SRC="$IDF/components/mbedtls/mbedtls"

fail() { printf 'error: %s\n' "$*" >&2; exit 1; }

[ -d "$SRC" ] || fail "找不到 mbedtls 源码：$SRC（先设置 IDF_PATH 或传入 IDF_PATH）"
[ -f "$SRC/include/mbedtls/build_info.h" ] || fail "不是 mbedtls 源码树：$SRC"

# 版本必须是规范写的 3.6.5；别的版本跑出来的 7/7 不能当作本规范的证据。
version=$(sed -n 's/.*MBEDTLS_VERSION_STRING *"\([^"]*\)".*/\1/p' "$SRC/include/mbedtls/build_info.h" | head -1)
[ "$version" = "3.6.5" ] || fail "mbedtls 版本为 '$version'，本规范要求 3.6.5（ESP-IDF 5.5.4）"

command -v cmake >/dev/null || fail "缺 cmake"

printf '>> mbedtls %s\n   src %s\n   out %s\n' "$version" "$SRC" "$OUT"

cmake -S "$SRC" -B "$OUT" \
      -DENABLE_TESTING=OFF \
      -DENABLE_PROGRAMS=OFF \
      -DCMAKE_BUILD_TYPE=Release >"$OUT.configure.log" 2>&1 \
  || { tail -30 "$OUT.configure.log" >&2; fail "cmake configure 失败"; }

cmake --build "$OUT" --target mbedcrypto -j"$(nproc)" >"$OUT.build.log" 2>&1 \
  || { tail -30 "$OUT.build.log" >&2; fail "构建 libmbedcrypto 失败"; }

LIB="$OUT/library/libmbedcrypto.a"
[ -f "$LIB" ] || fail "构建结束但找不到 $LIB"

sha256sum "$LIB" | tee "$LIB.sha256"
printf '>> ok：用下面的命令跑一致性检查\n   cc -O2 -I"%s/include" docs/pbr-secure-v1/mbedtls_check.c "%s" -o /tmp/check-%s\n' \
       "$SRC" "$LIB" "$version"
