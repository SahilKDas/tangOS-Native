# Security and destructive-operation review

## Enforced by the app

- Process operations are isolated from UI. General commands use CreateProcessW
  with individually quoted argv, not shell strings. Executables resolve from
  PATH before changing cwd. Inherited handles are limited to standard pipes/NUL.
- Every process runs inside a kill-on-close Job Object, assigned while suspended.
  Failure to contain the process refuses execution. Cancellation terminates
  descendants. Durable logs are flushed continuously; UI output is bounded.
- No automatic mass staging, reset, clean, force push, or PR merge is exposed.
  Explicit stage paths reject traversal and protected paths. Merge/rebase/pull
  require a clean tree; operations that alter history/tree require confirmation.
- Commit scans the entire index, including pre-staged files. Renames are examined
  as deletion plus addition, so protected originals cannot be renamed around
  the policy. Push scans each outgoing commit, including intermediate commits
  later deleted/reverted and each parent's merge changes.
- Port-only defaults on and blocks `src/`. Known ROM/asset/credential paths and
  extensions are always excluded. INI exclusions and Git ignores also apply to
  tracked/staged files. Symlinks/submodules require external review.
- Blob scans reject DS ROM signatures, known private-key delimiters and common
  GitHub/AWS credential prefixes. Blobs over 16 MiB and command captures over
  64 MiB fail closed. Full commit/patch previews precede commit/push and are
  recomputed after confirmation.
- No token is stored by Lite. Git/gh use their credential stores. Remote URLs
  added through the UI cannot contain HTTPS userinfo. Native ROM verification
  only reads the chosen file and emits a SHA-256; no data is packaged/extracted.
- The app requests ordinary user privileges. It imports only Windows system
  DLLs, including UCRT present on supported Windows versions.

## Trust boundaries and remaining risks

Repository tools, Git hooks/configuration, PATH executables, and AI agents are
trusted external code. A desktop wrapper is not a write sandbox: a malicious
script/hook can modify `src/`, leak credentials, or alter staged content. An
external agent can race the final safety check and Git operation. Coordination
must provide exclusive publication ownership. Hooks are intentionally preserved
because the decomp relies on its pre-push checks; do not disable those checks.

Content signatures are conservative heuristics, not a complete Nintendo-asset or
secret classifier. Encoded credentials/assets can evade them. Explicitly excluded
local assets must be configured in Git ignores or `exclusions`; arbitrary natural
language exclusions in AGENTS.md are conveyed to the human/agent but cannot be
mechanically inferred. Binary/text previews still require human review.

Existing remote credentials may appear in `git remote -v`; scripts may print
secrets. Complete logs are local, never uploaded automatically, and may contain
sensitive output. Review before sharing; ordinary Windows account ACLs protect
the local directory. Keep PATH and repositories under your own control.

GitHub readiness is a reported server snapshot, not an authorization to merge.
Authentication, branch policies, pending checks, and the actual merge decision
remain with GitHub. New-branch push is deliberately blocked until the destination
exists and has been fetched. Remote divergence is handled by Git's normal
non-force push checks.

Tests verify safe/blocked index paths and credentials, ignored assets, historical
asset removal, ref validation, actual disposable commits/remotes/worktrees, and
process-tree cancellation. Tests do not establish behavior of arbitrary external
scripts or all credential encodings.

The TinySkia raster bridge receives app-owned pixel buffers and bundled PNG data;
no repository-provided image or font is decoded. The Nunito font is registered only
for the app process. Rust dependencies are exact/lockfile pinned and release builds
run offline after fetching dependencies. Rendering changes do not bypass Git previews,
port-only path guards, or destructive-operation confirmations.
