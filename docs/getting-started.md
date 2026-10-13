# Installing xrdp-rkmpp on RK3576

This is the English build/installation overview for **[zhangjunnan8/xrdp-rkmpp](https://github.com/zhangjunnan8/xrdp-rkmpp), branch `main`**. The [complete installation recipe](new-board-install.md) includes dependency checks, guarded configuration generation and service units. Hardware validation currently covers one KICKPI K7 with vendor Ubuntu 24.04 arm64/Linux 6.1.75. Read [known limitations](known-limitations.md) first.

The public installation uses service names `xrdp-rkmpp` and `xrdp-rkmpp-sesman`, configuration under `/etc/xrdp-rkmpp/xrdp`, logs under `/var/log/xrdp-rkmpp`, and runtime sockets under `/run/xrdp-rkmpp`. TCP 3389 is the standard RDP endpoint and :50 is the virtual Xorg display offset; both must be available. The two units form one instance: listener/encoder and session manager. Headless operation needs no connected display or dummy plug, but does require Xorg/xorgxrdp/Xfce software. No private repository, original development account or existing pilot installation is required.

## Prerequisites

Use a vendor BSP image that already drives RK3576 and exposes `/dev/mpp_service` and `/dev/dma_heap/system`. Install the matching MPP runtime/development files; a generic Ubuntu ARM image or an arbitrary MPP binary is insufficient. Match Xorg headers to the installed Xorg ABI. Preserve vendor graphics packages when reviewing package-manager changes.

Build dependencies include C/C++ build tools, Git, autoconf/automake/libtool/pkg-config, Python 3, OpenSSL/PAM/X11/Xfixes/Xrandr/JPEG/FUSE3/Pixman development packages, x264, OpenH264 >= 2.0, the Xorg SDK and FFmpeg. The fresh-board guide gives the tested Ubuntu package names and dry-run commands. Xfce/X11 is the tested desktop.

Ubuntu 22.04 is untested. Ubuntu 20.04 requires additional BSP/MPP work: the tested vendor MPP binary requires glibc >= 2.34, while stock 20.04 has 2.31. Do not upgrade glibc manually to force that package to install.

## Clone and build

After the dependency checks above (including CMake 3.16+ and Python 3.8+), run on the RK3576 board as a normal user:

```bash
set -euo pipefail
mkdir -p "$HOME/src"
git clone --single-branch --no-tags --branch main \
  https://github.com/zhangjunnan8/xrdp-rkmpp.git "$HOME/src/xrdp-rkmpp"
cd "$HOME/src/xrdp-rkmpp"
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build --parallel 4
source build/install-env.sh
```

Alternatively, run `cmake ..` and `cmake --build .` inside a new `build` directory. Root CMake now builds the active XRDP integration. It shallow-fetches exact Git commits for XRDP 0.10.6.1, its pinned submodules and xorgxrdp 0.10.5, checks and patches them, builds both components, and passes the staged XRDP headers to xorgxrdp automatically. No manual tarball download or `XRDP_CFLAGS` setup is required.

Binaries are exposed as `build/bin/xrdp` and `build/bin/xrdp-sesman`; complete staged install trees are in `build/xrdp/stage/` and `build/xorgxrdp/stage/`. The generated `install-env.sh` sets all variables needed by the installation recipe. Builds run CPU conversion/parser checks, but do not run MPP encoding, install system files or start services. They use the board's existing BSP/MPP rather than rebuilding vendor drivers.

See [the CMake guide](building.md) for pinned revisions, output paths, build logs, mirrors and the workstation `XRDP_RKMPP_FETCH_ONLY` mode. Full compilation requires native Linux ARM64 with matching dependencies. Native compilation, staged artifacts and standalone MPP encoding with independent decoding have passed on the RK3576 board; see the [native validation record](validation/2026-10-13-cmake-native.md).

## Install and validate

Follow sections 6–9 of the [fresh-board recipe](new-board-install.md). It checks hardware, port and path conflicts; creates independent configuration, keys and service units; grants narrowly scoped device-group access; and leaves initial service startup explicit. The old lab installers have been removed. If 3389 is occupied, inspect and back up the old service, then retire it during a maintenance window before installation.

Use a normal desktop account, connect to the board on TCP 3389 with Windows Remote Desktop or an H.264-capable FreeRDP client, and verify the new connection's hardware-frame log and encoder activity. The RDP example intentionally contains a non-routable placeholder and no username. Set the real endpoint locally.

The [RDP profile](../rk3576/RK3576-optimized.rdp) defaults to 1920×1080 in a window, with dynamic resolution off and smart sizing on. Resizing the local window scales its content without increasing the remote encoding workload. Change the profile's width/height or the client's display settings and reconnect for another size; optionally enable dynamic resolution yourself. This is a client default, not a server-enforced size limit. Connecting by IP alone uses your client's existing settings. See [Microsoft's property definitions](https://learn.microsoft.com/en-us/azure/virtual-desktop/rdp-properties#display-settings).

The initial instance uses strict MPP validation. In its RDP desktop, run `xrdp-rkmpp-motion-test 60 cycle`; inspect `/var/log/xrdp-rkmpp/xrdp.log` for the current connection's `first hardware frame complete` and no `switching to x264`. After successful actual RDP validation, allow logged x264 fallback for daily use (`XRDP_MPP_STRICT=0`). Restarting that instance disconnects its desktop, so save work first. Avoid modifying another XRDP installation or an active office session.

## Later source updates and deployment

Section 10 of the [complete recipe](new-board-install.md#10-获取后续公开版本) fetches the public repository's `main`, checks for local changes, and records a new SHA before building. Fetching source or building a stage does not upgrade the running desktop. The installer rejects an existing instance; there is no validated one-command updater. Preserve current configuration and certificates, schedule session downtime, deploy matching XRDP/Xorg builds, and repeat acceptance before recording the new installed SHA. A temporary rollback archive can be retained during the switch. After acceptance, remove obsolete prefixes and archives according to the owner's retention policy, after checking process references; keep only one enabled instance.

Historical multi-instance helpers and the generic plugin prototype are no longer included. Use the documented manual animation/log checks for first acceptance.

The installation recipe has not been independently replayed end-to-end on a second blank board. The [native CMake checks](validation/2026-10-13-cmake-native.md) cover builds and standalone encoding; subsequent [single-instance acceptance](validation/2026-10-13-single-instance.md) covers actual public-namespace deployment, headless RDP, hardware encoding, resizing and reconnection on the original board. See also [public installation checks](validation/2026-10-11-public-install.md). English instructions do not expand that validation claim.
