# Unfinished features and known limits

- Windows x64 only. No macOS/Linux UI, ARM64 package, installer, updater or signing.
- Console's Aero shell is reproduced natively, but full pixel/interaction parity
  across all Console screens is unfinished. See UI-PARITY.md; atlas, provider/fleet
  cards, tours, full settings/key vault and full descriptor encyclopedia/run-dock
  screens remain. A compact manual Encyclopedia is implemented.
- Uses external Git, gh, Python and build tools. This is a single app executable,
  not a self-contained decomp/Git toolchain. The supported application build uses MinGW and Rust GNU.
- Known SM64DS check adapters plus local argv overrides; full `tangos.json`
  schema parsing, argument editors and automatic descriptor migration remain.
- Settings and command overrides are global for the selected repo, not a profile
  per repository. Settings changes reload on restart. Very long paths depend on
  external-tool support. Command arguments containing literal quotes cannot be
  serialized into INI check overrides.
- Root/nested tracked AGENTS.md and root untracked AGENTS.md are exported with the
  coordination entry guide. Role/runbook links must be read by the agent. Provider
  launching, live fleet claims, queue adoption, ownership locks and automated
  independent verification are not implemented. Existing worktree coordination
  is inspected, not replaced.
- GitHub checks/readiness are displayed as gh text/JSON, with manual refresh;
  no polling, check rerun, PR merge or browser authentication UI. Cross-fork PR
  creation uses the owner of origin; non-origin push forks need external gh.
- New remote branch creation, checkout, worktree creation/deletion, conflict
  resolution, stash, rebase continuation/abort, tags and force operations use
  external Git. Push requires an existing fetched destination.
- Diagnostics preserve file/line/commands from tools, with a general rerun hint.
  No clickable editor navigation, compiler-specific parser or automated repair.
- One running operation at a time. Complete output is on disk; UI retains a tail.
  Exception/process-launch failures are shown in UI; a launch failure can leave
  a header-only log. Retention/rotation is manual.
- ROM identity requires a separately trusted digest configured locally. Full
  decomp byte matching, port builds, smoke, declaration and link correctness depend
  on the repository's inputs/tools; no game or private compiler is distributed.
- Live network GitHub PR creation/push was not exercised against a real remote.
  Port/compiler/ROM-dependent gates were not run with copyrighted inputs. See
  the release verification report for the workflows actually tested.
- External programs/hooks and concurrent agents are outside the safety boundary;
  see SECURITY.md. The app cannot promise to identify every proprietary asset or
  credential encoding, or police edits made outside it.

The renderer is TinySkia CPU rasterization with Win32 Nunito text/input controls.
The supported release toolchain is MinGW plus Rust GNU; MSVC is not currently supported.
Full Console screen parity and arbitrary DPI/layout configurations still require further testing.
