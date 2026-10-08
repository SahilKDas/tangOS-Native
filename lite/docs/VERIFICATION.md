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

## 0.5.0 parity checkpoint

Source checkpoint: 88603fe in the original main checkout. Two clean release builds produced identical SHA-256 FA5987D8FB112A418C8F51A6D43104156BF937701A8DA7616AA8C0DFB15634C5. Portable executable: 4,289,536 bytes (4.29 decimal MB), Windows system DLL imports only.

All four native test suites passed, together with packaged loopback backend tests, instruction-adapter tests, 320 original-code policy/history comparisons and 29 incremental statistics comparisons. Additional preflight comparisons cover 15 Python/package/compiler/ROM-directory/Atlas availability results against the reference. Packaged native source inspection, module-popout cart relay, Connections/Services/Requirements/queue renders and repository → status → checks → complete logs passed. Complete logs append across repeated runs; queued targets added during a batch survive completion. Background frames move and remain deterministic for the same phase. Native screenshots were inspected to correct label backgrounds and multiline/clipped text.

This checkpoint does not establish absolute full parity. Its contract inventory remains explicit, and the original TS/CSS reference is retained for further comparisons. External provider/GitHub write interoperability and copyrighted-input-dependent checks remain untested with real services/data.

## 0.6.0 descriptor/help checkpoint

Source checkpoint: a3b8ee2. Portable executable: 5,053,952 bytes (5.054 decimal MB), SHA-256 758854D7014FB1FAD745330B982CE1D58C5FF97E48C0D57C5E7800449AE0ED9E. Two independent clean builds were identical and import Windows system DLLs only.

All four native suites passed. Packaged loopback and instruction-adapter tests, all 388 policy/history/statistics/preflight/help comparisons, and three packaged GUI workflows passed: normal repository/check/log/fleet/Viewer/help workflow plus missing-descriptor and invalid-descriptor recovery. The latter verify scan, editable generation, side-effect-free preview, fixture-only confirmation and validated generated tools. Native tour expression captures and tips navigation were inspected; tour completion persists. Statistics refresh uses an independent worker and retains mandatory independent-proof labeling.

No real API key, paid provider, GitHub write, proprietary compiler or Nintendo data was used. Full parity remains unfinished, as the inventory explicitly reports.

## Atlas geometry/color checkpoint

Native Atlas geometry matches 24 original buildWorld cases covering all four layouts, both orientations, stable ties, nested padding and draft/exempt grouping. Tile colors match 44 original fnColor cases covering status/author modes, aliases, draft visibility and exemptions. Contributor colors honor canonical aliases, career totals and case-insensitive shared palette keys; palette work is cached with layout rather than repeated per frame. No-match hatching and claim tint are now distinct. Four native suites and the packaged GUI repository/status/check/log workflow passed using lite/out/TangOSLite.exe. These changes are not yet a new reproducibly packaged release.

## 0.7.0 Viewer checkpoint

Source checkpoint f6e7384. Two clean builds produced identical SHA-256 94A9052F7FABEEE8DEBA3844BC7C1A48B84C778EC05234903435785AE8D791A9. Executable size: 5,158,400 bytes (5.158 decimal MB), Windows system DLL imports only. All four native suites, packaged backend/adapter tests and 480 reference comparisons passed. Three packaged GUI workflows passed, including independent Viewer coloring, contributor selection and persistent draft/color choices. Full parity remains unfinished.

## 0.8.0 MCP checkpoint

Source checkpoint f668c5f. Executable: 5,186,048 bytes (5.186 decimal MB), SHA-256 74B160CED50FC2B5902B3141AB6C62CA2DEA30F94C5E37AD3928E9321E9093C6. Two clean builds were identical with Windows system DLL imports only. Four native suites, all 480 reference comparisons, packaged backend/adapter checks and three packaged GUI workflows passed. MCP integration verifies session deletion, rejected reuse, traffic telemetry and preservation of an unrelated running CLI job. GUI assertions verify start/stop/restart and persisted server preference. Null/false claims remain selectable. Full parity is still not established.

