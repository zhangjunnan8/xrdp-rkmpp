# Single headless instance on TCP 3389 / 单套无头服务验收

Date: 2026-10-13. Compiled source: public commit `754eddb5ec46d5848b880e7e7295deb5318e0ae9`, clean checkout. Later documentation records the RDP-only XDG autostart setting applied during acceptance; encoder/converter/pipeline source was unchanged.

## Scope and changes

- Removed the inactive generic CodecBridge ABI, loader, standalone x264 plugin and their tests. Removed old archive build scripts and fixed-layout multi-instance deployment/benchmark wrappers. Root CMake now has one implementation.
- Rebuilt the optimized MPP integration with root CMake, using the pinned XRDP 0.10.6.1 and xorgxrdp 0.10.5 sources. Deployed their staged artifacts to the public `xrdp-rkmpp` / `xrdp-rkmpp-xorg` prefixes.
- Replaced the three historical server installations with **one TCP 3389 instance**. The listener and sesman are enabled together; sesman listens on its local Unix socket. Retired units, configurations, installed prefixes and the obsolete development checkout were archived, checked and removed from active paths. Rollback archives are private and are not source/build inputs.
- The current source tree and instructions no longer install alternative 3390/3391 instances. Historical benchmark reports retain their original conditions.

## Environment

KICKPI K7 / RK3576, vendor Ubuntu 24.04 arm64, Linux 6.1.75, GCC 13.3.0, CMake 3.28.3, Python 3.12.3, Xorg 21.1.7. Both HDMI and DP reported **disconnected**. Client: a separate LAN Linux x86_64 host, FreeRDP 2.11.5 with FFmpeg AVC420 decoding in a private Xvfb display. The client pinned the new certificate fingerprint obtained over verified SSH.

Settings: MPP, QP 18, DIRECT=1, PIPELINE=1, 16 ms frame interval, one virtual Xorg/Xfce desktop. Initial acceptance used STRICT=1; the second connection used daily STRICT=0 after restarting the listener. Daily mode retains the upstream in-process x264 recovery path and does not create another service.

## Checks and observations

| Check | Result |
|---|---|
| Native CMake configure/build/stage | Both components passed; launchers report XRDP/sesman 0.10.6.1 |
| Pipeline parser/ownership | 100,049 guarded cases passed with UBSan |
| NEON conversion/reference | 605 cases and exhaustive RGB checks passed, normal and UBSan builds |
| Lifecycle and intentional recovery | 80 cycles; 2,552 total checks; fd 11 → 11; RSS 5,232 → 5,232 KiB; intentional lost-output fallback recovered with IDR |
| Headless RDP login | Actual virtual Xorg and Xfce started using only public installation paths |
| Dynamic resolution | 2560×1440 → 1920×1080 → 3840×2160 → 2560×1440; MPP re-open and first hardware frame completed at each size |
| Synthetic desktop | 60 s movement/color/mixed cycle; 3,580 application draws, 20 skipped ticks |
| Hardware activity | RKVENC interrupt counter increased 4,025 → 6,221 during that cycle; kernel sessions showed active H.264 encoding |
| Independent client output | Captured decoded desktop and synthetic window were visually inspected; no real office content was published |
| Daily mode after removing old installations | New login and 15 s mixed animation passed; 897 draws, 3 skipped ticks; first hardware frame completed without fallback |
| Reconnection | FreeRDP disconnected and reconnected to the daily desktop successfully |
| Service/port state | Only `xrdp-rkmpp` and `xrdp-rkmpp-sesman` remain as XRDP units, both enabled; 3389 reachable from Windows, no listeners on 3390/3391 |

The first fresh desktop exposed a Wi-Fi scan authorization prompt from `nm-applet`. Restoring the RDP-only XDG autostart override eliminated it on a fresh daily-mode session. The installation recipe now creates this override without altering NetworkManager or the local desktop. This existing board also retained its minimal optional PulseAudio startup configuration under the new public namespace; audio redirection was not tested.

No runtime MPP failure or software fallback occurred in the real RDP connections. The lifecycle test deliberately injects failures; its fallback messages are expected and are separate from desktop acceptance. Initial client certificate-format troubleshooting and a TCP-only connectivity probe produced connection-level log errors, not encoder failures.

## Limits

This is an actual public-namespace deployment on the existing development board, **not a second factory-fresh board installation**. No OS reboot was performed; unit enablement was checked but a cold boot was not retested. Ubuntu 22.04/20.04, other boards, multi-user operation and audio remain unqualified. This round used FreeRDP; no new Windows mstsc visual/latency measurement is claimed.

Application draw counts and the driver's nominal fps fields are not delivered client FPS. Interrupt changes prove activity, not latency or sustained frame rate. This consolidation does not add new performance claims to the historical [matched benchmark](../performance.md).
