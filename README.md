# tangOS Native

A unified brand and toolkit for reverse-engineering / decompilation projects.

Everything reads **one file per repo** - `tangos.json` - a descriptor that normalizes any
decomp repo (whatever its layout) into a single vocabulary. Point a tangOS app at a repo, and
it knows the repo's tools, compiler, data source, and rules.

## Apps

| App | What it is | Status |
|---|---|---|
| **tangOS Console** | Downloadable desktop app. Exposes a repo's tools as an **MCP server** an AI connects to, with a **live viewer** to watch the AI drive them in real time. | in progress (`console/`) |
| **TangOS Lite** | Portable Windows C++ workbench: repository status, cancellable checks/logs, reviewed Git operations and GitHub CLI integration. No browser runtime. | usable first release (`lite/`) |
| **tangOS Docs** | Browsable catalog of a repo's tools, generated from `tangos.json`. | planned (`docs/`) |
| **tangOS Atlas** | Progress atlas / treemap (formerly Chaos Viewer). | planned rebrand |

## Download

**tangOS Console Native** ships as a desktop app that auto-updates from this repo's [Releases](https://github.com/SahilKDas/tangOS-Native/)

- **Windows** - download and run the installer; it self-updates from here. Not code-signed yet, so Windows warns once: click *More info*, then *Run anyway*.

Upstream was built with Claude Code. I'm doing shit with GPT-6.1 and 5.6
