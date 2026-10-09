# Security and destructive-operation review

The reference-shaped Controller footer retains the native safety policy: Writes
is explicit and defaults OFF; Review remains ON; Push remains OFF and opens the
reviewed Git workflow. These last two controls intentionally do not enable the
original Console's unattended rolling-PR pipeline, because every commit and push
must retain its own preview. MCP also defaults OFF until the user opts in.

Native debug snapshots are saved locally, with no upload. Their JSON contains
UI geometry, reviewed policy flags, agent identifiers/phases and queue counts;
it omits key values, driver commands, prompts and log bodies. Known vault values
are also redacted recursively from metadata strings. The window image captures
visible UI content, so users must review it before sharing. Snapshot files are
not staged or committed by the application.

## Enforced by the app

- Process operations are isolated from UI. General commands use CreateProcessW
  with individually quoted argv, not shell strings. Executables resolve from
  PATH before changing cwd. Inherited handles are limited to standard pipes/NUL.
- Every process runs inside a kill-on-close Job Object, assigned while suspended.
  Failure to contain the process refuses execution. Cancellation terminates
  descendants. Durable logs are flushed continuously; UI output is bounded.
- No background publication, reset, clean, force push, or PR merge is exposed. Explicit sync requires a content/ref-bound preview and confirmation. It pins the prior HEAD in a recovery ref, saves allowed changed files, then resets to the reviewed SHA. Only explicitly previewed regular untracked files can be removed; protected assets and port-only source changes block or survive the operation.
  Agent Review explicitly stages all audited changes in its isolated worktree.
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

## Fleet review

Agents run in isolated Git worktrees with repository and nested AGENTS instructions. Before review/commit, path/content policy rejects src changes in port-only mode, excluded assets, ROM signatures and credentials. Local queue claims prevent duplicate assignments. Cancellation uses Windows Job Objects. Local MCP requires a cryptographically random bearer, rejects browser Origins, bounds requests and uses loopback only. DPAPI protects vault files at rest; Runner redacts environment secrets before disk logging. Landing requires explicit confirmation and decomp mode, then independent checks and staged-tree review. No automatic fleet commit, push or merge.

Repository scripts and CLI agents are trusted executable code, not sandboxed applications. Instructions and worktree audits cannot prevent arbitrary executables from accessing other paths or the network. Secret redaction is exact-value matching, not a guarantee against encoded/transformed secrets. Independent verifier availability and real compiler/assets remain project-specific.

## User-owned connections and backend requests

No external connection profile is enabled by default. Each URL/method/key reference is configured locally by the user. Native HTTPS requests refuse redirects and URL credentials, validate header text, bound sizes/timeouts, and redact selected environment/vault secrets in responses. HTTP is accepted only at 127.0.0.1 for local services/tests. JSON settings reject credential fields; reports omit vault/connection values. Mutation previews are single-use, time-limited and bound to method, arguments, repository-content hashes and settings. Agents cannot authorize these mutations via MCP. Optional automatic lease profiles are user-approved connections, with atomic acquisition and heartbeat cancellation; service TTLs remain necessary for network/process failure recovery. Safe mode and tool visibility apply at execution as well as discovery. No auto-push is accepted. Source transcription policy mirrors Console's whole-file HAND-ASM/NONMATCHING banner exceptions.

Tool visibility and near-miss policy are enforced by manual execution and the fleet/MCP paths. Connection changes require exact configuration previews; profiles remain disabled until the user enables them. Source/history reads remain confined to the selected checkout, and late asynchronous Viewer responses cannot cross repository-data generations. Repeated batches append logs rather than truncating them.

MCP connection telemetry excludes bearer credentials. Client configuration and copied prompts intentionally include the per-session local token for the user-selected client; these files live in local application data, outside the repository. Sessions are capped at 128, can be explicitly deleted, and expire after 30 idle minutes. Stopping MCP cancels only external MCP jobs.

Source reads relax only the port-only editing restriction; all asset, credential and local exclusions apply to the resolved repository-relative path. Repository paths reject control characters, NTFS alternate streams, device paths and escapes. Process arguments reject embedded NULs. Agent instructions are path-confined, content-screened and limited to 1 MiB per file / 4 MiB total. Locally updated instructions are delivered as prompt text, without copying untracked assets into the agent worktree. New profiles require unique trimmed names; legacy duplicate MCP names cannot silently route to the first agent.

