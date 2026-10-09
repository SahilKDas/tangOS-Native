# Console parity audit

## Styled help and window stacking checkpoint

Editable `:joke[...]` text now follows the original span parser (129 differential
cases), uses italic Nunito and the theme's primary/accent gradient, and wraps
without displaying markup. The drawing buffer is bounded to visible width.
Native snapshots, tour backdrops and the helper share a back-to-front window
renderer that includes owner-drawn controls. A captured-header pixel regression
rejects the previous helper screenshot ordering and accepts the correction.
Helper visibility follows the original Controller-only rule.

Backend regression tests and all three native GUI workflows passed. Evidence:
`lite/out/gui-richtext-layered-final-024`, `gui-layered-missing-024` and
`gui-layered-invalid-024`. Original background physics, blur/shadow composition,
animation cadence and auxiliary overlay presentation remain under comparison.

## Floating help, tour and active Controller checkpoint

The native helper now preserves announcement read state, opens floating editable
tips beside Tango, supports circular navigation and click bounce, and anchors to
the root window. The first-run/replay tour overlays the existing screen, resolves
real control spotlights, supports keyboard navigation and preserves focus.
Simple mode running cards use a full-width Stop button and disable it while
stopping. Progress follows the reference 300 ms easing. Live role/count/effort
changes apply to the next batch without replacing the running instructions.

All five native suites and the expanded GUI workflow passed on 2026-10-09.
Original Controller, helper and tour captures were made with a loopback-only
development renderer. Exact animation, background and auxiliary layout parity
remain under comparison; this checkpoint does not establish full pixel parity.

## Project / batch / support workflow checkpoint

Discovery retains project glyphs and the project menu shows them. Existing
registry, descriptor cache, ZIP safety, viewer-only selection and clone-and-open
flows remain covered by backend and native GUI integration tests.
Asynchronous generation now preserves concurrent draft edits and excludes new
reservations before saving. Both cases have disposable scheduler regressions.
Draft buttons fit narrow windows. Support Tips now opens the correct overlay;
a complete native reference includes workflows, tool commands and arguments,
tour and tips. Update responses have actionable text. Git adds working/staged
differences, history and confirmed unstaging without discarding working files.

Display smoke tests exercise every attached monitor and three window sizes,
checking the Batches, Help and reference controls for clipping. They record actual
monitor scaling and explicitly leave unusual DPI unverified when no suitable
monitor is present. Windows still virtualizes DPI; this is not proof of sharp
per-monitor TinySkia rendering or exact reference animations.

## Controller telemetry and compact fields source checkpoint

The native Controller view now uses the original latest-batch selection, worked
target progress, pending queue totals, task precedence and latest output-line
rules. A new comparison extracts the actual React transformation and checks 138
combinations, including equal timestamps, Unicode output, empty tasks and other
agents' activity. The six-pixel progress bar and conditional hit/near statistics
are rendered in both modes, with statistics taking height from the task panel.
Task/live text follows the reference's flex shrinking and clipping; notes have
their yellow frame and live output uses a monospace font.

Controller activity caching selects one latest run per agent and copies at most
1,600 bytes of output per run. Tests compare it with the complete activity view
and confirm retained logs are unaffected. Debug snapshots continue to omit logs.
Compact role/effort fields use rounded TinySkia frames while preserving native
dropdown and keyboard behavior. Numeric fields retain native editing and caret
handling with centered text and rounded borders.

These changes follow the verified 0.21.0 package. Exact multiline-note wrapping,
progress easing, popup-list decoration, tooltips, card hover movement, background
motion and unusual DPI still need validation or implementation. Full parity is
not established by these source changes.

## Advanced Controller source checkpoint

Advanced cards now use measured queue rows, a compact count, continuous toggle,
API attempts, additive/removable role chips and a separate disabled-empty Drive
queue row. Single-option effort controls are omitted. Role order persists in
fleet.json; legacy single roles migrate, and the first role remains compatible
with existing scheduler selection. Running cards collapse their actions to Stop.
The MCP button uses the same header placement in both modes, correcting an
overlap found during native screenshot inspection. Profile creation moved to
Advanced settings and the key vault, preserving the original Controller header.
Native widget decorations, active role reconfiguration, progress composition,
card hover motion and background animation still differ from the original.
The offline reference renderer can capture either Simple or Advanced fixtures.

Waiting queue edits now preserve in-flight targets and active batch history;
preparation remains protected with an explicit retry error. A controlled CLI
fixture exercises removal/reorder guards, waiting-only clearing and completion
without resurrecting cleared work. These source changes follow 0.20.0 and do
not establish full Console parity.

