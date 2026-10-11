# RK3576 direct MPP integration / 直接接入

See [English overview](../README.md) / [中文概览](../README.zh-CN.md). This directory contains the active XRDP integration; it neither builds nor loads `libcodecbridge`.

## Build and first installation

Use the [fresh-board guide](../docs/new-board-install.md) or [English build overview](../docs/getting-started.md). Native entry points:

```sh
sh rk3576/build.sh /path/to/xrdp-0.10.6.1.tar.gz /absolute/new-xrdp-build xrdp-rk3576-lab
# Set XRDP_CFLAGS to the matching staged include directory as shown in the guide.
sh rk3576/build-xorg.sh /path/to/xorgxrdp-0.10.5.tar.gz /absolute/new-xorg-build
```

Pinned upstream archives are SHA-256 checked; unexpected source layouts stop the patch scripts. Builds stage versioned prefixes and do not change running services. Xorg modules are staged under the versioned module directory, not the system module directory.

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
| `run-lan-benchmark.py` | Explicitly configured separate LAN client and isolated 3391 / :50 workload |
| `profile-session.py`, `analyze-session.py` | Frame-level tracing and aggregate analysis |
| `install-test.py`, `install-lab-release.py`, `lab-run.py`, `rdp-soak.py` | Legacy lab helpers with additional layout requirements; not fresh-board installers |

`lab_config.py` requires the expected hostname and non-root desktop account explicitly. Known-host SSH verification remains enabled. Read [the lab setup](../docs/unattended-benchmark.md) before using helpers; specifying a different host does not remove their fixed-port/display/layout assumptions. Do not bypass guards with `python -O`.

旧安装脚本依赖既有 pilot 配置、模块路径、PAM 和测试显示号；新板使用安装教程。3391 若已用于办公，同样不能随意重启或压测。

Performance and validation: [matched optimization data](../docs/performance.md), [initial integration](../docs/validation/2026-10-10-rk3576-direct.md), [optimization acceptance](../docs/validation/2026-10-10-rk3576-optimization.md). Generated captures and raw logs stay outside Git.

Project additions: [Apache-2.0](../LICENSE), with [upstream notices](../THIRD_PARTY_NOTICES.md) retained.
