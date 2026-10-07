# Windows release verification — 2026-10-07

TangOS Lite 0.4.0, Windows x64; C++17/Win32, statically embedded TinySkia and Nunito. GCC 14.1.0 UCRT64, CMake 3.29.3, Rust 1.98.1 GNU target. Release optimization, stripped executable and disabled PE timestamp. No bundled browser or compiler/toolchain.

The generated release SIZE.txt records exact bytes, SHA-256 and DLL imports. Two independent build directories must produce the same executable hash; the release script normally creates both from scratch. The 0.4.0 final package was produced by two clean builds with identical SHA-256.

Verification:

- 60 core/disposable-Git assertions: configuration, commands, repository/worktree detection, fetch/pull/merge/rebase, conflicts, staged/outgoing previews, forbidden assets/credentials/history, complete logs and process-tree cancellation.
- 40 fleet/descriptor/vault assertions: typed commands and safe paths, encrypted DPAPI keys, simultaneous API agents against a local harmless HTTP fixture, isolated worktrees and scoped instructions, duplicate-queue prevention, source-change rejection, independent checks, reviewed commit, explicit landing and port-only refusal, persistence, CLI cancellation and authenticated MCP execution including more than 128 requests.
- 50 backend-service assertions: registry, descriptor preview, source/history, local/remote claims, credential/URL rejection, opt-in connections, single-use/content-bound confirmations, queue vetting, backup previews/manifests, transcription-aware statistics/harvest and remote lease lifecycle/refusal.
- Packaged native WinHTTP/CLI loopback tests: disabled profiles send no request, environment credentials work, GET/POST execute with confirmed writes, credentials are redacted, redirects are refused, and exports contain no credentials.
- Native TinySkia pixel regression test and Python instruction-adapter test, including refusal to execute a driver without an instruction hook.
- Packaged executable SHA-256 match/mismatch tests use an innocuous abc file, not a ROM.
- Packaged native window selection → status → check → complete log workflow. Timers continue during execution. Packaged CLI agent → isolated worktree → instructions → independent check → complete diff review also passes with UI messages continuing.
- Native window captures cover Controller, Viewer, Encyclopedia, Settings, agent detail/profile, Tour, landing/manual Git screens and five palettes. Capture review corrected card text overlap and native dropdown selection rendering.

Tests use disposable local repositories and fake API keys/providers. No paid provider, real GitHub push/PR creation, private compiler, Nintendo assets or game ROM was used. Port/compiler/real-game matching remains dependent on user-owned inputs. Agent process success alone is not matching proof. Full pixel/interaction parity across every auxiliary Console screen is not established; see UI-PARITY.md and LIMITATIONS.md.

Build/test/release scripts and the short fleet guide are included in source. Third-party notices are embedded, so the application itself requires just one portable executable.

Final executable: 4,038,656 bytes (4.039 decimal MB). SHA-256: 306BA218D9ADD72AE963ED017520D42DFF5E9B5DB3ADA1AB3C1083CB63FCD203.

No owner keys, paid APIs, real external claim services or real GitHub remote writes were used. Live service interoperability still requires the user's own configuration; see BACKEND.md.
