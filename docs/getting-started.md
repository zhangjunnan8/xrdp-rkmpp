# Getting started on RK3576

This is an English build/installation overview. The [complete installation recipe](new-board-install.md) includes dependency checks, guarded configuration generation, service units and rollback guidance. Hardware validation currently covers one KICKPI K7 with vendor Ubuntu 24.04 arm64/Linux 6.1.75. Read [known limitations](known-limitations.md) first.

## Prerequisites

Use a vendor BSP image that already drives RK3576 and exposes `/dev/mpp_service` and `/dev/dma_heap/system`. Install the matching MPP runtime/development files; a generic Ubuntu ARM image or an arbitrary MPP binary is insufficient. Match Xorg headers to the installed Xorg ABI. Preserve vendor graphics packages when reviewing package-manager changes.

Build dependencies include C/C++ build tools, Git, autoconf/automake/libtool/pkg-config, Python 3, OpenSSL/PAM/X11/Xfixes/Xrandr/JPEG/FUSE3/Pixman development packages, x264, OpenH264 >= 2.0, the Xorg SDK and FFmpeg. The fresh-board guide gives the tested Ubuntu package names and dry-run commands. Xfce/X11 is the tested desktop.

Ubuntu 22.04 is untested. Ubuntu 20.04 requires additional BSP/MPP work: the tested vendor MPP binary requires glibc >= 2.34, while stock 20.04 has 2.31. Do not upgrade glibc manually to force that package to install.

## Fetch reviewed source and upstream archives

Clone your chosen public release branch, inspect the checkout, record its SHA and ensure it is clean. If the snapshot was migrated to GitHub as `main`, set `RELEASE_BRANCH=main` below. Replace the placeholders; no public hosting URL is assumed:

```bash
REPOSITORY_URL=https://example.invalid/owner/xrdp-codecbridge.git
RELEASE_BRANCH=release/rk3576-open-source
git clone --single-branch --branch "$RELEASE_BRANCH" "$REPOSITORY_URL"
cd xrdp-codecbridge
SHA=$(git rev-parse HEAD)
test -z "$(git status --porcelain)"
DOWNLOADS="$HOME/downloads/xrdp-codecbridge"
mkdir -p "$DOWNLOADS"
curl -fL -o "$DOWNLOADS/xrdp-0.10.6.1.tar.gz" \
  https://github.com/neutrinolabs/xrdp/releases/download/v0.10.6.1/xrdp-0.10.6.1.tar.gz
curl -fL -o "$DOWNLOADS/xorgxrdp-0.10.5.tar.gz" \
  https://github.com/neutrinolabs/xorgxrdp/releases/download/v0.10.5/xorgxrdp-0.10.5.tar.gz
(cd "$DOWNLOADS" && sha256sum -c <<'SUMS'
2f7beb5a3b2529c8d72dc0df9b8cdca31ab0e0c14d1e3421210f5e6ec0ab3b75  xrdp-0.10.6.1.tar.gz
a5d03435f0ef48bf3d5010e63d9264f2334e7063cba3ecd8d4c0a15616a4f712  xorgxrdp-0.10.5.tar.gz
SUMS
)
```

These must be release attachments, not GitHub's auto-generated source archives. If the board cannot reach GitHub, transfer the same archives from another computer and verify their hashes on the board.

## Native staged build

```bash
XRDP_BUILD="$HOME/build/xrdp-rk3576-lab/$SHA"
XORG_BUILD="$HOME/build/xrdp-rk3576-xorg/$SHA"
sh rk3576/build.sh "$DOWNLOADS/xrdp-0.10.6.1.tar.gz" "$XRDP_BUILD" xrdp-rk3576-lab
PREFIX=$(cat "$XRDP_BUILD/install-prefix")
XRDP_CFLAGS="-I$XRDP_BUILD/stage$PREFIX/include" \
  sh rk3576/build-xorg.sh "$DOWNLOADS/xorgxrdp-0.10.5.tar.gz" "$XORG_BUILD"
```

The output directories must not exist. `XRDP_CFLAGS` points at this build's staged headers, so no old XRDP installation is required. Scripts stage versioned prefixes without changing services. Read build/test logs before installation.

## Install and validate

Follow sections 6–9 of the [fresh-board recipe](new-board-install.md). It checks hardware, port and path conflicts; creates independent configuration, keys and service units; grants narrowly scoped device-group access; and leaves initial service startup explicit. Do not substitute the legacy lab installers: they assume a pre-existing pilot installation.

Use a normal desktop account, connect to the board on TCP 3391 with Windows Remote Desktop or an H.264-capable FreeRDP client, and verify the new connection's hardware-frame log and encoder activity. The RDP example intentionally contains a non-routable placeholder and no username. Set the real endpoint locally.

The initial instance uses strict MPP validation. After successful actual RDP validation, allow logged x264 fallback for daily use (`XRDP_MPP_STRICT=0`). Restarting that instance disconnects its desktop, so save work first. Avoid modifying another XRDP installation or an active office session.

The installation recipe has been syntax/configuration-checked, not independently replayed end-to-end on a second blank board. English instructions do not expand that validation claim.
