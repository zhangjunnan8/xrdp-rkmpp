# Known limitations / 已知限制

## Qualification / 验证范围

| Area / 项目 | Status / 状态 |
|---|---|
| KICKPI K7 / RK3576, vendor Ubuntu 24.04 arm64, Linux 6.1.75 | Hardware-tested on one board / 单板硬件实测 |
| Ubuntu 22.04 | Requires matching BSP, MPP and Xorg SDK; untested / 有条件移植，未实测 |
| Ubuntu 20.04 | Additional compatibility work; untested / 需额外适配，未实测 |
| RK3588, RK356x and other Rockchip SoCs | Not qualified; MPP availability alone is insufficient / 未验收，不能仅凭 MPP 推定兼容 |
| Raspberry Pi, Allwinner, NVIDIA | No backend in this direct implementation / 当前直接适配没有这些后端 |
| Single-screen X11 / Xfce | Tested / 已测试 |
| Multi-monitor, concurrent users, Wayland, other desktops | Not qualified / 未验收 |
| Standard RDP clients | FreeRDP automated validation; Windows Remote Desktop user feedback / 自动数据来自 FreeRDP，Windows 有人工体验反馈 |

## Encoding and performance / 编码与性能

- H.264 AVC420 / 4:2:0, NV12 input, BT.709 full range, Baseline, no B frames. No AVC444; colored fine text may show chroma-subsampling artifacts. 当前并非无损桌面，彩色小字可能受色度抽样影响。
- Fixed QP 18 is the measured LAN setting, with a configurable range of 10–40. No adaptive WAN bitrate/congestion policy. 大面积变化可能增加码率；不能保证低带宽体验。
- No RGA conversion, DMA-BUF export/import across the entire capture pipeline, end-to-end zero-copy or GPU-accelerated desktop rendering. CPU activity remains expected. 减少了一次输入复制不等于零拷贝。
- One hardware frame at a time; capture overlaps only eligible single STARTFRAME/AVC420/ENDFRAME batches. Complex batches retain upstream scheduling. The real client ACK limit is unchanged.
- Encoded FPS, presentation FPS and input latency are different quantities. No 4K60, universal 60 FPS, energy-efficiency or superiority-over-all-other-software claim is established.

## Reliability and deployment / 稳定性与部署

- MPP uses 250 ms input/output waits. User-space timeouts cannot guarantee recovery from a hung kernel driver. 内核驱动挂死不能靠用户态超时完全隔离。
- Software recovery is tested for covered failure cases and must rebuild a complete frame/IDR. Strict mode forbids fallback; it does not force clients to negotiate H.264.
- Validation includes independent decode, lifecycle checks, source-reuse poisoning, UBSan, protected memory boundaries and a five-minute resize run. It is not multi-day, multi-user production qualification.
- ASan could not start on the tested board's 39-bit virtual-address kernel. Do not label the RK3576 runtime ASan-tested; historical generic-prototype tests on another environment are separate.
- Native builds depend on the exact BSP/MPP interfaces and matching Xorg ABI. The vendor MPP package tested requires glibc >= 2.34 and cannot simply be copied to stock Ubuntu 20.04 (glibc 2.31).
- Legacy `install-lab-release.py` uses Python 3.9+ `Path.is_relative_to()` and assumes a particular lab layout. Use the fresh-board guide for first installation. 更换主机参数并不使旧升级脚本成为通用安装器。
- The fresh-board recipe has static/configuration validation, not an independent clean-board end-to-end installation. The public `xrdp-rkmpp` namespace has local checks only; there is no validated one-command updater. No Ubuntu 22.04/20.04 hardware run is claimed. See [public installation checks](validation/2026-10-11-public-install.md).
- Legacy benchmark/deployment helpers still assume historical lab services and paths. They do not directly support the public `xrdp-rkmpp` installation; use the guide's manual animation/log checks for first acceptance. 仅替换地址或沿用同一个端口不足以适配旧脚本。
- Updating/restarting an XRDP instance interrupts its own desktop sessions, even if its name contains `lab`. Use a separate instance or a maintenance window.
- Audio redirection modules, clipboard/drive-redirection hardening, deployment automation and system-driver upgrades are outside this release's validation scope.

See [Performance](performance.md) for exact data and [fresh-board installation](new-board-install.md) for dependency and permission checks.
