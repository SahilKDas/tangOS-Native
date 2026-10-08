# Unfinished features and known limits

- Windows x64 only; no installer, updater or signing. MinGW and Rust GNU build required.
- Full Console screen/pixel parity remains unverified. Core Controller, Viewer, Encyclopedia, typed arguments, settings/vault, agent detail, provider profiles and tour are implemented. Remaining auxiliary screens/atlas interactions are listed in UI-PARITY.md.
- External Git, gh, Python, compilers and user-owned inputs are required by relevant operations. Untracked assets are not copied into agent worktrees. No game data is distributed.
- Local queues and CLAIMS.md are enforced. User-configured API/CLI leases coordinate an external claims service; MCP/external clients must follow the user's service setup. Queue adoption is explicit. See BACKEND.md.
- API drivers must expose a supported instruction hook or an explicit prompt argument. CLI commands must consume the instruction file. Provider compatibility depends on the repository driver. Real paid providers were not exercised.
- Independent checks run only when discoverable/available. No gate means unverified work, never proof of byte matching. Full asset/compiler-dependent SM64DS verification was not run with copyrighted inputs.
- GitHub readiness uses the user's gh setup with manual refresh. Real remote push/PR creation was not performed in this test run. Authentication uses existing Git/gh credentials.
- Conflict resolution, stash, rebase continuation/abort, tags, force operations and arbitrary branch/worktree management use external Git. Agent worktree creation is native fleet functionality.
- Native controls/dialogs and screen composition differ from Console. Animated backgrounds, source inspection, minimap, marquee and module popouts are implemented; full side-by-side pixel/interaction parity remains unverified. Published atlas updates are opt-in through user-enabled connections. Arbitrary DPI configurations require more testing.
- Global INI check settings apply to the selected repository. Logs/worktrees are retained manually. UI output retains a bounded tail; complete logs remain on disk.
- Executable repository scripts are trusted code, not OS-sandboxed. Instruction delivery and post-run audits cannot prevent arbitrary code from touching other paths or networking. See SECURITY.md.

Backend services and user-owned connection configuration are documented in BACKEND.md. Frontend parity remains a separate task.
