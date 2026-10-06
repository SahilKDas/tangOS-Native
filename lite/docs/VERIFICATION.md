# Verified Windows release — 2026-10-06

TangOS Lite 0.1.0, Windows x64, source checkpoint `13ab2b4` plus notice
line-ending normalization and this report. GCC 14.1.0 (MSYS2 UCRT64 Rev3),
CMake 3.29.3, Release `-O3`, static GCC runtime, stripped executable, PE timestamp
disabled. Only Windows system DLL imports, including GDI+, CNG and UCRT.

| Measurement | Result |
|---|---|
| Executable | `TangOSLite.exe` |
| Exact size | **1,848,832 bytes** |
| Decimal MB | **1.849 MB** |
| Budget | Below 10 MB; strictly below 50 MB |
| Two clean build directories | Byte-identical SHA-256 |
| SHA-256 | `33461C01B8DAF469CE397403EBACBF3F38FFE9C3D5D309F9001BC86165169913` |
| Automated core / disposable Git assertions | **60 passed** |
| Packaged executable launch and GUI workflow | **Passed** |
| Packaged SHA-256 match / mismatch behavior | **Passed** |
| Real SM64DS `port_refcheck` script | **337 references, all resolve** |

The packaged native window selected and remembered a disposable repository with
spaces in its path, displayed branch/status/worktrees, switched both navigation
views, opened the Encyclopedia, ran the discovered check, displayed a file:line
diagnostic, and preserved the complete log. Its timer continued for 13 ticks
while the check ran. Renders were generated for landing, controller, repository,
manual tools, and all five themes. Core tests independently verified descendant
cancellation and log preservation. Native ROM hashing used an innocuous `abc`
fixture with its known SHA-256, not game data. Embedded notices were verified.

Git integration used only disposable local repositories, peers, remotes and
worktrees: fetch, divergent merge, rebase, fast-forward pull, conflicts, commits,
outgoing review, protected/ignored paths, secrets, and intermediate forbidden
history. GitHub/fork/PR adapters were tested for command construction; no real
PR, push or authentication was performed against GitHub.

Port/compiler/ROM-dependent builds, real-game byte matching, and real GitHub
checks were not validated end-to-end. Agent support exports scoped repository
instructions and coordination handoffs; provider/fleet execution is unfinished.
Console's shell/design language is reproduced, but full screen/interaction parity
is unfinished. See LIMITATIONS.md, UI-PARITY.md and SECURITY.md.

`scripts/release.ps1` repeats the release recipe, checks hashes/size, and launches
the final packaged executable. Use the same toolchain and checkout line-ending
policy to reproduce these bytes. Third-party notices are embedded so the app
itself remains one portable executable.
