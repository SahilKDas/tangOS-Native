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

- `core`: argv construction, INI configuration, path policy, status parsing,
  check discovery; independent of window handles.
- `platform`: Unicode CreateProcess, restricted handle inheritance, streaming
  pipes, Job Object process-tree cancellation, durable logs.
- `repository`: Git root discovery, status, refs, worktrees, explicit staging,
  staged and outgoing-history review, GitHub CLI adapters, upstream comparison.
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
