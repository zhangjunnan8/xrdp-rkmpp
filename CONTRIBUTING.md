# Contributing / 参与贡献

The active scope is direct MPP integration for RK3576. Read [limitations](docs/known-limitations.md), [development workflow](docs/development.md) and [AGENTS.md](AGENTS.md) before changing code. Contributions do not need a new abstraction layer unless there is a demonstrated requirement.

## Useful issue reports

Include board model, OS, kernel/BSP, MPP package/runtime, Xorg version, source commit, RDP client/decoder, resolution, relevant encoder settings and a minimal reproduction. State whether the current connection logged `first hardware frame complete` or switched to x264. Attach only sanitized log excerpts, not complete user sessions.

报告问题时说明设备与完整测试条件。区分“选中了 MPP”“确实输出硬编码帧”“客户端显示正常”和“主观体验流畅”。删除日志中的地址、用户名、路径、凭据与私人桌面内容。

## Validation expectations

- Documentation/configuration-only changes: links, syntax, examples, privacy scan and affected configuration guards.
- Conversion changes: native `test-nv12`, bit-exact reference comparison and UBSan.
- Pipeline/ownership changes: `test-pipeline`, direct decode with poisoned source reuse, lifecycle/fallback and actual RDP.
- Encoder changes: `verify.py`, `test-lifecycle`, resizing, strict failure and explicit software recovery, then a real isolated RDP session.
- Performance claims: follow [the benchmark method](docs/performance.md), preserve settings and quality, rerun on an independent client and report variation.

Do not run tests against an occupied office desktop, silently replace vendor drivers, or claim untested boards work. Site configurations and generated output belong under `.local/`, `artifacts/` or outside the repository.

Project-authored contributions are provided under Apache-2.0. Preserve third-party attribution and note new dependencies in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
