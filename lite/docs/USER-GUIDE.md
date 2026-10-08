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

In Chaos Viewer, left-drag pans, right-drag selects eligible functions, the wheel zooms and WASD/arrows travel. Space adds/removes the selection from the cart. Inspect source opens numbered source and prior tries; Pop out module opens an independent Viewer that sends draft additions back to the main cart. It does not start a second agent controller.

Settings provides matching, delegation, animation and live-refresh controls. Connections stores user-owned endpoint profiles with credentials referenced by local environment/vault names; nothing is enabled by default. Project services shows request results and exact mutation previews, followed by a separate confirmation. This repo needs checks the selected repository's tools and inputs asynchronously and offers setup commands you control. Agent Details → Manage queue lets you reorder/remove pending targets after stopping the agent; additions made during an active batch survive that batch's completion.

Explicit Git sync is destructive: review its exact target, local changes and listed deletions before confirming. It backs up allowed changed files and pins the prior commit under refs/tangos/backups. Protected assets stay local; protected tracked/source changes block the reset.

## Descriptor setup and editable help

When tangos.json is missing or invalid, Generate descriptor scans available checks. Edit the JSON draft, choose Preview write, inspect the complete configuration, then Confirm write. An edited draft or changed repository invalidates confirmation. Reload descriptor rebuilds the native workspace after the write. Different folder opens the local repository picker.

Tour includes ten steps and expression artwork. Tips switches to short help messages. Edit text opens tango-tour.txt or tango-tips.txt in your local configuration folder. Changes are read when reopening/navigating help; empty files fall back to embedded defaults. Tour completion is remembered. Matching/publication instructions retain mandatory review and port-only safety.
