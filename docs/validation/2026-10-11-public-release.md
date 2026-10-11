# Public release preparation / 开源发布整理验收

Date: 2026-10-11. Scope: documentation, publication hygiene, upstream attribution, native build-header discovery and explicit lab site configuration. The active encoding/pipeline algorithms are unchanged; compare [runtime source provenance](../benchmarks/runtime-provenance.json).

## Checks completed

| Check | Result |
|---|---|
| Pre-release reachable Git history | 42 commits, 125 distinct file blobs; internal infrastructure references and personal commit email found; no private-key/token/literal-password patterns found |
| Current source tree | Targeted credential/private-network scan and manual review; local Markdown links resolved |
| Python syntax | 15 Python files parse with Python 3.8 grammar; not a Python 3.8 runtime compatibility claim |
| Native shell checks | 23 documented shell blocks plus both native build scripts pass `bash -n` on RK3576 |
| Explicit lab identity | 6 isolated guard checks: missing/wrong hostname, wrong SoC, intended SoC, root rejection, configured non-root home |
| Controller configuration | 7 malformed/mis-targeted configurations rejected before SSH/password processing |
| Fixed upstream sources | Both release archive SHA-256 values verified; XRDP and xorgxrdp patch scripts apply to pristine pinned archives |
| Scalar reference attribution | Generated reference retains xorgxrdp file-specific copyright/permission notice |
| Native converter | 605 bit-exact cases, all RGB values, strides/tails/write canaries pass against the generated upstream reference |
| Pipeline parser | 100,049 guarded cases pass with UBSan |
| Xorg configuration | `autoreconf` and configure succeed with staged XRDP headers, without the previous private pkg-config path |
| Active desktop | XRDP/sesman remain active with unchanged process IDs; no service restart, deployment or desktop benchmark |

No new desktop performance claim is derived from these checks. The public lab configuration changes are not yet qualified by an end-to-end RDP workload on another environment. Existing 2026-10-10 performance data remains historical, with its original settings and limits.

The original development history remains on the existing Git server. Publish only the new-root release branch described in [public-release.md](../public-release.md); a normal cleanup commit cannot remove historical private information.

本次不改变已运行的桌面版本，不重新跑办公会话。公开分支以独立根提交避免携带旧提交中的现场信息；旧构建 SHA 在报告中仅作为历史产物标识。
