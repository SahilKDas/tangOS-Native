# TangOS Lite 0.16.1

Click Tango or use **Alt+Space → About** for embedded third-party notices.

A portable Windows 10/11 x64 C++ desktop workbench. Run `TangOSLite.exe`; no
installer, browser runtime, administrator privileges, or ROM data is required.
The native Aero UI follows Console's branding, gradients, glass panels, header,
requirements rail, themes and Tango artwork. See [visual parity](docs/UI-PARITY.md)
for the reference and remaining differences.
Git is required for repository operations. Python 3, GitHub CLI (`gh`), MSVC x86,
CMake, Ninja, and the decomp's pinned compiler are required only by features
that use them. The executable does not bundle those tools.

## Quick start

1. Browse to an SM64DS working tree, or paste its path and click **Select**.
   The root is detected even when selecting a subdirectory or Git worktree.
   The path is remembered in `%LOCALAPPDATA%/TangOSLite/settings.ini`.
   **Chaos Controller** runs isolated AI agents; **Chaos Viewer** displays the atlas.
   **Git & reviews** opens repository status, checks and Git/GitHub controls.
   See [fleet guide](docs/FLEET-GUIDE.md) for API, CLI and MCP execution.
2. Status shows the branch, changed files, conflicts, all branches, remotes,
   and worktrees. Use **Refresh** after Git changes.
3. Open **Git & reviews**, select a check and click **Run check**, or run a declared tool from **Encyclopedia**. Review and approve the exact command.
   Missing scripts/build outputs are marked unavailable. Repository scripts are
   executable code: only run a repository you trust.
4. Inspect live output, including the original file names, line numbers, and
   suggested commands from the tool. **Cancel** kills the whole process tree.
   **Open logs** opens complete, continuously flushed logs. The UI retains a
   bounded tail; the disk log retains all output, including cancellation output.

## Checks and ROM identity

Built-ins discover `tools/port_refcheck.py`, `tools/check_decl_agreement.py`,
`tools/check_dead_references.py`, `tools/prepush_linkcheck.py`,
`tools/rombuild.py --no-rom`, and `tools/romdata_check.py`. The data comparison is
not an identity check. Port build/smoke delegates to `port/build-port.cmd`; smoke
reruns use `ctest --test-dir build/port --output-on-failure`.

**Settings file** opens the small local INI. Restart to load edits. To verify a
local ROM's identity, set `rom_path` and `rom_sha256` using an independently trusted
digest for the correct game/version. The native verifier streams the file through
Windows SHA-256, displays only the digest, and creates no ROM copy or extraction.
There is deliberately no bundled hash database or ROM download.

An example configuration (replace example paths and digest):

```ini
[settings]
repository=C:\work\sm64ds-decomp
python=python
port_only=true
rom_path=C:\private\my-cartridge.nds
rom_sha256=
exclusions=local-assets;private;extracted;baserom;roms;assets;nintendo

[checks]
Link checks="python" "tools/prepush_linkcheck.py" "--range" "tango/main..HEAD"
```

Overrides are explicit argv tokens, without implicit shell expansion. Quoted
tokens group spaces; backslashes are literal. Check names replace built-ins or
add new checks. The Encyclopedia discovers `tangos.json` tools and provides typed argument editing.
Settings apply to the selected repository; review overrides when changing repos.

## Git and upstreams

Use **Git / GitHub**, then **Execute**. The two fields are interpreted by action:

| Action | Remote / first ref | Branch / second ref | Details |
|---|---|---|---|
| Fetch | ignored (all configured remotes) | ignored | ignored |
| Pull | current branch's tracking remote | current tracking branch | ignored |
| Merge / rebase | ignored | e.g. `tango/main` | ignored |
| Stage paths | ignored | ignored | exact relative path per line |
| Commit staged | ignored | ignored | commit message |
| Push reviewed | e.g. `origin` (your fork) | destination branch | ignored |
| Compare upstreams / upstream diff | e.g. `tango/main` | e.g. `scopic/main` | ignored |
| Add remote | chosen name, e.g. `tango`, `scopic`, `fork` | ignored | actual GitHub HTTPS/SSH URL |
| PR readiness / checks | inferred by gh from current branch | ignored | ignored |
| Create draft PR | target `owner/repo` | target base branch | PR title |

Use the actual Tango and SCOPIC64 repository URLs rather than a built-in guess.
Keep `origin` pointing to your fork for cross-repository draft PRs. Authenticate
with `gh auth login` externally; Lite never stores tokens. It reports GitHub's
review decision, mergeability, merge state, draft state, and check rollup; UNKNOWN
or pending states are not evidence of readiness. It does not merge PRs.

