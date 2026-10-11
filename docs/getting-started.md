# Installing xrdp-rkmpp on RK3576

This is the English build/installation overview for **[zhangjunnan8/xrdp-rkmpp](https://github.com/zhangjunnan8/xrdp-rkmpp), branch `main`**. The [complete installation recipe](new-board-install.md) includes dependency checks, guarded configuration generation and service units. Hardware validation currently covers one KICKPI K7 with vendor Ubuntu 24.04 arm64/Linux 6.1.75. Read [known limitations](known-limitations.md) first.

The public installation uses service names `xrdp-rkmpp` and `xrdp-rkmpp-sesman`, configuration under `/etc/xrdp-rkmpp/xrdp`, logs under `/var/log/xrdp-rkmpp`, and runtime sockets under `/run/xrdp-rkmpp`. TCP 3391 and Xorg display :50 are deliberate isolation defaults; both must be available. No private repository, original development account or existing pilot installation is required.

## Prerequisites

Use a vendor BSP image that already drives RK3576 and exposes `/dev/mpp_service` and `/dev/dma_heap/system`. Install the matching MPP runtime/development files; a generic Ubuntu ARM image or an arbitrary MPP binary is insufficient. Match Xorg headers to the installed Xorg ABI. Preserve vendor graphics packages when reviewing package-manager changes.

Build dependencies include C/C++ build tools, Git, autoconf/automake/libtool/pkg-config, Python 3, OpenSSL/PAM/X11/Xfixes/Xrandr/JPEG/FUSE3/Pixman development packages, x264, OpenH264 >= 2.0, the Xorg SDK and FFmpeg. The fresh-board guide gives the tested Ubuntu package names and dry-run commands. Xfce/X11 is the tested desktop.

Ubuntu 22.04 is untested. Ubuntu 20.04 requires additional BSP/MPP work: the tested vendor MPP binary requires glibc >= 2.34, while stock 20.04 has 2.31. Do not upgrade glibc manually to force that package to install.

## Fetch reviewed source and upstream archives

Run the commands in one Bash session as a normal user on the board. Clone public `main` into a new directory, record its actual SHA and detach at that commit for a reproducible build. The maintainer's `release/rk3576-open-source` branch is not the GitHub installation branch. No GitHub account or SSH key is needed:

```bash
set -euo pipefail
mkdir -p "$HOME/src" "$HOME/downloads/xrdp-rkmpp"
REPOSITORY_URL=https://github.com/zhangjunnan8/xrdp-rkmpp.git
RELEASE_BRANCH=main
git clone --single-branch --no-tags --branch "$RELEASE_BRANCH" \
  "$REPOSITORY_URL" "$HOME/src/xrdp-rkmpp"
cd "$HOME/src/xrdp-rkmpp"
REPO=$PWD
git status --short
test -z "$(git status --porcelain)"
SHA=$(git rev-parse HEAD)
git switch --detach "$SHA"
printf 'Build source: %s\n' "$SHA"
DOWNLOADS="$HOME/downloads/xrdp-rkmpp"
curl -fL --retry 3 -o "$DOWNLOADS/xrdp-0.10.6.1.tar.gz" \
  https://github.com/neutrinolabs/xrdp/releases/download/v0.10.6.1/xrdp-0.10.6.1.tar.gz
curl -fL --retry 3 -o "$DOWNLOADS/xorgxrdp-0.10.5.tar.gz" \
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
XRDP_BUILD="$HOME/build/xrdp-rkmpp/$SHA"
XORG_BUILD="$HOME/build/xrdp-rkmpp-xorg/$SHA"
sh "$REPO/rk3576/build.sh" \
  "$DOWNLOADS/xrdp-0.10.6.1.tar.gz" "$XRDP_BUILD" xrdp-rkmpp
PREFIX=$(cat "$XRDP_BUILD/install-prefix")
XRDP_CFLAGS="-I$XRDP_BUILD/stage$PREFIX/include" \
  sh "$REPO/rk3576/build-xorg.sh" \
  "$DOWNLOADS/xorgxrdp-0.10.5.tar.gz" "$XORG_BUILD" xrdp-rkmpp-xorg
XPREFIX=$(cat "$XORG_BUILD/install-prefix")
cat "$XRDP_BUILD/pipeline-test.log"
cat "$XORG_BUILD/conversion-test.log" "$XORG_BUILD/conversion-ubsan.log"
```

The output directories must not exist. `XRDP_CFLAGS` points at this build's staged headers, so no old XRDP installation is required. Pass the third argument to **both** scripts: it selects `/opt/xrdp-rkmpp/<SHA>` and `/opt/xrdp-rkmpp-xorg/<SHA>`, matching the installation recipe. Omitted arguments retain historical defaults for existing lab workflows. Scripts only stage files; they do not install into `/opt` or change services.

## Install and validate

Follow sections 6–9 of the [fresh-board recipe](new-board-install.md). It checks hardware, port and path conflicts; creates independent configuration, keys and service units; grants narrowly scoped device-group access; and leaves initial service startup explicit. Do not substitute the legacy lab installers: they assume a pre-existing pilot installation.

Use a normal desktop account, connect to the board on TCP 3391 with Windows Remote Desktop or an H.264-capable FreeRDP client, and verify the new connection's hardware-frame log and encoder activity. The RDP example intentionally contains a non-routable placeholder and no username. Set the real endpoint locally.

The initial instance uses strict MPP validation. In its RDP desktop, run `xrdp-rkmpp-motion-test 60 cycle`; inspect `/var/log/xrdp-rkmpp/xrdp.log` for the current connection's `first hardware frame complete` and no `switching to x264`. After successful actual RDP validation, allow logged x264 fallback for daily use (`XRDP_MPP_STRICT=0`). Restarting that instance disconnects its desktop, so save work first. Avoid modifying another XRDP installation or an active office session.

## Later source updates and deployment

Section 10 of the [complete recipe](new-board-install.md#10-获取后续公开版本) fetches the public repository's `main`, checks for local changes, and records a new SHA before building. Fetching source or building a stage does not upgrade the running desktop. The installer rejects an existing instance; there is no validated one-command updater. Preserve configuration, certificates and the old prefixes, schedule session downtime, deploy matching XRDP/Xorg builds, and repeat acceptance before recording the new installed SHA.

Legacy lab helpers retain their original service and directory assumptions and cannot be used directly to install, upgrade or profile this public namespace. Use the documented manual animation/log checks for first acceptance.

The installation recipe has been syntax/configuration-checked, not independently replayed end-to-end on a second blank board. The public namespace changes have local checks only; see [public installation checks](validation/2026-10-11-public-install.md). English instructions do not expand that validation claim.
