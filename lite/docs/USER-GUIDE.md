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
