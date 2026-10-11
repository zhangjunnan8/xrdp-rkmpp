#!/bin/sh
# Build isolated Xorg modules and test the new converter against upstream.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
archive=${1:?xorgxrdp 0.10.5 archive required}
out=${2:?new absolute output directory required}
case "$out" in /*) ;; *) exit 2;; esac
test ! -e "$out"
test -z "$(git -C "$repo" status --porcelain)"
test "$(sha256sum "$archive" | cut -d ' ' -f 1)" = a5d03435f0ef48bf3d5010e63d9264f2334e7063cba3ecd8d4c0a15616a4f712
sha=$(git -C "$repo" rev-parse HEAD)
prefix=/opt/xrdp-rk3576-xorg/$sha
mkdir -p "$out"
cp -R "$repo/rk3576" "$out/integration"
tar -xzf "$archive" -C "$out"
src=$out/xorgxrdp-0.10.5
python3 -S "$out/integration/prepare-xorg.py" "$src"
cc -O2 -g -Wall -Wextra -Werror "$out/integration/test-nv12.c" \
    "$out/integration/nv12-neon.c" "$src/upstream-nv12-reference.c" -o "$out/test-nv12"
"$out/test-nv12" > "$out/conversion-test.log" 2>&1
cc -O2 -g -fsanitize=undefined -fno-sanitize-recover=all \
    "$out/integration/test-nv12.c" "$out/integration/nv12-neon.c" \
    "$src/upstream-nv12-reference.c" -o "$out/test-nv12-ubsan"
"$out/test-nv12-ubsan" > "$out/conversion-ubsan.log" 2>&1
cd "$src"
autoreconf -fi > "$out/autoreconf.log" 2>&1
./configure --prefix="$prefix" \
    --without-simd CFLAGS='-O2 -g' > "$out/configure.log" 2>&1
make -j4 > "$out/build.log" 2>&1
# Upstream obtains moduledir from the system Xorg pkg-config, ignoring prefix.
# Override it for every recursive install target so system modules stay intact.
make install DESTDIR="$out/stage" moduledir="$prefix/lib/xorg/modules" > "$out/install.log" 2>&1
printf '%s\n' "$sha" > "$out/source-commit"
printf '%s\n' "$prefix" > "$out/install-prefix"
echo "Built and tested isolated NEON Xorg: $out"