## Native report overlay source checkpoint

The following statistics correction preserves first-observation bucket order
through storage and CLI JSON parsing. All 24 equal-rate bucket permutations now
match the original JavaScript's stable recommendation order, alongside the
existing statistics/detail comparisons. Existing explicit order metadata is
retained. Old native files that already lost their original order cannot have
that history reconstructed; newly observed buckets retain it going forward.
This correction follows the verified 0.20.0 package and is not included in it.

The Report action now opens a native modal with the original 520px empty panel,
482x120 description field and measured attachment/footer positions. Screenshot
attachments survive local export; modified attachments invalidate the preview.
The export runs on a worker and closes without blocking the main UI. Native
regressions cover metadata, attachment hashes, redaction and the modal workflow.
Clipboard bitmaps are compressed to PNG natively, including a tested 4K fixture.
Remaining differences include WebP, chip sizing,
textarea decoration and the original report's richer diagnostic fields. This
checkpoint does not establish full Console parity or replace the 0.19.0 package.

## 0.19.0 Controller reference measurements

The offline capture tool now records actual original DOM geometry alongside its
screenshots. Native Controller uses the measured three-column offsets, 210px
Simple cards, 103px task area, 58px count field and 44px Go/Stop controls. The
header count scope, 24px detail icon, API badge, 9px presence dot, matched count,
italic idle text and centered footer have corresponding native implementations.
Controller and task fills now follow their own reference CSS rather than the
generic glass panel. The agent tint is confined to its border instead of leaking
through the entire card fill. Regression captures include a passive API card,
and native tests assert card/detail/footer geometry and remembered Writes state.
Simple cards expose Add chosen functions when the Viewer cart contains picks;
the task area shrinks while the card height stays fixed. The native workflow
tests per-agent cart assignment and verifies no provider starts as a side effect.

These changes do not establish exact visual parity. Numeric field skinning,
card hover transforms, background animation, full Advanced queue composition,
and signed-in GitHub footer states still differ. Review and Push deliberately
retain the user's mandatory preview policy; unattended rolling-PR behavior is
disabled. A fleet regression also exposed and fixed policy-read contention with
another agent's statistics transaction; a deterministic lock test covers it.

## 0.18.0 implementation checkpoint

The toolbar Settings button opens a native scrollable popover while preserving
Controller or Viewer. It includes expandable explanations, interface mode,
theme/motion, near-miss policy, delegation, functions per sub-agent, isolated
auto-land, tour replay, two-click statistics clearing, and local reports.
The key vault and advanced settings remain separate native screens. Snapshot
and synchronization buttons route to the existing reviewed service screens;
they do not yet reproduce the original snapshot or sync overlays exactly.

The following 0.18.1 source adds a real native window/state/layout debug export
and Ctrl+Shift+D. It exports curated metadata rather than credentials, commands,
prompts or log text. It does not reproduce the original browser DOM/DevTools
dump. Settings teardown ignores focus notifications during destruction, and
reopening resets its pending destructive confirmation.

The original worker policy is compared directly for 1,320 cases, alongside
7,208 automatic-role and 2,600 provider-effort cases. Requesty, GLM, GPT and
Nemotron remain serial drivers. Fanout means functions per sub-agent, with
flooring and a 1–64 limit; the shared API/CLI/MCP prompt now carries that policy.
Safe mode is checked when claiming prepared MCP work, as well as before local
execution. Glass panels use the reference three-stop gradient and theme border.
The Help/agent-operations command ID collision is fixed and covered by the
native window workflow. These changes do not establish full parity.

Reference: the original `console/` source retained in this repository. Full parity has **not** been established. `parity.json` inventories public contracts; an unverified contract must never count as complete.

The native frontend includes Controller agent cards, roles/effort/count controls, API/CLI/MCP profiles, encrypted local keys, queues and isolated worktree review; a weighted Atlas with local reload, minimap, source-level rendering, source/history inspector, contributor/claim overlays, smooth navigation, marquee selection and module popouts; searchable Encyclopedia, typed arguments and tool visibility; Settings, Connections, Requirements, missing/invalid-descriptor gates and advanced Services screens. Animated TinySkia backgrounds use the five reference palettes and embedded Nunito. Left-drag pans; right-drag selects; wheel zooms; WASD/arrows navigate functions; Space toggles the cart.

External profiles default off. Live refresh requires the user's explicit setting and enabled profiles. Each user controls endpoints and credentials. The native CLI service catalog is broader than the dedicated frontend; a generic JSON service form does not establish screen parity.

