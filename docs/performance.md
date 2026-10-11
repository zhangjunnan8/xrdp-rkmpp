# Performance / 性能对比

Measured on 2026-10-10. These are development measurements on **one KICKPI K7 / RK3576**, not a cross-device benchmark or an independent replication. 本页保留实际数据及测试条件，不把编码吞吐量当作端到端体验，也不将测试板结论推广到所有 Rockchip 平台。

## Environment / 环境

| Component | Recorded environment |
|---|---|
| Server | KICKPI K7, RK3576, Ubuntu 24.04 arm64, vendor Linux 6.1.75 |
| XRDP / xorgxrdp | 0.10.6.1 / 0.10.5, pinned official archives |
| Graphics stack | Vendor Xorg 21.1.7, Xfce/X11; no RGA or Glamor optimization |
| MPP | Vendor package 1.5.0-1; actual runtime links `librockchip_mpp.so.1` |
| Compiler / decode check | GCC 13.3 / FFmpeg 6.1.1 |
| Desktop benchmark client | Separate LAN host, private FreeRDP 2.11.5 with FFmpeg H.264 decoding, Xvfb |
| Hardware path | H.264 AVC420, BT.709 full range, fixed QP 18, no B frames |

No frequency locking or CPU affinity was imposed. No power consumption or physical input-to-display latency was measured. Raw captures and session logs are private; the synthetic generators, analysis tools and aggregate results are supplied for rerunning, not presented as an independently audited dataset.

没有锁频、绑定 CPU 或测量功耗。原始会话日志/抓图不随源码发布；仓库提供合成负载、分析工具和汇总数据，供他人复测。

## 1. Desktop pipeline optimization / 桌面链路优化

**Both configurations use MPP hardware encoding.** Same XRDP binary, QP 18, 2560×1440, 16 ms frame interval and client. Baseline: scalar conversion, `DIRECT=0`, `PIPELINE=0`. Optimized: NEON conversion, `DIRECT=1`, `PIPELINE=1`. Each run lasts 60 seconds: 20 seconds movement, 20 seconds rapid color changes, 20 seconds both. Exclude the first 2 seconds of each phase.

**这不是软件编码与硬件编码的对比。** 两组硬编码证据均通过；比较的是 NEON、输入复制和流水线的组合效果，不能把收益全部归给某一项。

| Workload / 负载 | Baseline FPS | Optimized FPS | Gain | Capture mean ms, before → after | Encode mean ms, before → after |
|---|---:|---:|---:|---:|---:|
| Movement / 移动 | 19.834 | 35.889 | 81% | 18.908 → 3.955 | 24.375 → 19.224 |
| Color / 变色 | 25.111 | 49.445 | 97% | 11.750 → 2.483 | 21.504 → 17.714 |
| Mixed / 混合 | 20.278 | 36.500 | 80% | 17.042 → 3.695 | 24.599 → 19.519 |

FPS means completed **server encoding** work. Both runs observed at most 2 unacknowledged frames, with no runtime encoder error, fallback or trace loss. Capture and encode now overlap, so their mean times cannot simply be added to predict FPS. One run per configuration, no repeated-trial variance or confidence intervals.

FPS 为**服务端实际编码速率**。两组最多均为 2 帧未确认；没有通过扩大队列或降低 QP 画质换取帧率。单次测量没有置信区间，不能证明 Windows 最终显示帧率或输入延迟。

The converter-only 1440p full-frame microbenchmark measured **24.099 ms scalar → 5.141 ms NEON (4.69×)**. This is distinct from partial-damage desktop capture. Bit-exact comparison covered 605 cases including all 16,777,216 RGB values, row padding and non-aligned dimensions, in native and UBSan runs.

## 2. x264 versus MPP callbacks / 软件与硬件回调对照

Earlier controlled 1080p synthetic callback runs, sequential on the same board, 120 frames including initialization. CPU time includes encoder-process worker threads; it excludes test-image generation and file I/O.

| Scene / 场景 | Encoder | Mean ms | P95 ms | CPU ms / 120 frames | Compressed bytes |
|---|---|---:|---:|---:|---:|
| Partial damage / 局部 | XRDP x264 | 26.006 | 27.560 | 5239.654 | 100989 |
| Partial damage / 局部 | MPP QP18 | 8.156 | 8.343 | 158.860 | 105362 |
| Full change / 整帧 | XRDP x264 | 38.275 | 39.568 | 7660.593 | 1524544 |
| Full change / 整帧 | MPP QP18 | 11.693 | 11.904 | 591.584 | 3129830 |

**Not equal-quality or equal-bitrate configurations.** x264 used ultrafast/zerolatency, two threads and its configured CRF; MPP used fixed QP 18. x264 PSNR: 53.709 / 53.099 dB for partial/full change; MPP: 56.683 / 57.392 dB. MPP full-change output was about twice as large. The complete original x264 configuration is not reconstructed here, so these are illustrative historical configuration comparisons, not an encoder rate-distortion ranking.

**两种配置不等画质、不等码率。** MPP 的整帧输出约为两倍；不能将表中 CPU 降幅解读为整机 CPU 降幅，也不能用编码耗时倒数当作桌面 FPS。原始 x264 完整配置未在此重建，因此不作严格编码效率排名。

## 3. Resize run / 动态分辨率连续运行

One connection, 60 seconds per size, each containing the same three phases. No runtime fallback/error/trace loss. File descriptors returned to 45 after each stage; RSS retained about 65 MiB after 4K expansion and stabilized at repeated sizes. Five minutes is not proof of indefinite leak-free operation.

| Order / 次序 | Movement FPS | Color FPS | Mixed FPS |
|---|---:|---:|---:|
| 1440p first | 36.778 | 48.889 | 36.222 |
| 1080p first | 36.166 | 30.223 | 40.944 |
| 4K | 22.111 | 26.722 | 21.056 |
| 1080p return | 47.278 | 33.000 | 43.000 |
| 1440p return | 34.889 | 44.722 | 36.444 |

**4K60 is not demonstrated.** The two 1080p stages differ substantially; composition, client behavior and scheduling remain relevant. 4K 未达到 60 FPS，1080p 的波动也必须保留，不能只选最好的一次作为保证。

## Reproduce and inspect / 复现与证据

- [Isolated LAN workload setup](unattended-benchmark.md): configured test account, separate decoding host, real RDP, frame-level profiler and analysis. Run only on an isolated session.
- [Initial direct-integration record](validation/2026-10-10-rk3576-direct.md): original callback comparison, decode validation and failure recovery.
- [Optimization acceptance](validation/2026-10-10-rk3576-optimization.md): matched desktop comparison and resize run.
- [Desktop CSV](benchmarks/desktop-1440p.csv), [callback CSV](benchmarks/callback-1080p.csv), [resize CSV](benchmarks/resize-soak.csv): the same recorded aggregates, not newly measured data.

For new comparisons, record the source SHA, environment, encoder settings, client/decoder, dimensions, duration and warm-up exclusion. Repeat runs in alternating order and report variation. Public-release metadata changes do not constitute a new hardware benchmark; see [provenance](public-release.md).
