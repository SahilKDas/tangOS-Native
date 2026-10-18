# Unfinished features and known limits

- Windows x64 only; no installer or publisher signature. Portable updates require an explicitly trusted publisher profile and published SHA256. MinGW and Rust GNU build required.
- Full Console screen/pixel parity remains unverified. Core Controller, Viewer, Encyclopedia, typed arguments, settings/vault, agent detail, provider profiles and tour are implemented. Remaining auxiliary screens/atlas interactions are listed in UI-PARITY.md.
- External Git, gh, Python, compilers and user-owned inputs are required by relevant operations. Untracked assets are not copied into agent worktrees. No game data is distributed.
- Local queues and CLAIMS.md are enforced. User-configured API/CLI leases coordinate an external claims service; MCP/external clients must follow the user's service setup. Queue adoption is explicit. See BACKEND.md.
- API drivers must expose a supported instruction hook or an explicit prompt argument. CLI commands must consume the instruction file. Provider compatibility depends on the repository driver. Real paid providers were not exercised.
  Actual installed Ollama `llama3.2:3b` inference was exercised through native Fleet with instruction delivery, completion, independent fixture checks, retained logs and unchanged source. This covers a local OpenAI-compatible provider, not every hosted dialect or repository driver.
- Independent checks run only when discoverable/available. No gate means unverified work, never proof of byte matching. Real checkout validation is recorded in VERIFICATION.md; existing reference/declaration baselines are not proof of complete byte matching.
- GitHub readiness uses the user's gh setup with manual refresh. Native clone, reviewed commits/push (including first branch publication), draft PR creation and actual Actions checks passed in a fresh private synthetic repository on 2026-10-10. Authentication uses existing Git/gh credentials. This does not verify every fork permission/protection policy or hosted provider.
- Branches, selected-path stash, tags, rebase/merge continuation and abort have native controls over external Git. Conflict editing remains external. Force pushes and destructive unreviewed operations remain disabled.
- Native controls/dialogs and screen composition differ from Console. Animated backgrounds, source inspection, minimap, marquee and module popouts are implemented; full side-by-side pixel/interaction parity remains unverified. Published atlas updates are opt-in through user-enabled connections. Arbitrary DPI configurations require more testing.
  Layout tests exercised attached 125% and 100% monitors and three requested window sizes (including minimum-size clamping), plus a 25,000-function database. Per-monitor v2 physical-resolution output now has renderer and injected transition tests at 100–300%, with actual GUI validation at 125%. Real mixed-monitor transitions and other physical display configurations remain unverified.
- Global INI check settings apply to the selected repository. Logs/worktrees are retained manually. UI output retains a bounded tail; complete logs remain on disk.
- Executable repository scripts are trusted code, not OS-sandboxed. Instruction delivery and post-run audits cannot prevent arbitrary code from touching other paths or networking. See SECURITY.md.
- Descriptor Atlas generation is implemented with command review and protected output paths. Post-land refresh stays inside the agent worktree; it does not publish to the primary checkout. The native result includes exit/log/refreshed metadata, differing from the reference's direct database return. Exact background physics, CSS blur/shadows and animation cadence remain incomplete.

Backend services and user-owned connection configuration are documented in BACKEND.md. Frontend parity remains a separate task.

- Remote registry discovery, descriptor downloading with offline cache, safe ZIP import and clone-to-project switching are implemented. Discovery and network downloads are opt-in. Published database availability depends on the project endpoint or the user's enabled `atlas.live` connection. Remote source/disassembly and work assignment require a checkout. Full Console parity remains incomplete.
- The official MCP Inspector CLI has exercised the native stdio bridge. Request cancellation and concurrent stdio input are tested, including cross-session isolation and EOF cleanup. Modern 2026-07-28 discovery and per-request metadata now have HTTP and stdio integration coverage. This does not establish interoperability with every desktop client or paid provider, or every optional MCP capability.

## Five-journey scope — 0.38

The native recovery, MCP, batch/review, synchronization and manual-update journeys
have additional implementation and automated coverage. This does not certify all
133 Console public contracts or whole-screen pixel parity. New differential cases
cover original updater eligibility and packaged UI preference transitions; native
fault cases cover corruption, cancellation, interrupted work and update restart.
Physical mixed-monitor, unavailable hosted clients/providers and ROM/compiler
coverage remain separate unresolved validation. Conflict editing stays external.
The v0.38 development candidate includes the five journey implementations, but
final runtime validation is paused by explicit user instruction while C: is below
11 GiB free. A compiled deferred-control-rebuild fix still requires its GUI
regression run. Final preference comparisons, packaged journeys and reproducible
release builds are pending. Keep using the verified v0.37 portable package until
these gates pass; the candidate does not establish full parity.