## Remote viewer boundary (0.15.0)

Remote descriptors are validated before registration. Opening a project writes only a confined metadata cache (`tangos.json` and a viewer-only marker), never clones or executes descriptor tools. Registry and active selection are human-readable local files. Published reads begin only when the user opens/reloads Viewer; enabled connection profiles and local credentials remain user-controlled. Remote windows create no Fleet or MCP server. Tool execution, Git state changes and keyboard/mouse work assignment are blocked in viewer-only mode. The existing clone service retains preview, confirmation, cancellation and full logs. A remote descriptor is still untrusted configuration: choosing a local checkout later does not sandbox its scripts.

## Discovery and archive import (0.16.0)

Registry discovery requires the user's enabled `projects.registry` connection.
Automatic startup discovery requires its separate `automatic` opt-in. Public
GitHub descriptor downloads require `allowDescriptorDownloads`; registry secrets
are not forwarded to that host. A failed refresh retains cached project data.
Downloaded descriptors are validated, credential-screened and limited to 1 MiB.

ZIP import validates the central directory, file boundaries, CRCs, bounded
DEFLATE output and all file contents before creating a new destination. It rejects
traversal, device/alternate-stream names, Git metadata, links, duplicate paths,
file/directory collisions, protected assets and recognized credential content.
It does not execute imported scripts, stage files, or make an initial commit.
Importing fresh source files is distinct from repairing `src/`: subsequent
port-only edits, commits and pushes retain the existing source protections.
Git initialization uses an empty hooks path. A failure after extraction retains
the new folder for inspection; it never recursively deletes user data.

## Portable update boundary (0.16.0)

Updates require a user-enabled `update.check` connection, a trusted HTTPS
`assetPrefix`, and `allowUpdateDownloads`. Automatic startup staging additionally
requires `automatic`. The publisher must supply a SHA256 digest. Downloads never
receive registry or provider credentials; redirects are limited to the configured
prefix and GitHub release asset hosts. The digest establishes integrity relative
to the configured publisher, not an independent publisher signature.

Staging validates the digest, size and Windows x64 GUI executable headers without
replacing the application. Installation waits for the application to exit,
rechecks both executable hashes and uses a same-volume Windows replacement with
a retained rollback backup. A changed application invalidates the staged update.
Receipts and candidates must reside in the local update store. Failed candidates
are not executed. Updates never disable antivirus or add security exclusions.

## MCP request ownership (0.16.1)

Cancellation is keyed by authenticated session and typed JSON-RPC request ID.
It never selects another session's matching ID or an unrelated CLI/API agent.
Descriptor tools use a request-owned Runner registered under the Fleet mutex;
UI Stop and server shutdown can still cancel it safely. Cancelled calls release
their registration and keep durable logs. Peer cancellation suppresses the
protocol response; UI Stop returns an actionable cancelled tool result.
Waiting batch calls poll cancellation without claiming new work afterward.
Deleting a session cancels its in-flight requests, including stdio EOF teardown.
Stdio workers and output writes are bounded and synchronized. Provider reads
retain their normal network timeout; only local MCP calls have the longer
ten-minute receive budget needed for repository tools.

## Native client setup and activity (0.17.0)

Client installation requires a path/configuration preview and explicit user
confirmation. It merges only the `tangos-lite` entry, retains unrelated values,
copies the original file to a local backup, and rechecks its hash before atomic
replacement. Malformed files, reparse-point files and edits after review are
rejected. VS Code JSONC is normalized to JSON; original comments remain in the
backup. No client configuration or credentials are committed to the repository.

Activity capture receives the same redacted stream as durable logs and returned
process output. Nested sensitive arguments and known environment credentials
are removed before previews and activity events. The in-memory store is bounded;
complete logs remain on disk. These filters protect known credentials, not
arbitrary unknown secret formats printed by a trusted repository script.

Modern MCP is stateless and uses explicit per-request metadata, routing-header
validation and private, zero-TTL discovery results. Client display names do not
authorize execution. The authenticated local bridge explicitly selects the
configured agent; its opaque transport identifier scopes cancellation and
presence rather than serving as authentication. HTTP disconnect cancels its
request-owned tool, while stdio cancellation and EOF retain worker lifetimes.
The bearer token grants access to this user's enabled tools; the endpoint stays
loopback-only and rejects browser origins.
