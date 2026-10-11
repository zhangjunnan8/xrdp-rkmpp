# CMake workflow checks / CMake 构建流程检查

Date: 2026-10-11. Base: public commit `d527c53`. This change makes root CMake the default build entry point for the RK3576 XRDP integration; the old generic prototype requires explicit opt-in.

## Implemented / 已实现

- Shallow Git fetch at exact XRDP/xorgxrdp commit IDs; XRDP's three submodules use the pinned gitlinks.
- Separate pristine dependency checkouts and marked generated work trees. Integration changes regenerate from pristine source instead of applying patches twice.
- Native build orchestration for Autotools, staged XRDP headers, isolated Xorg module output, helper/test compilation, artifact shortcuts and an installation-variable file.
- Explicit source-only workstation mode, native ARM64 checks, positive job-count checks and prevention of mode changes in an existing build directory.
- No installation of system packages/files, MPP hardware tests, service changes or RDP connections during the default build.

## Executed locally / 本地已执行

Environment: Linux x86_64 under WSL, CMake 4.2.3, Python 3.14.4, GCC 15.2.0. CMake 3.16 compatibility is the intended API baseline, not an independently executed tool-version test.

| Check | Result |
|---|---|
| Real upstream Git downloads via source-only CMake mode | Passed for both projects and XRDP's three submodules |
| Checked-out HEADs and tracked source state | Match pinned commits; pristine source remains unchanged |
| Actual integration scripts on Git source trees | Both pass; MPP integration and NEON reference source generated |
| Repeated build | Reuses patched source trees without downloading or patching again |
| Temporarily change, then restore an integration header | Both changes trigger regeneration from pristine upstream; generated header matches each input; original source bytes restored |
| Wrong upstream revision | Rejected before patching |
| Root/outside output paths and non-empty unmarked output | Rejected; unrecognized file content preserved |
| Non-ARM64 default configuration, zero jobs, source/native mode switch | Rejected with explicit messages |
| Source-only mode outputs | No `bin/` or `install-env.sh`; no binary/hardware result |
| Explicit legacy prototype with x264 disabled | Builds; existing contract test passes |
| Updated installation instructions | 16 relevant Shell blocks, two legacy build scripts and two embedded Python blocks pass syntax checks; configuration generation and two isolated systemd units pass the earlier fixture checks |
| Build helper | Parses with Python 3.8 grammar |

The legacy contract test concerns the historical generic API only. It does not validate MPP or this XRDP runtime.

## Pending / 尚待板端验证

The RK3576 board was unavailable and the maintainer requested local implementation/checks first. **Native ARM64 configure, compilation, linking, stage generation and execution through the new CMake path have not yet run.** In particular, workstation source checks do not prove binary output or the generated install environment on the target board.

No new hardware-encoding, RDP or performance result is claimed. Before treating this build path as board-validated, run the documented native CMake build with the vendor SDK, inspect staged binaries/modules and logs, then perform the existing isolated hardware/RDP acceptance procedure. Current office services were not changed.

RK3576 暂未开机，本次按维护者要求完成本地实现和检查。新入口的原生编译及安装产物仍须板端验收；现有硬编码性能数据来自先前构建，不能替代这次构建入口的验证。

See [CMake build guide](../building.md) and [fresh-board installation](../new-board-install.md).
