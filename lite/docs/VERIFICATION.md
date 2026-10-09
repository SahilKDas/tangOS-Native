# Windows release verification — 2026-10-09

## Post-0.23.0 source checkpoint

The native floating helper, root-level guided tour, eased progress, active
configuration deferral and stopping controls passed all five CTest suites
(236.85 seconds). The expanded native GUI workflow exited successfully with
14 responsive timer ticks, retained check logs, centered/spotlight tour captures,
floating-tip captures and the display/theme matrix. Evidence is under
`lite/out/gui-tour-final-024`. Development-only original tour captures reported
no page errors. These source changes have not yet replaced the verified 0.23.0
portable package below.

## 0.23.0 verified portable package — 2026-10-09

Release source checkpoint **707b7a7** passed two clean Windows x64 builds with
identical bytes: **6,613,504 bytes (6.614 decimal MB)**, SHA256
**DEBE289AB8E67F07789AC11D61D8DB7B5DF1AB7F923C3C8583E24D078B1BFC50**.
Imports are Windows system DLLs only. All five native suites passed with the
official MCP Inspector enabled, alongside packaged backend, driver adapter,
disposable Git/ZIP workflows and **12,262 original Console comparisons**.

All three packaged GUI workflows passed: valid, missing and invalid project
descriptors. Each exercised repository selection, status, check execution,
actionable diagnostics, complete retained logs and 12 responsive timer ticks.
The valid project also exercised the complete reference screen, large database
and display matrix. The verified portable artifact is `lite/release/TangOSLite.exe`;
build evidence is retained in `lite/release-0.23.0-verified`.

Actual provider, GitHub, display and ROM/check results are detailed below.
The existing SM64DS dead-reference and byte-matching failures remain failures.
Hosted providers, remote GitHub writes and exact full UI/protocol parity remain
unverified. No credentials, ROMs or extracted assets are included in the package.

## 0.23.0 workflow and live-validation source checkpoint — 2026-10-09

