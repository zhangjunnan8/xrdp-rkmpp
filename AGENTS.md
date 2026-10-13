# Contributor and automation rules

This repository maintains one implementation: the optimized RK3576 direct MPP integration in `rk3576/`. The generic prototype and historical multi-instance installers have been removed. Do not reintroduce them without a new requirement.

Root CMake defaults to the active integration, fetching pinned upstream Git revisions. `XRDP_RKMPP_FETCH_ONLY` is source/patch checking only; it never counts as native build or hardware validation. Deployment defaults to one headless Xorg/Xfce instance on TCP 3389 with MPP, DIRECT and PIPELINE enabled. The main daemon and sesman are two components of that one instance. The supplied RDP profile defaults to 1920x1080, with manual resolution changes allowed; do not describe this as a server-enforced limit.

1. Before changes, inspect Git status, branch, commit and remotes. Preserve existing work. Do not merge branches or rewrite published history implicitly.
2. Keep runtime changes focused. Preserve software fallback, explicit diagnostics, source-buffer ownership, cache synchronization and real client flow control.
3. Pin upstream versions and checksums. Keep upstream attribution and dependency license information.
4. Build and test hardware changes natively on the intended board. Distinguish static checks, standalone encoding, actual RDP encoding and subjective desktop feedback.
5. Require explicit site configuration for remote operations. Verify target identity before changes. Use an idle dedicated test host or an agreed maintenance window; service updates interrupt sessions. Do not leave comparison instances installed or enabled after acceptance.
6. Do not commit passwords, private keys, access tokens, host-key fingerprints, real private-network addresses, personal home paths, production configuration, desktop captures or raw session logs. Keep site configuration and generated output in ignored local directories.
7. Do not copy SSH private keys between machines. Retain known-host verification. Keep deployment and validation credentials out of command-line arguments and source files.
8. Do not upgrade drivers, restart active desktops or delete user data as part of an unrelated change.
9. Document actual validation scope and limitations. Benchmarks must state the metric, settings, client, workload and comparison baseline; do not claim unmeasured FPS or latency.
10. The development branch and sanitized public snapshot serve different purposes; consult `docs/public-release.md` before migration. Do not publish unrelated branches or private development history by mirroring the repository.
