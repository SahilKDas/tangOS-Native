# Verified Windows release — 2026-10-06

TangOS Lite 0.2.0, Windows x64, TinySkia and embedded Nunito release. GCC 14.1.0 (MSYS2 UCRT64 Rev3),
CMake 3.29.3, Rust 1.98.1 with the Windows GNU target, Release `-O3`, static GCC runtime, stripped executable, PE timestamp
disabled. Only Windows system DLL imports, including GDI, CNG and UCRT.

| Measurement | Result |
|---|---|
| Executable | `TangOSLite.exe` |
| Exact size | **2,954,752 bytes** |
| Decimal MB | **2.955 MB** |
| Budget | Below 10 MB; strictly below 50 MB |
| Two clean build directories | Byte-identical SHA-256 |
| SHA-256 | `494A845CD31767D9D0DD9D6C0266095D85CACFC136F2FA9833D8EBCD3680635E` |
| Automated core / disposable Git assertions | **60 passed** |
| Packaged executable launch and GUI workflow | **Passed** |
| Packaged SHA-256 match / mismatch behavior | **Passed** |
| Real SM64DS `port_refcheck` script | **337 references, all resolve** |

The packaged native window selected and remembered a disposable repository with
spaces in its path, displayed branch/status/worktrees, switched both navigation
views, opened the Encyclopedia, ran the discovered check, displayed a file:line
diagnostic, and preserved the complete log. Its timer continued responding
while the check ran. Embedded Nunito selection, repeated status updates and a
TinySkia BGRA raster replacement regression test passed. Renders were generated for landing, controller, repository,
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

