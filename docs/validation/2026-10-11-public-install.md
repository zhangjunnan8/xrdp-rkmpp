# Public installation correction / 公开安装教程修正

Date: 2026-10-11. Scope: the public installation documentation and build-namespace arguments following public commit `caeeebf`.

## Changes / 改动

- Installation now clones `main` from `https://github.com/zhangjunnan8/xrdp-rkmpp.git`, records the actual public commit SHA, and uses `$HOME/src/xrdp-rkmpp`.
- Both language guides pass explicit build namespaces: `xrdp-rkmpp` for XRDP and `xrdp-rkmpp-xorg` for Xorg. Configuration, services, runtime sockets, logs, acceptance commands and source-update instructions use the same names.
- Build scripts accept the public namespaces. Their existing defaults and legacy namespaces are retained for existing lab callers. Encoding, conversion and pipeline source are unchanged.
- The first-install recipe records the installed source SHA. It still refuses an existing instance; fetching public source or producing a stage does not upgrade a running desktop.
- Historical lab installers/profilers and benchmark identifiers remain documented as historical. They are not presented as public installation or upgrade commands.

## Local validation / 本地校验

Checks ran in local Ubuntu under WSL on x86_64, with Python 3.14.4. They did not execute the installation block against the host system.

| Check / 项目 | Result / 结果 |
|---|---|
| Shell snippets in Chinese/English installation guides, integration overview and maintainer publication instructions | 17 blocks pass `bash -n` |
| Two build scripts | Pass `sh -n` |
| Public installation command blocks | No placeholder repository, private release branch, old repository directory or legacy lab installation namespace |
| Embedded configuration Python | Both blocks parse with Python 3.8 grammar |
| Build namespace/path guards | 11 cases cover public, legacy and default namespaces plus invalid names; rejection occurs before build or installation |
| Actual tutorial configuration generator | Runs against XRDP 0.10.6.1 upstream INI/TOML templates with Linux substitutions; assertions verify MPP selection, port 3391, service account, certificate/log paths, one display and the public Xorg module path |
| Generated systemd services | Both pass `systemd-analyze verify` inside an isolated root, with inert executable fixtures for path-existence checks; no service is started |

原始配置模板验证的是教程中的配置生成逻辑，不是新板安装结果。unit 校验使用隔离目录中的占位可执行文件，不代表真实二进制已经启动或完成动态链接检查。Python 3.8 语法解析也不等于在 Python 3.8 上运行通过。

## Limits / 未验证范围

The RK3576 board was unavailable over SSH during this correction. There was no new native ARM build, MPP encoding run, RDP connection, package installation or service restart. The public namespace has not been replayed end-to-end on a second blank board. Existing hardware/performance results retain their original artifact IDs and lab paths; this change makes no new performance claim.

板端暂不可达，本次仅完成本地检查；未进行 ARM 原生重新编译、硬编码复测或空白板完整安装。没有经完整验收的一键升级工具。已有安装应保留配置和旧版本，安排维护窗口并成对切换 XRDP/Xorg，随后重新验收。

Instructions: [中文安装教程](../new-board-install.md), [English installation overview](../getting-started.md), [release provenance](../public-release.md).