Native generation regression tests cover saved edits during scheduling and new
global reservations before a generated draft is saved. Reviewed disposable Git
tests cover working/staged differences, history and unstaging without discarding
working content, plus branches, tags, stash and merge/rebase conflicts. ZIP
import safety tests pass. Native Help Tips and complete reference navigation
pass; display validation checks nine layouts with requested window sizes
980×720, 1475×1025 and 1770×1230 (the first is clamped to Lite's 1100×760 minimum)
and captures both attached monitors (125% and 100%, including negative origins).
The application remains DPI-virtualized; sharp per-monitor rendering is not
established. The 25,000-function Viewer retained a 500-row roster; fixture
creation/layout/capture took 4,156 ms in this run, not a sustained FPS benchmark.

Official MCP Inspector 2.10.1 exercised the real native stdio bridge. An actual
installed Ollama `llama3.2:3b` model returned a nonempty OpenAI-compatible message
through a native Fleet API agent in a disposable Git repository. Instructions,
retained driver log, independent fixture gate, completion and unchanged tracked
source were verified. No model was downloaded, no key was used and no byte
matching claim is made for this transport test. Hosted/paid providers remain
unverified because no user connection is configured.

Read-only GitHub readiness against tangosdev/tangOS PR 16 returned merged metadata.
The PR has no reported checks; this is not a green-check result. Real remote
writes/PR creation remain unverified. A second actual read-only check against
open cli/cli PR 14640 returned real readiness metadata and mixed successful,
failed and pending checks; the native checks command returned a failing status
and preserved its full log. No mutation was made to either project.
Actual 64DS-DX gates: port references passed
8,868 checks; declaration agreement passed against 12,228 existing disagreements;
dead references failed on 561 prose/comment references; link checks skipped an
empty changed-source range; byte matching failed the existing strict stock-ROM
bootstrap control (current SHA256 does not equal its admitted proof). The
checkout's tracked source remained unchanged. No baseline was changed or waived.
Native hashing of the existing 16-MiB baserom agrees with independent Windows
SHA256 and rejects a deliberately different expected digest. This verifies
real-file hashing behavior, not the ROM's retail identity; the failing bootstrap
comparison concerns the checkout's built control ROM and remains a separate failure.

An initial cancellation test used a fixed 500-ms delay and sometimes cancelled
before Python initialized. It now waits for an actual descendant, verifies
termination and retained logs, and passes. An initial help test leaked Tips mode
into the tour fixture; mode isolation is corrected. An actual-provider fixture
initially compared LF data with a Git CRLF checkout; its disposable checkout now
pins LF and additionally checks the tracked-source diff. Release packaging is a
separate gate; this entry alone does not establish a verified portable package.

## 0.21.0 verified Advanced Controller package — 2026-10-08

Source checkpoint **b6c0ebf** passed two clean Windows x64 builds with identical
bytes: **6,525,440 bytes (6.525 decimal MB)**, SHA256
**885356F9E8D2FB8284F353613E5E6CE6C29C826B56F19BC86A5CDD766063FDEA**.
All five native suites, disposable backend/Git/ZIP workflows, 12,124 original
source comparisons and all three packaged GUI workflows passed. Official MCP
Inspector was enabled. Advanced Controller tests cover compact count geometry,
disabled empty Drive, additive/removable roles, effort persistence, attempts
clamping, continuous count guarding, header containment and passive queue edits.
A controlled CLI driver confirms waiting-only removal/reordering/clearing keeps
the in-flight target and active batch history, then completes without resurrecting
cleared work. Equal-rate recommendations retain original insertion order.
The package is `lite/release-0.21.0-final/TangOSLite.exe`, with Windows system
DLL imports only. Full parity remains incomplete: native widget decoration,
live telemetry composition, animations, further overlays and unusual DPI still
need work; paid providers and real GitHub writes remain unverified. Subsequent
Controller view-model source changes are not included in this package.

## 0.20.0 verified native report package — 2026-10-08

Source checkpoint **03c85a2** passed two clean Windows x64 builds with identical
bytes: **6,411,776 bytes (6.412 decimal MB)**, SHA256
**8FF45749DA1CD1C8F05A79822D9460174231E2EEE677662534EBD9EDFFEAF431**.
All five native suites, disposable backend/Git/ZIP workflows, 12,100 original
source comparisons and all three packaged GUI workflows passed. Official MCP
Inspector was enabled. The launched report modal checks its empty-description
guard, measured field geometry, asynchronous screenshot export and owner
restoration without modifying the test machine's clipboard or launching Explorer.
Backend tests cover changed-attachment rejection, secret redaction and native
4K clipboard-to-PNG compression. The real module window passes initial and
resized containment checks. The packaged 25,000-function Viewer rendered in
2,187 ms with a capped 500-row roster.
The package is `lite/release-0.20.0-final/TangOSLite.exe`, with Windows system
DLL imports only. Full parity remains incomplete. An expanded reference audit
found equal-rate recommendation ordering differs from JavaScript; the subsequent
source correction is not included in this package. Exact Advanced composition,
arbitrary DPI, paid providers and real GitHub writes remain unverified.

## 0.19.0 verified Controller package — 2026-10-08

Source checkpoint **afad693** passed two clean Windows x64 builds with identical
bytes: **6,299,136 bytes (6.299 decimal MB)**, SHA256
**71E8518B3140B66EEEAFA0897FB001AE74BDCCBEF53953919AB8E322600F1D15**.
All five native suites, disposable backend/Git/ZIP workflows, 12,100 original
source comparisons and all three packaged GUI workflows passed. Official MCP
Inspector was enabled. Controller tests cover reference control geometry,
remembered Writes state, badge text surviving composition, and Simple-mode cart
assignment without provider execution. The cross-agent policy/statistics lock
collision is covered by a deterministic regression.
The package is `lite/release-0.19.0-final/TangOSLite.exe`, with Windows system
DLL imports only. Full UI, arbitrary DPI, paid-provider and GitHub-write parity
remain unverified. Further source edits after this checkpoint are not included.

## 0.18.1 verified native snapshot package — 2026-10-08

Source checkpoint **2dbdf07** passed two clean Windows x64 builds with identical
bytes: **6,278,656 bytes (6.279 decimal MB)**, SHA256
**25A2F1C82F51C11A788416CFE1060E89B86AD8B3400637BBB6249F34369439AC**.
All five native suites, disposable backend/Git/ZIP workflows, 12,100 original
source comparisons and all three packaged GUI workflows passed. Official MCP
Inspector was enabled. Native snapshots produce an actual window bitmap and
valid state/layout JSON without network requests. Closing Settings from its
focused numeric field and reopening without a stale confirmation are covered;
confirmed statistics clearing resets lifetime, session and best-divergence data.
The package is `lite/release-0.18.1-final/TangOSLite.exe`, with Windows system
DLL imports only. Full UI, arbitrary DPI, paid-provider and GitHub-write parity
remain unverified. No such verification is implied by this release.

## 0.18.0 verified portable package — 2026-10-08

Source checkpoint **f039e50** passed two clean Windows x64 builds with identical
bytes: **6,251,520 bytes (6.252 decimal MB)**, SHA256
**AB60FF4BFBF01AC7D8B66C5979A352E44B055A2F126EB22BC40D43E66C5D7EC6**.
The package is `lite/release-0.18.0-final/TangOSLite.exe`. Imports are Windows
system DLLs only. All five native suites, disposable backend/Git/ZIP workflows,
**12,100 original-source comparisons**, and all three packaged GUI workflows
passed. Official MCP Inspector was enabled during the Fleet suite. Installed
VS Code accepted registration in a disposable profile; actual authenticated
tool execution inside VS Code remains unverified.

The native Settings overlay preserves Controller, scrolls, expands explanations,
persists delegation changes, guards statistics clearing and releases its windows.
Help opens Support rather than the agent operations menu. The 25,000-function,
500-row roster fixture took **1,625 ms** for layout and capture. This is neither
arbitrary-DPI coverage nor a sustained frame-rate benchmark. Exact footer,
snapshot/sync overlays and remaining screen interactions still need parity work.
No paid providers, personal client configuration changes or GitHub writes were
used. Existing SM64DS validation failures recorded below remain failures.

## 0.17.0 source validation — 2026-10-08

Release source checkpoint **b4c059d** passed two clean Windows x64 builds with
identical executable bytes: **6,177,792 bytes (6.178 decimal MB)**, SHA256
**347D5651A24DA65DE2EDD3814264062729BB7EEE791F6464196AB16C8FD56157**.
The verified package is `lite/release-0.17.0-final/TangOSLite.exe`; imports are
Windows system DLLs only. All five native suites, harmless backend/Git/ZIP
integration tests, original-code comparisons and all three packaged window
workflows passed. This includes 7,208 automatic-role cases and 2,600 provider
effort cases. The installed official MCP Inspector was enabled for the Fleet
suite. No personal client settings, credentials or paid providers were used.

An earlier 0.17.0 packaging attempt failed on simultaneous Git worktree
registration. It was not promoted. The final build serializes internal
worktree metadata operations, with eight concurrent disposable registrations
and cancellation behind a controlled checkout hook tested successfully.

Installed VS Code 1.140.0 accepted the native stdio server definition through
its own CLI in a disposable user profile. This establishes registration only;
authenticated server/tool execution inside VS Code remains unverified.

The actual native window workflow in `lite/out/gui-viewport-modal-017` passes
repository selection, status, checks, complete logs, Controller model streams,
recent-run expansion and statistics scope, remote clone/viewer, report export
and source inspection. Controller, Viewer and detail controls stay within
1100×650, 1180×754 and 1600×900 logical viewports. This is resize coverage,
not proof of physical multi-monitor DPI behavior. The 25,000-function fixture
rendered in 1,265 ms with a 500-row roster.

There are 111 direct original activity/detail/role comparisons and 32 original
statistics comparisons. Modern HTTP and stdio MCP discovery/metadata and the
official Inspector pass. Disposable client configuration tests include atomic
backups, stale preview rejection, malformed input refusal and VS Code JSONC.
Split credentials are redacted from disk, returned output and nested activity
arguments. Fresh packaged-build evidence is recorded below when it completes.

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

## 0.15.0 remote projects / viewer-only checkpoint — 2026-10-07

Source checkpoint: `87904e1` (feature checkpoint `17191fd`). Two independent clean builds in `lite/release-0.15.0-verified` produced identical portable executables: **5,400,576 bytes (5.401 MB)**, SHA-256 `84910C7556158073EF9933E7EB705FAC67BA9F6B705EE055C7EC75F0DC1CA5F6`. Imports are Windows system DLLs only.

All four native suites passed, plus the driver adapter, packaged backend connection tests and 810 actual-original-code comparisons. All three packaged native GUI workflows passed: valid descriptor with fleet/Viewer/remote project, missing descriptor and invalid descriptor. The remote workflow imports a validated registry descriptor, opens a metadata-only project without Git/Fleet/MCP or automatic requests, explicitly loads published data through an injected read-only transport, captures both native screens, and checks disabled assignment and the Space shortcut. Disposable backend tests cover stable cache reopening, local versus remote project paths, invalid descriptors, no network during opening and configuration-injection rejection. INI tests cover remembered Unicode remote project IDs.

The first packaging attempt revealed test sequencing that ran the remote fixture after descriptor recovery with an uninitialized UI descriptor. The test now keeps those scenarios separate; the final package above passed all workflows. Real external project endpoints and owner API credentials were not used. Full Console parity remains incomplete; see LIMITATIONS.md. Work stopped for today after this scope.

## 0.16.0 source and real-integration checkpoint — 2026-10-08

Source checkpoint `79f3590` adds portable updates and explicit cross-repository
GitHub PR selection. Five native suites passed, including actual replacement of
disposable executable copies, rollback backup preservation, changed-target
rejection, checksum/header/trust/redirect guards and receipt confinement. The
official MCP Inspector 2.10.1 CLI exercised the native stdio bridge. Presence has
48 additional direct comparisons with the retained original implementation.
The native GUI launched and passed repository/status/check/log/responsiveness,
clone, batches, source inspection, remote Viewer and cached-data workflows.

Read-only real GitHub validation used the existing local `gh` login against
`tangosdev/tangOS` PR 16. Native readiness returned the merged PR's metadata.
The checks command returned “no checks reported”; this is not a green checks
claim. No remote write, PR creation, merge or push occurred.

Actual `64DS-DX` checkout checks ran through the native backend and retained
complete logs under `lite/out/live-sm64ds-*`. Port reference checks passed.
Declaration agreement passed its existing baseline, with 12,228 existing
disagreements; it does not establish universal agreement. Dead-reference checks
failed on 561 new prose/comment references. Link checks skipped an empty changed
source range, so they establish no broader relocation result.

For byte matching, an isolated Python environment installed the checkout's pinned
requirements and ndspy 4.2.0. The existing local ROM was unpacked only under the
checkout's ignored `extracted/` tree, using both its unpack script and documented
`dsd rom extract` layout. The compiler/link pipeline then failed the existing
strict stock/TU baseline control: symbol checks failed and its bootstrap proof
did not agree with the generated baseline. The outer `--no-rom` command also
invokes a nested baseline builder that produces an ignored local ROM; none of
that data is committed or packaged. The selected checkout's tracked source
remained unchanged. Byte matching is failed, not verified. These failures are
recorded without changing baselines, waiving checks or repairing `src/`.

Remaining screen parity is still pending.

## 0.16.0 verified portable package

Release source checkpoint: `13e1475`. Two clean builds in
`lite/release-0.16.0-verified` produced identical Windows x64 executables:
**5,764,096 bytes (5.764 decimal MB)**, SHA256
`03102C14C1D7D2FF6D31CC1DDEA95BE8B586E87ABD0F5A79966465ED895D21F8`.
Imports are Windows system DLLs only. All five native suites, packaged backend,
driver adapter, disposable Git/ZIP workflows and **858 original-code comparisons**
passed. Official MCP Inspector ran during the Fleet suite.

All three packaged GUI workflows passed: valid, missing and invalid descriptors.
The valid workflow also covered Viewer full-map toggle/F11 restore and a
25,000-function database with a 500-row roster (3,250 ms for fixture creation and
capture/layout combined on this machine). This is not a sustained frame-rate or
arbitrary-DPI benchmark. The default artifact is `lite/release/TangOSLite.exe`.
Full UI/protocol parity remains unverified; real SM64DS failures above remain
failures, and no real provider calls or GitHub writes were made.

## 0.16.1 verified portable package

Release source checkpoint: `7760d02` (cancellation checkpoint `71eba2b`).
Two clean builds in `lite/release-0.16.1-final` produced identical Windows x64
executables: **5,797,376 bytes (5.797 decimal MB)**, SHA256
`90768FBF666D4002789AB6DFC602659035DE0D1E7471F2671B6FD2C5EE68E89E`.
Imports are Windows system DLLs only. All five native suites, packaged backend,
driver adapter, disposable Git/ZIP workflows and 858 original-code comparisons
passed. Official MCP Inspector ran in the Fleet suite. All three packaged GUI
workflows passed, including the real edit-control log scroll/selection and tail
follow regression check.

Additional coverage includes HTTP and stdio request cancellation, typed request
IDs, cross-session isolation, UI Stop error responses, EOF/malformed-input
cleanup, SSH GitHub descriptor URLs and deliberate transient Windows state-file
locks. The first packaging attempt encountered an undiagnosed fleet state
replacement failure; it was not promoted. State saves now retry transient lock
errors briefly and include the path and Windows error code on persistent failure.
The corrected package passed the complete release process.

The default artifact is `lite/release/TangOSLite.exe`. Exact UI composition,
newest stateless MCP and broader third-party/provider validation remain pending.
The real SM64DS failures recorded above remain failures. No credentials, ROMs or
extracted assets are included; no real provider calls or GitHub writes were made.
