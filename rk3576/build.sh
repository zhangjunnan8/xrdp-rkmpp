#!/bin/sh
# Build only; install into an isolated staging directory, never restart services.
set -eu
repo=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
archive=${1:?XRDP 0.10.6.1 release tar.gz required}
out=${2:?new absolute build directory required}
variant=${3:-xrdp-rk3576}
case "$variant" in xrdp-rkmpp|xrdp-rk3576|xrdp-rk3576-lab) ;; *) echo 'Unknown deployment namespace' >&2; exit 1;; esac
case "$out" in /*) ;; *) echo 'Build directory must be absolute' >&2; exit 1;; esac
test ! -e "$out"
test "$(sha256sum "$archive" | cut -d ' ' -f 1)" = 2f7beb5a3b2529c8d72dc0df9b8cdca31ab0e0c14d1e3421210f5e6ec0ab3b75
sha=$(git -C "$repo" rev-parse HEAD)
test -z "$(git -C "$repo" status --porcelain)"
prefix=/opt/$variant/$sha
mkdir -p "$out"
cp -R "$repo/rk3576" "$out/integration"
tar -xzf "$archive" -C "$out"
src=$out/xrdp-0.10.6.1
python3 -S "$out/integration/prepare.py" "$src"
cd "$src"
autoreconf -fi > "$out/autoreconf.log" 2>&1
./configure --prefix="$prefix" --sysconfdir=/etc/$variant \
    --localstatedir=/var --runstatedir=/run/$variant \
    --with-socketdir=/run/$variant/sockdir \
    --with-systemdsystemunitdir="$prefix/lib/systemd/system" \
    --enable-pam-config=debian --enable-strict-locations --enable-mpp --enable-x264 \
    --enable-openh264 --enable-jpeg --enable-pixman --enable-fuse \
    CFLAGS="${CFLAGS:--O2 -g}" LDFLAGS="${LDFLAGS:-}" > "$out/configure.log" 2>&1
make -j4 > "$out/build.log" 2>&1
make install DESTDIR="$out/stage" > "$out/install.log" 2>&1
for test in direct lifecycle; do
cc -O2 -g -Wall -Wextra -Werror -I. -Icommon -Ixrdp -Ilibxrdp -Ilibipm \
    "$out/integration/test_${test}.c" xrdp/xrdp_encoder_mpp.o \
    xrdp/xrdp_encoder_x264.o xrdp/xrdp_tconfig.o \
    common/.libs/libcommon.a third_party/tomlc99/.libs/libtoml.a \
    -lrockchip_mpp -lx264 -lssl -lcrypto -ldl -lpthread -lm \
    ${LDFLAGS:-} -o "$out/test-$test"
done
cc -O2 -Wall -Wextra -Werror "$out/integration/desktop-motion.c" \
    -lX11 -lm -o "$out/desktop-motion"
cc -O2 -Wall -Wextra -Werror "$out/integration/test_desktop.c" \
    -lX11 -o "$out/test-desktop"
cc -O2 -Wall -Wextra -Werror -fsanitize=undefined -fno-sanitize-recover=all \
    "$out/integration/test-pipeline.c" -o "$out/test-pipeline"
"$out/test-pipeline" > "$out/pipeline-test.log" 2>&1
printf '%s\n' "$sha" > "$out/source-commit"
printf '%s\n' "$prefix" > "$out/install-prefix"
echo "Built and staged direct MPP XRDP: $out"
