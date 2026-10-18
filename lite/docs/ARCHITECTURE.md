# TangOS Lite architecture

Windows 10/11 x64; C++17 and Win32 common controls. One statically linked
portable executable, no browser, installer, embedded ROM, or bundled toolchain.
The native TinySkia skin reproduces Console's Aero tokens and shell layout; see
UI-PARITY.md for the exact reference and remaining screen differences.

## Repository inspection

The existing Console uses `tangos.json` (`schema/tangos.schema.json`), argv-based
tool launches in `console/src/main/runTool.ts`, and porcelain status / GitHub
operations in `gitsafe.ts` and `pullRequests.ts`. This checkout is the Console,
not the decomp. A locally available 64DS-DX checkout was inspected read-only:
`AGENTS.md`, `port/build-port.cmd`, and argparse definitions for ROM, declaration,
dead-reference and link checks. The port script discovers MSVC, builds with CMake,
and runs CTest; it needs user-owned extracted assets. Do not replace that script
with a guessed compiler command.

## Boundaries

`dpi` isolates the Win32 coordinate boundary. Layout and hit tests use 96-DPI
units; child-window positions, native fonts and buffers use each window's DPI.
Registered window procedures translate size/input/owner-draw messages, handle
the suggested `WM_DPICHANGED` rectangle and refresh the child tree. GDI paint
DCs map logical units to physical pixels. TinySkia renders directly into those
physical buffers, with DPI included in background/backdrop cache identities.
Capture files store physical pixel dimensions. Deterministic tests inject window
DPI at this boundary without changing desktop settings; real attached-monitor
coverage is reported separately from simulated 175%/250% transitions.

- `core`: argv construction, INI configuration, path policy, status parsing,
  check discovery; independent of window handles.
- `platform`: Unicode CreateProcess, restricted handle inheritance, streaming
  pipes, Job Object process-tree cancellation, durable logs.
- `repository`: Git root discovery, status, refs, worktrees, explicit staging,
  staged and outgoing-history review, GitHub CLI adapters, upstream comparison.
- `controller_view`: pure batch/activity transformation shared by the native
  Controller and original-source comparison tests. A bounded activity cache
  keeps complete logs out of the repaint path.
- `ui`: native controls, background workers, streamed log events and explicit
  approval dialogs. No Git or shell strings assembled by controls.

Git and gh resolve from PATH. Credentials stay in their existing credential
stores. Checks are repository code: show the exact command and require explicit
trust before running. Built-in check discovery only enables known existing paths.
Local INI overrides use argv tokens, never general shell commands. The one shell
adapter calls the inspected `port/build-port.cmd`, with a restricted path.

## Delivery sequence

1. Select/remember repository; status and discovered checks; responsive log viewer,
   durable logs, cancellation; core and disposable-repository tests.
2. Explicit Git actions, upstream comparison, safe commit/push previews, gh PR
   creation and readiness, AGENTS coordination handoff.
3. Rebuild twice, test the packaged executable and its real window workflow,
   report byte size and remaining limitations.

## Safety model

No automatic `git add -A`, reset, clean, force push, credential collection, ROM
extraction, or source repair. Port-only commits reject `src/` including rename
sources. Commit/push scan paths and blobs for excluded assets and recognizable
credentials. Index and outgoing history are inspected before approval and checked
again afterward. Pull is fast-forward only; merge/rebase require a clean tree and
confirmation. Exclusions augment Git ignore rules and apply even to tracked files.
Repository scripts/hooks and other agents are trusted external programs, not a
sandbox; their behavior cannot be guaranteed by a desktop wrapper.

Settings and complete logs live under `%LOCALAPPDATA%/TangOSLite`; no machine paths
or secrets enter the distribution. Agent support produces a policy handoff from
root and nested AGENTS.md and the repository coordination guide; it does not claim
to implement or silently enroll in a remote fleet protocol.

## Native Console execution

Descriptor parsing and argv expansion are isolated in descriptor; atlas_layout ports Console squarify without a browser. Fleet owns per-agent workers/Runners, queues, persistence, worktrees, audit and verification. Vault uses Windows DPAPI; mcp uses an authenticated loopback HTTP service; network uses WinHTTP HTTPS with bounded reads/timeouts. ConsoleUI owns only native controls and rendering. Python driver adaptation injects repository instructions into supported instruction hooks. No provider SDK or model is bundled.

## Backend parity services