## 0.9.0 batch workflow checkpoint

Executable: 5,269,504 bytes (5.27 decimal MB), SHA-256 CBD933FA6C2E6D986AF2B19CC8D60EE5CBC1178F980F8BD44BBB4B6FA76CE5C7. Two clean builds were identical with Windows system DLL imports only. All four native suites, packaged backend/adapter tests, 520 original-code comparisons and three packaged GUI workflows passed. Tests cover actual driver delivery of saved batch instructions, refusal to edit an active batch, project draft persistence, legacy queue migration, partial completion and bounded completed history. Clearing history preserves complete logs; processed targets do not become byte-match claims. Full parity remains unfinished.

## 0.10.0 source and instruction checkpoint

Source checkpoint aae51d2. Executable: 5,290,496 bytes (5.290 decimal MB), SHA-256 AEB896C23D0D614245209EBE589F834229BBC748D99C4D7366F8460EDB16D723. Two clean builds were identical with Windows system DLL imports only. All four native suites, packaged backend/adapter checks, 540 original-code comparisons and three packaged GUI workflows passed. New comparisons cover the 400-line source response, CRLF/Unicode and disassembly fallback. Regression tests cover normalized local exclusions, embedded NULs, alternate streams/device paths, local AGENTS instruction delivery and duplicate agent names. Full parity remains unfinished.

## 0.11.0 execution loop checkpoint

Source checkpoint 06cf95f. Executable: 5,305,856 bytes (5.306 decimal MB), SHA-256 A40A73CF0CCECCE6FCF2026A69028DBFE6BF0438B15B9662402A53A62235A129. Two clean builds were identical with Windows system DLL imports only. All four native suites, packaged backend/adapter checks, 738 original-code comparisons and three packaged GUI workflows passed. Fleet integration covers immediate streamed exhaustion, five fast-empty runs, manual restart, split numeric boundaries, one-shot queue draining and separate per-run retained files. Summary ingestion tests cover actual results[]/sources/token aliases and transcription exclusion. Full parity remains unfinished.

## 0.12.0 Controller and project selection checkpoint

Two clean release builds produced identical Windows x64 executables: **5,323,264 bytes (5.323 decimal MB)**, SHA256 **9ED7851ED2FF53B6A7D2E4CA17EB1FD5D3DA7BE42AE745313FD6493ED8AB5203**. Only Windows system DLLs are imported. All four native suites, adapter/backend tests, 738 actual-original-code comparisons, and all three packaged GUI workflows passed. The GUI exercises four-agent Simple/Advanced control containment and remembered canonical repository/title registration. Dense-detail mascot downsampling has a raster regression. The development-only original Controller reference rendered without errors. Full parity remains open as listed in UI-PARITY.md and LIMITATIONS.md.

## 0.13.0 source inspection and clone cancellation checkpoint

Release source checkpoint: db3c9a9. Two clean Windows x64 builds are byte-identical: **5,351,936 bytes (5.352 decimal MB)**, SHA256 **DB49E74F536250C9E68088CCE483024FE3B367A5931EF453B0081FD021522681**. Windows system DLLs only. All four native suites, driver/backend tests, **762 original-code comparisons**, and three packaged GUI workflows passed. The packaged regression reproduces rapid function-list selection followed by source inspection and verifies the final target's numbered source, fixing dropped requests. Viewer list selection/sort persistence/control separation and native service Cancel routing also passed. Full parity remains open.

## 0.14.0 partial driver ledger checkpoint

Release source checkpoint: 8d9af41. Two clean Windows x64 builds are identical: **5,358,592 bytes (5.359 decimal MB)**, SHA256 **68E666333B3674FAE947F516EB96C8DC359A3F94FF6E3DF149E6D39364FD1FC9**. System DLL imports only. All four native suites, driver/backend tests, **810 actual-original-code comparisons**, and three packaged GUI workflows passed. Partial driver ledgers preserve unattempted queued targets, successfully drain later attempts, park zero-progress ledgers, and survive restart. Token aliases and null/per-landed fallback have 48 direct comparisons with the retained original ingestion block. Full parity remains open.