The ten-step tour and editable tips now use the reference parser format and Tango expressions, with safe native workflow guidance. Descriptor generation, editing, side-effect-free preview, confirmation and reload are tested against disposable missing/invalid descriptors.

Remaining work includes complete reference composition for Controller and Viewer,
helper spotlight/update/report overlays, automatic MCP client registration,
complete update-screen composition, and independent-client/provider protocol comparisons.
Native controls, dock composition, background alignment, animation positions and
confirmation screens still need side-by-side visual checks. Arbitrary DPI and
large-database sustained performance remain unverified. Portable update application
and a 25,000-function GUI workflow have automated coverage recorded in VERIFICATION.md.

Automatic pushing conflicts with the requested mandatory outgoing-commit preview. It stays disabled. Port-only protections and protected asset preservation also take precedence over unsafe reference behavior. These are explicit product constraints, not missing checks to bypass.

Atlas geometry and tile colors are now compared directly with original buildWorld/fnColor. Contributor filtering, independent status/contributor colors, per-user color/draft persistence, career/daily totals and distinct exemption/claim overlays are implemented. The compact contributor selector does not yet reproduce the original full roster/list composition.

The native MCP connection screen now provides client/traffic visibility, persisted asynchronous start/stop, and copyable client configuration and agent instructions. Authenticated MCP DELETE disconnection and preservation of unrelated CLI jobs are covered by integration tests. Full third-party-client interoperability remains unverified.

The native Batches screen now stores project drafts and named queued/active/completed batches, assigns drafts, reorders/removes stopped batches, clears completed history and opens retained logs. Per-batch instructions reach the actual driver prompt. Partial completion preserves untouched targets; interrupted and legacy queues recover. Forty original-code comparisons cover removal, order, clearing and completed-history retention. This does not yet establish complete global/unassigned queue or original draft-generator UI parity.

Source inspection now follows the original 400-line response and disassembly fallback, with twenty original-handler comparisons. Normalized local exclusions remain enforced even through a src/../ path. Current selected-checkout instructions, including modified and untracked AGENTS.md files, reach isolated API/CLI agents. Unique agent names and ambiguous legacy MCP routing are guarded. Live Atlas cache and full inspector composition remain unverified.

Native API usage stopping now follows the original explicit quota/credit signals and five fast-empty-run rule, with 198 original-code comparisons. One-shot execution drains pending chunks; all per-run instructions/worklists/results are preserved and result paths survive restarts and MCP/landing operations. Driver summary result rows and token aliases now feed statistics without phantom metadata attempts. Dedicated project/no-clone and other remaining screen work is still open.

Controller visual comparison now uses a development-only offline bundle of the actual original React frontend with a read-only synthetic bridge (`reference-render.py`, `reference-capture.cjs`, and `reference-ui.json`). The capture tool uses an externally installed browser and Playwright, rejects non-loopback reference URLs, and blocks requests outside the local reference origin. These tools and dependencies are not packaged.

The native Controller now has three-column colored agent cards, Simple/Advanced layouts, a descriptor-derived title, optional requirements rail, and an aspect-correct mascot. The GUI regression exercises four cards and verifies every card control stays within its bounds in both modes. Area-averaged raster downsampling is tested with dense checkerboard detail. This checkpoint does not establish pixel-identical cards, full requirements compact/expanded states, project switching, or the remaining detailed Controller controls.

The native project title now opens a remembered local-project menu. Selecting a valid Git checkout registers its canonical location and descriptor-derived title in the human-readable local projects.json registry. Active agent runs block switching. The actual GUI selection test verifies the remembered path and title. Remote/no-clone entries and ZIP/clone gates remain separate unfinished frontend work.

The Viewer now includes a selectable function roster with the original 500-row cap and six sorting modes. Twenty-four direct original-sort comparisons cover empty/single/mixed lists, stable ties, case and accents. Sort preferences persist, roster selection selects/zooms the target, and the GUI test checks contributor/draft controls do not overlap. Latest source-inspection requests are queued while an earlier request runs; the GUI regression switches targets during inspection and verifies numbered source for the final target. Original Controller and Viewer reference captures both render without errors. Full Viewer screen composition, complete contributor roster, hover interactions and unusual locale behavior remain open.

0.15.0 adds remote descriptor import, remembered remote/local project selection, a no-checkout landing screen and published viewer-only mode. Native GUI coverage verifies no network at landing, no Git checkout/Fleet/MCP, explicit published loading, and disabled assignment including the keyboard shortcut. Automatic registry discovery, ZIP import and automatic post-clone project switching remain open; this is not a full parity claim.

