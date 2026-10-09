# Unfinished features and known limits

- Windows x64 only; no installer or publisher signature. Portable updates require an explicitly trusted publisher profile and published SHA256. MinGW and Rust GNU build required.
- Full Console screen/pixel parity remains unverified. Core Controller, Viewer, Encyclopedia, typed arguments, settings/vault, agent detail, provider profiles and tour are implemented. Remaining auxiliary screens/atlas interactions are listed in UI-PARITY.md.
- External Git, gh, Python, compilers and user-owned inputs are required by relevant operations. Untracked assets are not copied into agent worktrees. No game data is distributed.
- Local queues and CLAIMS.md are enforced. User-configured API/CLI leases coordinate an external claims service; MCP/external clients must follow the user's service setup. Queue adoption is explicit. See BACKEND.md.
- API drivers must expose a supported instruction hook or an explicit prompt argument. CLI commands must consume the instruction file. Provider compatibility depends on the repository driver. Real paid providers were not exercised.
  Actual installed Ollama `llama3.2:3b` inference was exercised through native Fleet with instruction delivery, completion, independent fixture checks, retained logs and unchanged source. This covers a local OpenAI-compatible provider, not every hosted dialect or repository driver.
- Independent checks run only when discoverable/available. No gate means unverified work, never proof of byte matching. Real checkout validation is recorded in VERIFICATION.md; existing reference/declaration baselines are not proof of complete byte matching.
- GitHub readiness uses the user's gh setup with manual refresh. Real remote push/PR creation was not performed in this test run. Authentication uses existing Git/gh credentials.
- Branches, selected-path stash, tags, rebase/merge continuation and abort have native controls over external Git. Conflict editing remains external. Force pushes and destructive unreviewed operations remain disabled.
- Native controls/dialogs and screen composition differ from Console. Animated backgrounds, source inspection, minimap, marquee and module popouts are implemented; full side-by-side pixel/interaction parity remains unverified. Published atlas updates are opt-in through user-enabled connections. Arbitrary DPI configurations require more testing.
  Layout tests exercised attached 125% and 100% monitors and three requested window sizes (including minimum-size clamping), plus a 25,000-function database. Rendering still uses Windows DPI virtualization; sharp per-monitor raster output and other scale factors remain unverified.
- Global INI check settings apply to the selected repository. Logs/worktrees are retained manually. UI output retains a bounded tail; complete logs remain on disk.
- Executable repository scripts are trusted code, not OS-sandboxed. Instruction delivery and post-run audits cannot prevent arbitrary code from touching other paths or networking. See SECURITY.md.

Backend services and user-owned connection configuration are documented in BACKEND.md. Frontend parity remains a separate task.

- Remote registry discovery, descriptor downloading with offline cache, safe ZIP import and clone-to-project switching are implemented. Discovery and network downloads are opt-in. Published database availability depends on the project endpoint or the user's enabled `atlas.live` connection. Remote source/disassembly and work assignment require a checkout. Full Console parity remains incomplete.
- The official MCP Inspector CLI has exercised the native stdio bridge. Request cancellation and concurrent stdio input are tested, including cross-session isolation and EOF cleanup. Modern 2026-07-28 discovery and per-request metadata now have HTTP and stdio integration coverage. This does not establish interoperability with every desktop client or paid provider, or every optional MCP capability.
