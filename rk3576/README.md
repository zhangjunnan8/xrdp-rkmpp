# RK3576 direct MPP integration / 直接接入

See [English overview](../README.en.md) / [中文概览](../README.md). This directory contains the active XRDP integration; it neither builds nor loads `libcodecbridge`.

## Build and first installation

Use the [fresh-board guide](../docs/new-board-install.md) or [English build overview](../docs/getting-started.md). The default root CMake entry point fetches pinned upstream Git revisions and builds both components:

```sh
cmake -S . -B build
cmake --build build --parallel 4
```

Unexpected upstream commits or source layouts stop the build. Builds stage versioned prefixes and do not change running services. Xorg modules are staged under the versioned module directory, not the system module directory. See [CMake details](../docs/building.md); native compilation and independent encoding validation are recorded in [the native checks](../docs/validation/2026-10-13-cmake-native.md).

Public installations clone `main` from [zhangjunnan8/xrdp-rkmpp](https://github.com/zhangjunnan8/xrdp-rkmpp). CMake selects the public `xrdp-rkmpp` / `xrdp-rkmpp-xorg` paths and supplies staged headers automatically. This is the only supported build entry point; historical archive scripts have been removed.

## Encoder behavior

- Select `h264_encoder = "MPP"` in `gfx.toml`; direct `-lrockchip_mpp` linkage. Tested vendor runtime SONAME is `.so.1`; verify actual `ldd` output on each image.
- NV12 / H.264 AVC420, BT.709 full range, Baseline, no B frames, one MPP frame in flight.
- Persistent full-image state receives damaged regions; resize rebuilds the encoder; every IDR carries SPS/PPS; one-second idle triggers an IDR refresh.
- Default fixed QP 18, nominal 60 fps, GOP 120; `XRDP_MPP_QP=10..40`. Nominal fps does not promise delivered 60 FPS.
- On MPP failure, default behavior logs and switches to x264 with a complete-image IDR recovery. `XRDP_MPP_STRICT=1` forbids this fallback for hardware validation.
- Input/output wait limits are 250 ms each; these cannot guarantee recovery from a hung kernel driver.
- `XRDP_MPP_DIRECT=1`: update persistent cacheable DMA input directly; retain synchronization and reconstruct full-image software recovery safely.
- `XRDP_MPP_PIPELINE=1`: return capture input after transfer, only for eligible single STARTFRAME/AVC420/ENDFRAME batches. Preserve real client ACKs and upstream frame-count limits.
- DIRECT/PIPELINE default off in source; the optimized instance explicitly enables them. Complex batches retain upstream scheduling.
- The AArch64 NEON converter matches upstream integer coefficients, rounding, 2×2 chroma averaging, padding and tail pixels exactly.

## Tools and scope

| Tool | Purpose |
|---|---|
| `test-direct`, `verify.py` | Real encoding callbacks and independent FFmpeg decode/quality validation |
| `test-lifecycle` | Repeated creation, resize, destruction, invalid input and software recovery |
| `test-nv12`, `test-pipeline` | Bit-exact conversion and bounded command parsing/ownership checks |
| `desktop-motion` | Real X11 window movement and changing synthetic pixels |
| `test_desktop.c` | Optional independent X11 client-window resize/close helper |

Default deployment is one headless Xorg/Xfce instance on TCP 3389. The old multi-instance installers, profiling wrappers and generic plugin prototype have been removed. See [the workload instructions](../docs/unattended-benchmark.md) for retained animation and hardware checks. Never run a workload on an occupied office session.

Performance and validation: [matched optimization data](../docs/performance.md), [initial integration](../docs/validation/2026-10-10-rk3576-direct.md), [optimization acceptance](../docs/validation/2026-10-10-rk3576-optimization.md). Generated captures and raw logs stay outside Git.

Project additions: [Apache-2.0](../LICENSE), with [upstream notices](../THIRD_PARTY_NOTICES.md) retained.