The 0.16.0 source checkpoint adds a complete destination/preview/progress/cancel/
clone-and-open flow, opt-in startup registry discovery, public descriptor warming
and 24-hour offline fallback, and native stored/DEFLATE ZIP import. Backend tests
cover discovery and credential isolation; disposable ZIP tests cover valid import
and adversarial path/content cases. Native window tests cover clone completion,
global batch handoff, local report preview/save and fresh/stale published caching.
Controller details include lifetime/session statistics, queue edge controls and
per-agent durable logs. Viewer includes a contributor strip and metadata hover.
Draft generation uses isolated worktrees. MCP client templates and the native
stdio bridge have transport integration tests. These additions supersede earlier
unfinished feature notes, but do not establish exact full-screen parity or real
provider/client interoperability. Reproducible 0.16.0 packaging passed as recorded
in VERIFICATION.md.

The Viewer now implements the original full-map state: statistics and rosters
hide while the map expands beneath the header. Its toggle and F11 restore retain
selection/cart state; zoom, camera travel, minimap and marquee use the same
viewport bounds in both layouts. Actual GUI coverage includes a 25,000-function
fixture and verifies the original 500-row roster cap. This is functional
coverage, not proof of pixel-identical composition or every DPI configuration.

Portable update staging/application now has opt-in native controls and tested
checksum, executable-header, trusted-host and rollback boundaries. The official
MCP Inspector CLI has exercised the native bridge. Paid providers and other
desktop clients remain unverified. Real local compiler/ROM-dependent checks ran
and retained their failures as documented in VERIFICATION.md.

The next native checkpoint adds a bounded process-local activity bus, real
session statistics independent of persisted lifetime totals, model-separated
live output, ten recent runs with expansion, copy feedback, and measured role
recommendations. There are 111 direct original activity/detail comparisons and
32 original incremental-statistics comparisons. The actual native-window fixture
`lite/out/gui-controller-activity-017-b` passes latest-run hydration, recent-run
expansion, model isolation and scope switching. Equivalent Windows slash/case
paths resolve to the same activity scope. Complete output remains in disk logs.
The contributor strip now includes color dots, daily badges and toggle filtering.
These functional checks do not establish exact modal, card or overlay geometry.

Client connection can preview and install native stdio configuration for Claude
Code, Claude Desktop, Cursor and VS Code. It preserves unrelated entries, backs
up an existing file and rejects edits made after preview. Disposable installation
tests pass; no personal client configuration or API keys were changed in testing.
MCP supports 2026-07-28 per-request discovery alongside four legacy handshake
versions. HTTP routing mirrors, encoded names, metadata, cache hints and result
types have integration coverage; modern stdio and the official Inspector pass.

0.17.0 follows captured original geometry for the Viewer statistics header,
map and right-hand search/filter/sort/roster stack. Agent detail is a centered
560-pixel overlay with a 3-pixel blurred, 32-percent darkened scrim, role banner,
four statistics cards and retained live/history controls. Reviewed operations
remain available in its menu. Overview function labels and the zoom-only,
upper-right minimap follow the original rendering rules. The offline capture
tool now records the original detail overlay as well as Controller and Viewer.
The main toolbar, native control styling and other screen composition still
require exact comparison; these changes do not establish full pixel parity.

Simple-mode role selection now follows the original model-family prior,
measured recommendation, explicit assignment and adaptive-role ceiling. Both
scheduled and explicitly queued batches use the same selection. Provider
families are configurable locally. Advanced-mode effort choices and actual
driver effort use the original family-specific catalog and validated defaults,
including DeepSeek, Requesty and providers whose effort is off. The release
gate compares these policies directly with the retained original TypeScript.

Internal Git worktree metadata operations are serialized with cancellable
waiting. This fixes a release-gate failure where simultaneous agent creation
read another worktree's partially written commondir. Regression coverage creates
eight parallel disposable worktrees and cancels an operation waiting behind a
controlled checkout hook. External Git processes are outside this local lock.

The next UI source checkpoint uses native TinySkia toolbar icons, hover states,
tooltips, selected tabs, reference toolbar spacing and accurate owner-drawn
button captures. Theme selection is in Settings. Controller cards expose
session/all-time totals, resolved idle roles and chart buttons. Pick in Viewer
keeps the parent tab synchronized, and Escape closes detail even from a focused
log control. `lite/out/gui-toolbar-017-d` passes the full native workflow and
these interactions; the 25,000-function fixture rendered in 1,672 ms. An earlier
attempt timed out because every translucent button repainted the large Viewer;
the corrected cache shares one backdrop per parent paint. Raster tests pass.
This source work follows the verified 0.17.0 package; it has not yet replaced
that package. Glass composition, footer layout and remaining overlays still
need comparison and implementation.
