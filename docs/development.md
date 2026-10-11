# Development workflow / 开发流程

The active implementation is the RK3576 direct MPP path. Root CMake and the generic ABI prototype are historical; see [repository overview](../README.en.md).

## Source and board responsibilities

Keep one authoritative source checkout for edits, review and commits. The board checks out an exact reviewed commit for native builds and runtime verification. Site addresses, credentials and absolute paths belong in ignored local configuration, not source control.

1. Inspect status, branch, HEAD and remotes. Preserve local and board-side changes before synchronizing.
2. Make focused changes and run applicable static checks. Record the exact source SHA.
3. Transfer that commit using your Git server, SSH or a Git bundle. Never copy private keys to the board.
4. Check the board's status, then check out the exact SHA. A directory name or “latest” is not proof of matching source.
5. Build using `rk3576/build.sh` and `rk3576/build-xorg.sh` into new directories outside the repository. Stage files before deployment.
6. Run affected standalone tests first. Hardware and RDP changes require native validation on an isolated instance with no unrelated desktop sessions.
7. Record source SHA, dirty/clean state, compiler, BSP/MPP/Xorg versions, client, settings, commands, results and limitations. Save sanitized summaries under `docs/validation/`.

本机维护源码，板端按确切提交原生构建。现场改动先保存补丁并回收到主检出，不能直接覆盖；未经验证的编译成功不能标记为硬编码成功。任何已经用于办公的测试端口都要按真实办公会话对待。

## Public releases

Contributors clone public `main` from [zhangjunnan8/xrdp-rkmpp](https://github.com/zhangjunnan8/xrdp-rkmpp). Use the public build arguments and installation paths in [getting started](getting-started.md). Only maintainers publishing from the original source repository use the sanitized `release/rk3576-open-source` branch; see [release procedure](public-release.md). Historical build IDs in validation reports identify older private development artifacts. Do not manufacture new benchmark claims when only documentation or lab configuration changes.
