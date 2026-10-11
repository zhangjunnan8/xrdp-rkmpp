# Public release provenance and privacy / 公开发布说明

## Public installation / 公开安装

Install from **[zhangjunnan8/xrdp-rkmpp](https://github.com/zhangjunnan8/xrdp-rkmpp), branch `main`**. Follow [the Chinese installation recipe](new-board-install.md) or [the English overview](getting-started.md). The migration instructions below are for maintainers; users do not need the private development repository or its release branch.

公开安装使用 GitHub 的 `main`；下面的发布分支名仅用于维护者从原仓库发布，不是用户克隆或编译时要填写的分支。

## Maintainer publication branch / 维护者发布

**Publish only `release/rk3576-open-source` from the maintainer's source repository.** Its initial commit is a new root containing the reviewed source tree; later public commits descend from that root without private development ancestry. The existing development branch remains available on the original Git server; no existing history is force-rewritten.

**迁移时仅使用 `release/rk3576-open-source`。** 原开发分支的旧历史仍包含个人提交邮箱、现场地址、用户名路径和主机指纹。修改当前文件不会删除这些历史；不要直接使用整库导入、`--mirror`、`--all` 或把旧 `main` 一起推到公开仓库。

In the maintainer's source checkout, review the public branch and push only that ref to GitHub `main`:

```sh
git log --oneline release/rk3576-open-source
python3 tools/check-public-tree.py --ref release/rk3576-open-source
git -c push.followTags=false push https://github.com/zhangjunnan8/xrdp-rkmpp.git \
  refs/heads/release/rk3576-open-source:refs/heads/main
```

This publishes reviewed public commits to GitHub `main` without changing the original remote configuration or pushing tags. Do not push other refs or merge private development ancestry into this branch. The non-personal author identity on the initial public snapshot is a project attribution, not a claimed verified GitHub identity.

## What was reviewed

The pre-release scan examined **42 reachable commits and 125 distinct file blobs** across local branch/remote refs, plus commit author/committer metadata. Targeted patterns and manual review found internal infrastructure references and a personal author email; they did not find private-key material, recognized access-token formats or quoted literal password assignments. This is a scoped review, not a claim that a pattern scanner detects every possible secret.

The current tree replaces original addresses, users and local paths with explicit parameters or reserved documentation placeholders. The RDP example contains no real endpoint or username. A live board SSH fingerprint has been removed from source; Paramiko continues to reject unknown/mismatched keys through the user's verified `known_hosts`. Credentials remain stdin/prompt input, not configuration fields or command-line values. Legacy helpers require an explicit expected hostname; their fixed port/display guards remain.

The public snapshot excludes ignored local configuration, generated SDK/build trees, raw traces, screenshots, private keys and archives. Upstream copyright names and email addresses in license notices are intentionally retained; they are not private project contact information.

公开内容保留技术环境、汇总数据和上游署名，移除现场运维身份信息。真实 SSH 指纹继续由本地 `known_hosts` 管理，不通过删除主机校验来实现脱敏。

## Performance provenance

Published measurements were recorded on 2026-10-10. The optimized runtime was built from development artifact ID `071e67e75c73c6d02e0b59de4e14510f48b4d205`; initial integration measurements used `79897f41d9bc41d8d50cd682260c353444596f58`. Those historical commits are not parents of the public snapshot and may not be available publicly.

[The provenance manifest](benchmarks/runtime-provenance.json) compares retained runtime source with the optimized artifact. The release preparation changes documentation, attribution, build-header discovery and lab configuration; it does not change the encoding/conversion/pipeline algorithms. The converter header gains upstream attribution, and the scalar-reference generator preserves the original license notice.

**No new end-to-end desktop benchmark was run for this documentation release.** Existing services were not restarted. Public lab parameters and identity guards receive static/isolated validation; they still need an end-to-end run on a newly configured test environment. Aggregate CSV data transcribes the existing reports and is not a new measurement or an independently reproduced dataset.

## Release checks

Run `python tools/check-public-tree.py` before committing and with `--ref` against the final publication commit. It checks non-ignored files for common key/token/password patterns and unintended private-network addresses, parses Python with 3.8 syntax, and checks local Markdown file links. It does not certify Python 3.8 runtime compatibility or replace manual review of configuration and Git history.

Preserve separate reports for native shell syntax, pinned upstream patch application, generated-source attribution and lab guard checks. Do not rerun a desktop workload on an occupied session just to produce a release checklist.

Detailed results: [2026-10-11 release checks](validation/2026-10-11-public-release.md).

The later correction to the public GitHub installation recipe has a separate [installation-check record](validation/2026-10-11-public-install.md). It introduces explicit `xrdp-rkmpp` / `xrdp-rkmpp-xorg` build namespaces; historical benchmark paths remain unchanged in their original reports.

Root CMake now defaults to the active integration and fetches pinned upstream Git commits. The [CMake check record](validation/2026-10-11-cmake-build.md) distinguishes local source/flow checks from pending native board compilation. The archive scripts remain available for historical workflows.
