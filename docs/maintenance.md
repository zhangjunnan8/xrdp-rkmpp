# Maintenance / 维护

## Preserve reproducibility

Retain the source SHA, pinned upstream Git revisions, build logs, stage tree, configuration backup and sanitized acceptance report for each deployed release. Keep a verified rollback archive outside active installation paths; do not leave redundant services enabled or old binaries installed after acceptance. Raw traces and desktop captures stay outside Git.

Before updating, inspect actual executable/module paths and connected sessions. A directory's date or name does not establish that it is obsolete. Review configuration, back up the selected instance and plan for its session interruption before switching versions.

维护时保留当前版本、回退版本与实际验证材料。升级会影响对应实例的桌面；不要把 `lab` 名称理解为可以随时重启。现场路径、账号与归档位置记录在本地运维材料中。

## Cleanup

Archive only confirmed obsolete build output, verify archive contents and hashes, and check that running processes do not reference it before removal. Never use broad recursive cleanup against installation prefixes or the source repository. Generated NV12 references can be large; reproduce them from the supplied tests when needed.

Do not remove packages merely because they are marked automatic: custom builds may depend on libraries outside the package manager's dependency graph. Evaluate kernel, Xorg, Mesa, libdrm, MPP, Mali and RGA changes separately from this integration.
