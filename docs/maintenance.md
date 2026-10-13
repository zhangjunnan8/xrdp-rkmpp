# Maintenance / 维护

## Preserve reproducibility

Retain the current source SHA, pinned upstream Git revisions, current build/stage and sanitized acceptance report. During a version switch, a temporary verified rollback archive can support recovery. After acceptance, follow the owner's retention policy: delete obsolete installations and archives when no longer needed, rather than recreating or retaining them indefinitely. Raw traces and desktop captures stay outside Git.

Before updating, inspect actual executable/module paths and connected sessions. A directory's date or name does not establish that it is obsolete. Review configuration, back up the selected instance and plan for its session interruption before switching versions.

维护时保留当前版本及必要的验证记录。旧实现、构建副本和回退归档的保留期限由维护者决定，明确不再需要时应在确认无运行引用后删除。升级会影响对应实例的桌面；不要把 `lab` 名称理解为可以随时重启。现场路径和账号记录在本地运维材料中。

## Cleanup

Identify exact obsolete paths and check that running processes do not reference them before removal. If rollback retention is requested, verify archive contents and hashes; if permanent removal is requested, do not create another archive of those obsolete files. Never use broad recursive cleanup against installation prefixes or the source repository. Generated NV12 references can be large; reproduce them from the supplied tests when needed.

Do not remove packages merely because they are marked automatic: custom builds may depend on libraries outside the package manager's dependency graph. Evaluate kernel, Xorg, Mesa, libdrm, MPP, Mali and RGA changes separately from this integration.
