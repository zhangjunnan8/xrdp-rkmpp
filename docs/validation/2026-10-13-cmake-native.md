# Native CMake validation / CMake 板端验收

Date: 2026-10-13. Tested public source: **`ba6496babafa1a826ba28cb958f97c49f3029788`**, clean Git checkout. This follows the [2026-10-11 workstation checks](2026-10-11-cmake-build.md).

**结论：根目录 CMake 构建流程已在 RK3576 上跑通，生成了 ARM64 XRDP 与 xorgxrdp 产物；独立硬编码、解码与故障回退检查通过。** 本轮使用独立检出和暂存目录，没有安装或启动新服务，没有改动正在运行的办公实例。

## Environment / 环境

| Item | Observed version |
|---|---|
| Board | KICKPI K7, RK3576, aarch64 |
| OS / kernel | Vendor Ubuntu 24.04.3 LTS / Linux 6.1.75 |
| CMake / Python / GCC | 3.28.3 / 3.12.3 / 13.3.0 |
| MPP, from pkg-config | `rockchip_mpp` 1.3.9 |
| x264 / OpenH264 | 0.164.3108 / 2.4.1 |
| Xorg SDK / Pixman | 21.1.7 / 0.42.2 |
| Build type | RelWithDebInfo; four upstream Make jobs |

The public checkout was transferred as a public-branch-only Git bundle because it contained commits not yet published to GitHub. Upstream XRDP, its three submodules and xorgxrdp were fetched from their official Git repositories at the [pinned revisions](../../cmake/UpstreamVersions.cmake). A direct xorgxrdp download timed out; retrying with a process-local proxy succeeded. No proxy address or machine credentials are part of this report or the source configuration.

## Commands and results / 命令与结果

The build ran as a normal user with existing matching development packages:

```bash
cmake -S . -B build
cmake --build build --parallel 4
build/bin/xrdp --version
build/bin/xrdp-sesman --version
source build/install-env.sh
```

| Check | Result |
|---|---|
| Configure, patch, compile, link and stage both components | Passed; native build exit status 0 |
| Upstream revisions and source provenance | Pinned HEADs match; pristine tracked sources unchanged; both build manifests identify the clean tested public commit |
| XRDP executables | ARM64 ELF; staged launchers report 0.10.6.1; MPP enabled; dependencies resolve using this build's private library directory and vendor/system libraries |
| Xorg modules | `libxorgxrdp.so`, `xrdpdev_drv.so`, `xrdpkeyb_drv.so`, `xrdpmouse_drv.so`: ARM64; shared-library dependencies resolve |
| Installation variables | `install-env.sh` sources successfully; SHA, XRDP stage and Xorg module paths match generated artifacts |
| Pipeline parser / ownership checks | 100,049 guarded cases passed with UBSan |
| BGRA → NV12 converter | 605 cases including all 16,777,216 RGB colors, strides, tails and write canaries; byte-identical to upstream; normal and UBSan runs passed |
| Independent hardware output validation | Nine cases / 660 frames, covering small and unaligned geometry, 1080p, 1440p and 4K; real MPP callbacks followed by FFmpeg decoding passed |
| Lifecycle and failure recovery | 80 cycles; 2,552 total checks; file-descriptor count unchanged; measured RSS unchanged at the lifecycle checkpoints; intentional failure recovered through real x264 with a full-frame IDR |
| Repeated CMake build | `cmake ..` and `cmake --build . --parallel 4` inside `build/` passed with an unusable network proxy; dependency fetch timestamps remained unchanged; no download or re-patching step ran; staged-artifact checks passed again |
| Launcher handling | Existing generated symlinks migrate; repeated generation works; quoted paths/arguments and existing library environment preserved; unrecognized output files refused |

The nine hardware cases used `XRDP_MPP_DIRECT=1` and source-buffer poisoning after input ownership transfer. `verify.py` required strict hardware mode, a completed hardware-frame log, no software fallback, the expected frame count/aligned dimensions, full-range BT.709, no B frames, and correct initial/idle-refresh IDRs. All decoded synthetic outputs exceeded the PSNR > 35 dB threshold; minimum observed was 54.922415 dB. This is a correctness check for synthetic inputs, not a desktop-quality or performance comparison.

The initial repeat-build audit also expected Xorg object timestamps to remain unchanged. That extra assertion failed because XRDP's upstream `make install` refreshes staged header timestamps, causing xorgxrdp to compile again. The CMake build itself succeeded. Repeated builds currently reuse dependency downloads and generated source trees but are not fully no-op builds; this is not a claim that all compiled objects are reused.

Hardware tests were explicitly run separately from the default build, as a normal user with access to the required video devices:

```bash
XRDP_MPP_DIRECT=1 XRDP_TEST_POISON_ON_READY=1 \
  python3 -S rk3576/verify.py build/xrdp/test-direct build/xrdp/verify-cmake-final
XRDP_MPP_DIRECT=1 build/xrdp/test-lifecycle
```

The verification output directory must not already exist. Use an idle board; these commands exercise its hardware encoder.

## Problems fixed / 本轮修正

1. **Uninstalled executables could not find their private XRDP libraries.** The original `build/bin/` symlinks led directly to staged binaries whose install paths did not yet exist. Generated launchers now set the staged library path for the child process. Deployment continues to use the original ELF files. Fix: `ff40a28`.
2. **Standalone fallback testing depended on installed configuration.** Hardware-only cases passed, but the lifecycle test's x264 recovery attempted to read the not-yet-installed public `gfx.toml`. A test-only linker wrapper redirects the real configuration parser to the staged file. Both test programs retain the real encoder and parser; symbol inspection confirmed that the wrapper is absent from the server binary. Fix: `ba6496b`.

Neither correction changes the MPP runtime implementation, NEON conversion or RDP flow control. The final clean commit was rebuilt and the above checks rerun.

## Scope / 范围

- Existing office services remained active with the same process IDs; no desktop was disconnected or restarted by this validation.
- No system package, driver, installed configuration, certificate or service unit was changed. The proxy was scoped to the build process.
- This run did not install the public service namespace, load these modules in a new Xorg/RDP session, test Windows client behavior, or measure end-to-end desktop performance. Earlier RDP/performance reports describe their own builds.
- A fresh second-board installation, Ubuntu 22.04/20.04, other SoCs and multi-user operation remain unqualified. Shared-library resolution alone does not prove Xorg ABI/session behavior.
- CMake 3.28.3 was exercised here; the declared CMake 3.16 minimum was not independently retested on that tool version.

See the [build guide](../building.md) and [new-board installation and RDP acceptance](../new-board-install.md).
