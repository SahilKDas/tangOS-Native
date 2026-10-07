# Console UI implementation and remaining differences

The reference is the Console source in this repository: Aero CSS, Controller, AtlasView, Encyclopedia, RunDock, Settings, KeyVault, AiDetail, ReviewPanel, TangoTour and the classic atlas/squarify renderer.

Implemented natively with TinySkia and embedded Nunito: frameless Aero header, five palettes and Tango artwork; Controller agent cards, role/effort/count controls, live tails and queues; Chaos Viewer weighted squarified atlas with grouping/filter/search/zoom/cart; searchable descriptor Encyclopedia and typed argument form with run logs; provider/CLI/MCP profile editor; DPAPI vault; agent details, isolated worktrees, cancellation, independent checks and complete diff/commit review; five-step tour. Git and GitHub operations remain available in Git & reviews.

Packaged smoke tests render these views and run a real local CLI agent through isolated worktree creation, instructions, verification and diff review while checking UI messages continue to process.

This is not verified pixel-identical across every Console screen. Windows controls/dialogs, static gradients, the dock layout and confirmation flows differ. The original animated backgrounds, store/cosmetics, OAuth screens, remote no-clone project mode, source-level atlas LOD/minimap/marquee, remote claim service and all auxiliary overlays are not implemented. Atlas published data is manually refreshed. Provider and user-owned toolchain compatibility requires the selected repository's scripts and inputs. These are outstanding features, not completed parity.
