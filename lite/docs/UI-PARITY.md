# Console visual reference

Reference: the Console source in this same repository, not an unverified later
installed build. The native renderer maps the shared Aero tokens and main frame
directly from `packages/ui/aero.css`, `console/src/renderer/src/app.css`,
`App.tsx`, `RepoPicker.tsx`, `Controller.tsx` and `AppSwitcher.tsx`.

Implemented: frameless draggable header with native minimize/maximize/close,
tangOS brand with primary-colored OS, centered segmented navigation, 14 px
workspace gutters, wide controller and 340 px right rail, rounded 14 px glass
panels, Segoe UI typography, primary/ghost/danger buttons, the centered repo
landing card, five original named palettes, and embedded original Tango mascot
and icon. Themes are remembered. All rendering is native Win32/GDI+.
The controller retains Console's empty-agent state and footer Encyclopedia entry;
manual check/Git/log controls open from that entry. Repository navigation provides
the full status view. Smoke testing exercises both views and all five palettes.

Lite's functional panels contain local checks, Git operations and logs rather
than pretending to have Console's provider/fleet functionality. The second view
is Repository, not the unimplemented Chaos Viewer atlas. Native dropdowns,
scrollbars, folder picker and approval windows retain Windows behavior. Static
gradients replace animated blobs, bubbles and backdrop blur. Full Console
settings/key vault, atlas, tours, fleet cards, complete descriptor encyclopedia, floating run
dock and every auxiliary screen are not ported. These are remaining visual and
interaction differences; this is a close reproduction of the shell/design
language, not established pixel parity across all Console screens.

GUI smoke testing produces landing/workspace BMP renders from the actual native
window and tests selection/status/check/log contents plus responsiveness. The
release verification records rendered screens and remaining differences honestly.
