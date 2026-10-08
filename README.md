# tangOS Native

A unified brand and toolkit for reverse-engineering / decompilation projects.

Everything reads **one file per repo** - `tangos.json` - a descriptor that normalizes any
decomp repo (whatever its layout) into a single vocabulary. Point a tangOS app at a repo, and
it knows the repo's tools, compiler, data source, and rules.

## Apps

| App | What it is | Status |
|---|---|---|
| **tangOS Console** | Downloadable desktop app. Exposes a repo's tools as an **MCP server** an AI connects to, with a **live viewer** to watch the AI drive them in real time. | in progress (`console/`) |
| **TangOS Lite** | Portable Windows C++ workbench: repository status, cancellable checks/logs, reviewed Git operations and GitHub CLI integration. No browser runtime. | native implementation; parity audit underway (`lite/`) |
| **tangOS Docs** | Browsable catalog of a repo's tools, generated from `tangos.json`. | planned (`docs/`) |
| **tangOS Atlas** | Progress atlas / treemap (formerly Chaos Viewer). | planned rebrand |

## Portable native app

Build and run **TangOS Lite** from [`lite/`](lite/README.md). The Windows app is one portable executable with C++/Win32, TinySkia and embedded Nunito; it does not bundle Electron or a browser runtime. No installer is required. External tools and account credentials belong to each user.

The original TypeScript/CSS Console under `console/` and related packages remains as the parity reference and development test oracle. It is not packaged in the native executable. GitHub's language chart counts this retained source.

Full parity is still being audited: see [`lite/docs/UI-PARITY.md`](lite/docs/UI-PARITY.md) and [`lite/docs/LIMITATIONS.md`](lite/docs/LIMITATIONS.md). Build, tests, release reproduction and security review are documented under `lite/`. Do not treat a passing native build as proof that every reference feature is complete.