There is no automatic staging. Every commit gets a full staged diff review.
Every outgoing commit and patch is reviewed before pushing, and the snapshot is
rechecked after approval. Push requires a fetched, existing destination branch
so historical asset scans cannot be skipped for a new branch. Create an empty
destination branch on GitHub and fetch it before the first Lite push. Force pushes
are not offered. Fetch refreshes remote refs but does not modify the working tree.
Merge/rebase and fast-forward pull require a clean working tree and confirmation.

Port-only mode rejects `src/` in commit/push reviews and explicit staging.
It never repairs source files. To work on actual decomp source, changing
`port_only=false` is an explicit local policy change; follow AGENTS.md and obtain
byte/link proof yourself. Resolve merge conflicts and rebase continuation using
Git outside Lite. Merge/rebase can legitimately update upstream `src/`.

**Agent guide** exports repository instructions for external coordination. The native Controller additionally launches API/CLI agents and serves authenticated MCP clients with persistent queues and isolated worktrees. See [fleet guide](docs/FLEET-GUIDE.md).

## Build and test

Recommended verified toolchain: Windows x64, MSYS2 UCRT64 GCC 14.1.0, CMake,
MinGW Make, Python 3, and Git, on PATH. No npm packages are required. Rust GNU dependencies must be fetched once as described below.

```powershell
.\lite\scripts\build.ps1
.\lite\scripts\gui-smoke.ps1 -Executable .\lite\out\TangOSLite.exe
.\lite\scripts\release.ps1
```

Use fresh output/fixture directories, or pass new `-OutputDir` / `-FixtureDir`
paths. Release builds run two clean builds, compare SHA-256, run integration
tests, launch the packaged native window and exercise selection → status → check
→ log, enforce the 50,000,000-byte ceiling, and emit `SIZE.txt`. The final EXE is
at `lite/release/TangOSLite.exe`. Reproducibility means the same sources and
toolchain; different GCC versions need not produce identical bytes.

Unit tests cover argv quoting, configuration, status parsing, refs, native hash,
and safety rules. Integration tests use disposable repositories/remotes/worktrees
and cover staging, commit/push previews, ignored files, intermediate forbidden
commits, missing executables, logs, and descendant cancellation. GUI smoke tests
assert rendered status/log contents and timer responsiveness while a check runs.
No copyrighted data or real remote writes are used by tests.

The TinySkia release currently supports the MinGW GNU toolchain.

See [architecture](docs/ARCHITECTURE.md), [security review](docs/SECURITY.md),
[limitations](docs/LIMITATIONS.md), and the generated release size report.

## TinySkia and Nunito build requirements

Install Rust and `rustup target add x86_64-pc-windows-gnu` alongside the existing
MinGW toolchain. Run `cargo fetch --locked --manifest-path lite/renderer/Cargo.toml`
once before building; subsequent CMake builds are locked and offline. The executable
statically embeds TinySkia and Nunito. Neither needs installing on the user's PC.
TinySkia renders the window surfaces, cards, buttons and mascot; Windows handles
native text input, accessibility and Nunito text rasterization.
Repository selection and Git remote discovery work for other users' checkouts.
SM64DS-specific check adapters appear only when their scripts are found.

## Native backend services

See [backend guide](docs/BACKEND.md) for project registration, source/history, claims/leases, preferences, reports, recovery, and confirmed Git helpers. All external connections and credentials belong to the user. No owner API key or hosted service is required to build or test.

## Reference verification

Development tests additionally use Node 26 to run retained original Console policies as comparison oracles. Node, TypeScript and CSS are not included in TangOSLite.exe. `test_reference_parity.py` compares source classification, adaptive roles, pool difficulty and function history; `test_statistics_reference.py` compares incremental unique-function statistics. `audit_parity.py --require-full` refuses a full-parity claim while public contracts remain unverified.

Version 0.5 adds native source/history inspection, minimap/marquee/navigation/module windows, animated backgrounds, Connections/Requirements/Services screens, queue editing and retention, unique-function statistics and safer explicit sync backups. Full parity remains underway, as documented in UI-PARITY.md.

0.15.0 adds remembered remote projects and viewer-only published progress. Use the project menu to import a downloaded tangos.json. See docs/USER-GUIDE.md for your own connection setup. Verified portable size: 5,400,576 bytes (5.401 MB); two clean builds are byte-identical. Full Console parity remains incomplete.