Backend owns a native JSON request dispatcher, atomic local preferences/project/statistics/report stores, repository-bound single-use mutation previews, source/attempt/history queries and user-configured service adapters. Transport is injected for unit tests and WinHTTP in the packaged executable. RemoteLease owns atomic range requests and a cancellable heartbeat/release lifecycle for opted-in API/CLI work. Fleet feeds statistics/harvest records after independent verification and refuses held targets/transcribed source. MCP exposes read-only backend methods and filtered tool discovery; local users confirm writes through the executable backend interface. Connection profiles are disabled by default and reference local vault/environment credential names; no owner credentials or cloud service are bundled.

## Parity additions

`viewer.cpp` owns testable camera, LOD, marquee and directional navigation math. `console_ui.cpp` paints native views and publishes source/history/services results from worker threads using synchronized handoff; generation IDs discard stale Atlas responses. Module windows have Viewer state and relay drafts to the parent without acquiring another fleet lock. Background rendering and font/frame caches remain in the TinySkia/skin layer.

Backend policies and statistics are shared across manual execution, fleet and MCP. Repeated batch logs append; persisted assigned-target lists distinguish the current batch from subsequently queued work. Explicit sync binds confirmation to repository content/refs, backs up allowed changes and pins recovery history before resetting. Original-code comparison tests run retained TypeScript as development oracles, outside the packaged runtime. The parity inventory distinguishes tested behavior from unresolved public contracts.

BatchBook isolates pure draft/batch lifecycle and retention from UI and Git. Fleet persists it atomically alongside agent queues, synchronizes queue edits, parks interrupted work and incorporates saved batch instructions into repository-scoped prompts. Native controls edit drafts and display history; worker completion updates the model without destroying editor contents.

`archive` validates bounded stored/DEFLATE project imports before extraction,
including Windows ordinal case aliases, path conflicts, protected content and
the project descriptor. `updater` separates trusted download staging from the
post-exit helper transaction; it validates publisher checksums and executable
headers, rechecks the installation baseline and retains a rollback executable.
UI code owns confirmation and restart scheduling, while replacement remains in
the isolated native service. Project discovery/descriptor downloads and provider
traffic require separate local opt-ins. Portable update download/install remain
manual; an opted-in automatic check only reports availability. Dev-only MCP Inspector and reference
browser dependencies stay outside the portable executable.

Remote project selection resolves through `projects.open` in the backend. A validated descriptor becomes a confined metadata cache; the main window stores its registry ID separately from the local repository path. ConsoleUI receives a remote-mode flag and does not instantiate execution services. Published reads use the existing connection layer; injected read-only transport makes the native GUI regression independent of external credentials and services.

## Shared live-data services (0.33 source checkpoint)

`live_data` contains transport-independent SSE framing, cosmetic normalization,
progress snapshot/delta reduction, live-only record overlays and GitHub identity
mapping. `backend` owns per-repository/descriptor/connection/principal cache state
and approved stale generation; `network` owns asynchronous WinHTTP streaming,
redirect refusal, content-type validation and cancellation. Window code consumes
coalesced events on its UI timer and loads data in workers. HTTP requests release
cache locks, allowing a concurrent state read or event update during a slow request.

Live progress supports full matched sets, added/removed deltas and changed function
records. It overrides the Live view only; local generated records remain local.
Authoritative live statistics feed the Viewer counters. Invalid local cache blobs
are reloaded from the repository database. Atomic cache writes reserve space for
both file copies plus 32 MiB; the developer workstation's free-space policy is
not a storage prerequisite imposed on other users.

The current release-build and GUI-verification storage floor is 11 GiB, with
additional build headroom. Builds are serial and disposable outputs are removed
after retaining evidence. Original React reference tools use an installed browser
only for offline comparison and are excluded from the executable.
## Five-journey lifecycle (0.38)

Root-window state publications carry a request generation. Repository/settings
selection commits after preparation succeeds; obsolete messages are discarded.
Fleet restore verifies repository ownership, parks interrupted phases and preserves
pending reviews. Draft revisions prevent asynchronous edit/undo overwrite. UI
preferences and update status use atomic same-directory replacement. Failed Atlas
refresh retains the prior model and exposes a stale notice.

MCP's popup separates public endpoint copy, explicit private configuration copy,
and expandable public stdio setup. The active server owns its connection path.
Registration remains reviewed and baseline-bound; outcomes survive modal setup.
Git synchronization previews include repository/branch/ref/SHA, affected files,
remotes and backup root. Backup manifests distinguish staged and working content.
Cancellation after reset stops further deletion and retains a phase-specific log.

Update checks never authorize downloads. Manual download persists its receipt;
manual restart launches the existing post-exit helper. Durable results distinguish
replacement failure from successful installation with restart failure. The helper
uses its validated receipt's store so isolated portable copies can be tested.