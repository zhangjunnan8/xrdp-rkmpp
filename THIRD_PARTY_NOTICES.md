# Third-party sources and notices

Project-authored additions use Apache-2.0, consistent with the pre-existing RK3576 source headers. This does not replace the licenses of upstream code or linked dependencies. This repository distributes integration source, not a combined binary package.

| Component | Source / role | License information |
|---|---|---|
| XRDP 0.10.6.1 | [Official release](https://github.com/neutrinolabs/xrdp/releases/tag/v0.10.6.1); patched at build time | [Apache-2.0 upstream COPYING](https://github.com/neutrinolabs/xrdp/blob/v0.10.6.1/COPYING); retain bundled-component notices |
| xorgxrdp 0.10.5 | [Official release](https://github.com/neutrinolabs/xorgxrdp/releases/tag/v0.10.5); capture conversion integration | X11 permission license, including file-specific attribution; exact notices in [LICENSES/xorgxrdp.txt](LICENSES/xorgxrdp.txt) |
| Rockchip MPP | [rockchip-linux/mpp](https://github.com/rockchip-linux/mpp); installed separately from the board BSP/SDK | Preserve the license files of the actual vendor SDK/package; no SDK implementation or binary is bundled here |
| x264 | [VideoLAN x264](https://www.videolan.org/developers/x264.html); software fallback and historical plugin | GPL-2.0-or-later in the open-source distribution; commercial licensing also exists. Apache-2.0 on this source does not override x264's terms |
| OpenH264 | [Cisco OpenH264](https://github.com/cisco/openh264); upstream build option/test client | BSD-style license; check the selected package's LICENSE and binary terms |
| FreeRDP / FFmpeg / Xvfb | External validation tools, not redistributed | Retain the licenses for the particular source/build; FFmpeg licensing depends on configuration |

`rk3576/xrdp_encoder_mpp.c` adapts the XRDP x264 encoder interface (Copyright Jay Sorg 2016–2024), with that attribution retained in its header. `rk3576/nv12-neon.c` reproduces the conversion semantics from xorgxrdp `module/rdpCapture.c` (Copyright 2014 Laxmikant Rashinkar; Copyright 2014–2017 Jay Sorg). The extraction script now retains the original notice in the generated scalar-reference source as well.

Both build scripts verify exact upstream archive SHA-256 values. Their upstream license files remain in generated build trees. Before distributing compiled combinations, review the actual linked components and their redistribution requirements separately; this release is source-only.

新增代码的 Apache-2.0 不会覆盖上游与依赖的许可。尤其 x264 开源版本采用 GPL；不要把整个链接后的二进制简单标成“纯 Apache-2.0”。本次只发布源码，不包含 SDK 或预编译发行包。
