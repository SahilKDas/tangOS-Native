# TangOS Lite quick guide

Run the portable `TangOSLite.exe` on Windows 10/11 x64. Install Git first; check
scripts additionally need Python or the repository's existing compiler/build
tools. No ROM, extracted asset or compiler is included.

1. **Choose repo folder** or enter a local path and select **Open repo**.
   Lite remembers the repository and detects Git worktrees.
2. Use **Repository** for branches, remotes, changed files and conflicts.
   **Refresh** updates the snapshot.
3. In **Chaos Controller**, open **Encyclopedia**, choose a check and click
   **Run check**. Review the command before approving repository code.
4. Watch **Live activity**. **Cancel** stops the process tree;
   **Open logs** gives you complete logs with file/line diagnostics and commands.
5. Use the Git dropdown for fetch, pull, merge, rebase, exact-path staging,
   commits, reviewed pushes, upstream comparisons, and GitHub PR operations.
   Put paths or a commit/PR title in Details, according to the selected action.
   Every commit and each outgoing patch require review. Destructive actions
   require confirmation; reset, clean and force push are not offered.
6. For fork PRs, point origin at your fork and enter the target `owner/repo` and
   base branch. Run `gh auth login` externally first. Push requires an existing
   fetched destination branch.
7. **Agent guide** exports the repository's instructions for coordinated work.
   It does not launch AI providers. Follow its ownership/handoff links.

The theme selector remembers Aero, Sunset, Deepsea, Bubblegum or Lemonlime.
**Settings file** opens the local INI; restart after editing Python, exclusions,
ROM path/digest or check commands. Port-only mode defaults on and rejects `src/`
in staging/commit/push. Keep all proprietary assets excluded.

Click Tango or choose **Alt+Space → About** for embedded notices. The full
README describes field meanings and check overrides; LIMITATIONS.md lists the
unfinished Console screens and fleet functionality.

## New native screens

Controller presence uses the reference timing: API profiles are available;
active work pulses green; an MCP signal stays green for five minutes, then
yellow until one hour, then red. The last signal remains visible after a session
disconnects for the lifetime of the window. CLI activity supplies local signals.

MCP client templates launch this same executable with `--mcp-stdio`; no Node
bridge is required at runtime. Legacy initialization negotiates 2024-11-05,
2025-03-26, 2025-06-18 or 2025-11-25. Newer clients can fall back from discovery
to that handshake. Official MCP Inspector 2.10.1 has been tested against a real
local server and native stdio bridge. Other client UI setup remains user-owned;
the Inspector and its dependencies are development tools and are not bundled.

In Chaos Viewer, left-drag pans, right-drag selects eligible functions, the wheel zooms and WASD/arrows travel. Space adds/removes the selection from the cart. Inspect source opens numbered source and prior tries; Pop out module opens an independent Viewer that sends draft additions back to the main cart. It does not start a second agent controller.

Settings provides matching, delegation, animation and live-refresh controls. Connections stores user-owned endpoint profiles with credentials referenced by local environment/vault names; nothing is enabled by default. Project services shows request results and exact mutation previews, followed by a separate confirmation. This repo needs checks the selected repository's tools and inputs asynchronously and offers setup commands you control. Agent Details → Manage queue lets you reorder/remove pending targets after stopping the agent; additions made during an active batch survive that batch's completion.

Explicit Git sync is destructive: review its exact target, local changes and listed deletions before confirming. It backs up allowed changed files and pins the prior commit under refs/tangos/backups. Protected assets stay local; protected tracked/source changes block the reset.

## Descriptor setup and editable help

When tangos.json is missing or invalid, Generate descriptor scans available checks. Edit the JSON draft, choose Preview write, inspect the complete configuration, then Confirm write. An edited draft or changed repository invalidates confirmation. Reload descriptor rebuilds the native workspace after the write. Different folder opens the local repository picker.

Tour includes ten steps and expression artwork. Tips switches to short help messages. Edit text opens tango-tour.txt or tango-tips.txt in your local configuration folder. Changes are read when reopening/navigating help; empty files fall back to embedded defaults. Tour completion is remembered. Matching/publication instructions retain mandatory review and port-only safety.

Viewer color and contributor controls are independent of the grouping layout. Choose status or author colors, select a contributor to dim other authors, and toggle draft/near-miss visibility. Color and draft choices are remembered in console-ui.json. Published career/daily totals and shared colors require your enabled connections; local attribution is the offline fallback. The minimap follows the same color choice.

## Remote projects and viewer-only mode

**Import project ZIP** accepts a stored/DEFLATE ZIP with a valid `tangos.json`,
including GitHub archives with one enclosing folder. Pick the archive and parent
folder, review the complete file list, then confirm. The destination must be new.
Import initializes Git and remembers the project; files remain untracked until
you explicitly review and commit them. No archive script executes. ZIP64,
encrypted archives, symlinks, path traversal, case collisions, protected assets,
credential content, archives over 128 MiB, files over 16 MiB, and expanded content
over 256 MiB are refused. Failed imports retain their partial folder for inspection.

Remote discovery uses your **projects.registry** connection. Set its URL to your
project registry (the Console reference uses `https://tangos.dev/api/projects`),
method GET, and enable it. In `connections.json`, set
`"allowDescriptorDownloads": true` (or check **Allow public GitHub descriptor downloads**) on that profile to permit fetching public
GitHub descriptors. Choose **Discover remote projects** in the project menu,
then choose a discovered project to download its descriptor and open Viewer.
Registry credentials are never forwarded to GitHub. Valid descriptors are cached
for 24 hours; an offline refresh preserves the previous descriptor and projects.
Discovery preserves local checkout paths and does not remove remembered entries.
Check **Allow automatic leases / project discovery** on `projects.registry` to
refresh the registry and warm descriptors at startup. Both automatic access and
descriptor downloads are opt-in. Cancellation stops warming between requests;
each in-flight HTTP request retains the bounded network timeout.

Open the project title menu (or Choose a project on first launch), choose **Add remote project**, and select a downloaded `tangos.json` descriptor. The project is remembered in `projects.json`; `active_project` in `settings.ini` restores it next launch. You do not need to clone it to view published progress.

The landing screen opens without a network request. Choose **Open Chaos Viewer** to load the descriptor's published database. If the project uses a different endpoint or needs credentials, configure your own `atlas.live` profile in **Connections**, enable it, and reload published data. Credentials belong in your environment or the encrypted local vault. Nobody else's API key is included.

Viewer-only mode allows searching, sorting, panning, zooming and inspecting published function metadata. Work assignment, local tools, Git changes and agent execution require a local checkout. Use **Choose a local folder** when you have one. **Clone project** opens the existing previewed clone service; after cloning, select the resulting folder.

## Portable updates

Configure your own `update.check` connection and trusted **Update asset prefix**
(an HTTPS release directory ending in `/`). Enable downloads on that profile.
Release metadata must contain `version`, `artifactUrl` and `sha256`, or a GitHub
release with a `TangOSLite.exe` asset and its `sha256:` digest. No connection or
publisher is silently enabled.

Help and updates can check releases, preview and confirm a download, then restart
to install it. The separate automatic option stages an available update at
startup and installs it when the application exits. The helper keeps a
`.previous-…` executable beside the installation for manual rollback. Installation
results remain in the local `updates/last-result.json`. Close other application
copies if Windows prevents replacement. A failed checksum or changed executable
requires a fresh download; it never installs an unchecked candidate.
