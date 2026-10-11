# XRDP RKMPP — Rockchip Hardware Encoding

[简体中文](README.md) | [English](README.en.md)

Hardware H.264 encoding for **XRDP on Rockchip RK3576**, with a direct Rockchip MPP backend, NEON desktop conversion, and bounded capture/encode overlap. Connect with an unmodified RDP client, including Windows Remote Desktop.

**Status: hardware-validated on one KICKPI K7, Ubuntu 24.04 arm64, vendor Linux 6.1.75.** This is a board-specific implementation, not an upstream XRDP release or a universal Rockchip driver. Other boards and Ubuntu 22.04/20.04 have not been hardware-validated.

## Development acknowledgment

Most of this project's RK3576 integration and optimization work was completed by **GPT-6 Astra** under the maintainer's guidance, including implementation, performance optimization, test tooling, and documentation. This acknowledgment covers this project's additions; attribution and licenses for upstream projects such as XRDP, xorgxrdp, and Rockchip MPP remain unchanged.

## Encoding path

```text
X11 desktop → xorgxrdp capture → BGRA-to-NV12 (AArch64 NEON)
            → XRDP MPP backend → Rockchip MPP → RKVENC → H.264 AVC420 → RDP client
```

RKVENC is the dedicated video encoder. Mali GPU utilization does not indicate its activity. Desktop rendering and some memory transfers still use the CPU.

- Direct XRDP H.264 callbacks; no CodecBridge plugin loader in this path.
- Persistent DMA input with damaged-region updates and explicit cache synchronization.
- Capture overlaps encoding after source-buffer ownership transfers, retaining real client ACKs and the upstream in-flight frame limit.
- Dynamic resolution, idle refresh, SPS/PPS at IDR frames, and explicit x264 recovery after hardware failure.
- Strict hardware validation mode; no silent fallback on MPP failure.
- Pinned Git revisions and submodules for **XRDP 0.10.6.1 / xorgxrdp 0.10.5**, native builds and separate versioned installation prefixes.

## Measured performance

Same board, 2560×1440, QP 18, 16 ms capture interval, separate LAN FreeRDP client. **Both columns already use MPP hardware encoding.** The comparison measures NEON conversion, reduced copying and pipeline overlap together.

| Synthetic desktop workload | Initial MPP path | Optimized MPP path | Gain |
|---|---:|---:|---:|
| Window movement | 19.834 FPS | 35.889 FPS | 81% |
| Rapid color changes | 25.111 FPS | 49.445 FPS | 97% |
| Movement + color changes | 20.278 FPS | 36.500 FPS | 80% |

These are **server encoded-frame rates**, not monitor presentation FPS or input-to-display latency. One matched run per configuration; no confidence intervals. The separate 1080p x264/MPP callback comparison, quality differences, methodology and machine-readable data are in [Performance](docs/performance.md).

## Build and install

Start with the [English getting-started guide](docs/getting-started.md) or the [complete fresh-board installation guide / 新板安装教程](docs/new-board-install.md) (Chinese). They cover native builds, a separate service on TCP 3391, device permissions and actual RDP validation.

Public source is on **`main`** at [zhangjunnan8/xrdp-rkmpp](https://github.com/zhangjunnan8/xrdp-rkmpp). The guides consistently use the `xrdp-rkmpp` service/directory namespace. No original development branch, old XRDP installation or lab configuration is required.

Prerequisites: a working RK3576 BSP/MPP stack, matching Xorg development headers, x264/OpenH264 development packages and an X11 desktop. An ordinary ARM Ubuntu image alone is insufficient.

After preparing the documented development dependencies on the board:

```bash
git clone --branch main --single-branch https://github.com/zhangjunnan8/xrdp-rkmpp.git
cd xrdp-rkmpp
cmake -S . -B build
cmake --build build --parallel 4
```

Root CMake shallow-fetches pinned Git commits for XRDP, its submodules and xorgxrdp, applies patches and builds both. Running `cmake ..` inside `build/` is equivalent. Executable shortcuts are `build/bin/xrdp` and `build/bin/xrdp-sesman`; complete staged trees are in `build/xrdp/stage/` and `build/xorgxrdp/stage/`. No system files or services are changed. Continue with the installation recipe; see the [CMake guide](docs/building.md). The new entry point has local source/flow checks; native RK3576 compilation is pending while the board is offline.

After installation, select `h264_encoder = "MPP"` in the instance's `gfx.toml` and set these XRDP service environment variables:

| Setting | Purpose |
|---|---|
| `XRDP_MPP_QP=18` | Tested fixed-QP LAN setting; supported range 10–40 |
| `XRDP_MPP_DIRECT=1` | Update persistent DMA input directly; source default is off |
| `XRDP_MPP_PIPELINE=1` | Bounded capture/encode overlap; source default is off |
| `XRDP_MPP_STRICT=1` | Hardware validation; use `0` for explicit software fallback in daily use |

Verify **`first hardware frame complete` for the current connection** and no runtime `switching to x264`. A config setting alone proves nothing; clients must negotiate H.264. See [implementation details](rk3576/README.md).

## Scope and limitations

- H.264 AVC420 / 4:2:0, BT.709 full range, Baseline, no B frames; no AVC444.
- Single-display desktop validation; multi-monitor and concurrent users are unqualified.
- No RGA conversion, end-to-end zero-copy or GPU desktop rendering acceleration.
- Fixed QP; no WAN adaptive bitrate. 4K60 and universal 60 FPS are not demonstrated.
- A five-minute resize run is not a multi-day reliability test. ASan could not start on the tested board's 39-bit VA kernel; UBSan and explicit memory checks were used.
- Lab helpers require explicit site configuration and isolated sessions; they are not a general-purpose deployment manager.

See [Known limitations / 已知限制](docs/known-limitations.md) for the complete qualification boundary.

## Repository map

| Path | Purpose |
|---|---|
| `rk3576/` | Active MPP encoder, upstream integration, NEON conversion, builds and tests |
| `docs/performance.md` | Benchmarks, methodology and interpretation |
| `docs/validation/` | Dated evidence and historical build identifiers |
| `docs/unattended-benchmark.md` | Isolated workload and LAN test setup |
| `CMakeLists.txt`, `cmake/` | Default XRDP integration build and pinned upstream dependencies |
| `src/`, `include/`, `backends/` | Historical generic C ABI prototype; built only with explicit legacy opt-in |

The public snapshot omits private development history. Build identifiers in dated reports describe the original tested artifacts; they need not resolve in the public snapshot. See [release provenance and privacy review](docs/public-release.md).

## Contributing and license

See [CONTRIBUTING.md](CONTRIBUTING.md) for environment reporting and validation. Submit sanitized logs only; never attach credentials, private desktop captures or site configuration.

Project-authored additions use **Apache-2.0**; see [LICENSE](LICENSE). Retain upstream attribution and dependency licenses. [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) lists sources and the x264 dependency's separate licensing. No BSP, SDK binaries, private keys or prebuilt release binaries are bundled.
