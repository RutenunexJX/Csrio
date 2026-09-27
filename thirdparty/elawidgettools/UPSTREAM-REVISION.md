# ElaWidgetTools source provenance

Source: <https://github.com/Liniyous/ElaWidgetTools>

Upstream revision: `454cac2d57a47d3cc28577dc817793aec1881ca7`

The `ElaWidgetTools/` library subtree was vendored without its example
application. Its MIT license and upstream README are preserved alongside
the source. The following ZeroSlack-specific changes are applied:

- Convert icon enum values explicitly when constructing `QChar`, as required
  by Qt 6.10.2.
- Omit four example images from the resource collection. ZeroSlack does not
  use Ela's sample user card, acrylic card, or Mica image features.
- Replace the unverified Fontello-generated `ElaAwesome.ttf` with the
  unmodified Font Awesome Free Solid 6.7.2 font (SIL OFL 1.1) and remap
  internally referenced icon values. The exact mapping is preserved in
  `patches/03-icon-font-swap.patch`; eleven unavailable icons use free alternatives.

Reconstruction: extract the library subtree at the pinned revision, then apply
`patches/01` through `06` in filename order. These patches reproduce all 417
files of the audited comparison source, including its historical formatting.
Apply `patches/07-zeroslack-control-contracts.patch` last for the migration:
QIcon/mnemonic/keyboard/checked/focus/default-button painting, selected tool-button
icons, theme repaint, ownership of a removed combo-box layout item, and safe style
detachment before tool/combo/spin-box destruction. The latter includes the shared
popup/view and embedded line-edit styles; visible top-level teardown is tested.

The product adapter in `src/ui/uicontrols.cpp` releases fixed dimensions, restores
ZeroSlack typography, updates per-button theme colors, supplies focus outlines,
and uses Qt's immediate combo popup lifecycle with Ela's style. This avoids
upstream's non-interruptible popup animation. No recursive application event
filter or protected-surface traversal is installed for Ela.

This integration is pinned to Qt 6.10.2 because ElaTabBar includes Qt private
headers. Rebuild both DLLs and rerun validation before changing the Qt version.
Upstream CMake declares version 2.0.0 while its public header declares 2.0.3;
the commit identifier above is the authoritative source version.

Both `LICENSE` (ElaWidgetTools) and `Font/FontAwesome-LICENSE.txt` must
accompany redistributed binaries.

## RegMapWorkbench integration

This repository adds `patches/08-regmap-control-lifecycle.patch` after the
ZeroSlack snapshot above. See `REGMAP-NOTICE.md` for its changes and attribution.
Apply `patches/09-regmap-scroll-and-feedback.patch` next for the second slice.
Apply `patches/10-regmap-remaining-surfaces.patch` next for item views, status,
combo popup teardown and window chrome.
The active adapters are `src/app/workbench_controls.cpp`,
`workbench_feedback.cpp`, `workbench_item_views.cpp`, `workbench_cell_editors.cpp`
and `workbench_window_chrome.cpp`; the ZeroSlack adapter path above describes
historical source provenance, not a build dependency.

Apply `patches/11-regmap-native-capabilities.patch` after RegMap patch 10.
This incrementally merges applicable shared patches 08/10/12/15/23/24/26/27
from ZeroSlack `75180fad5e5f5142684cf092649deffe5720994d` and the xIPs
patch-28 ElaListView style lifetime fix and the verified Wave patch-29 overlay
origin-scrollbar/area lifetime fix. Patch 29 SHA-256:
`c292256d9d23cc391b2a185b88d7335f79410ef08e491f727916829c627a88f8`.
It does not replace the vendor tree.
Existing RegMap semantic painting, scrollbar hit regions, feedback, tab ownership
and AppBar fixes remain. Deferred application-owned combo/menu/toolbar/list
styles supersede the older manual teardown sequences in the overlapping files.
RegMap preserves its reduced-motion policy for menus and combo indicators.
The same increment also includes shared patch 30, contributed by RegMap: add the
Ela layout padding to Qt's freshly calculated popup endpoint and make repeated
visible show requests idempotent, preventing last-row clipping and height growth.
Patch 30 SHA-256: `e69b815ba035831e2a84484acb0c46f957c83f346fff230a8c2b3034d4a44046`.

The native drawer, combo, menu, tree expansion and wheel contracts, local
extensions and application boundaries are recorded in
`docs/ela-native-capabilities.md`. Tree expansion observes the existing Qt private
animation; exact Qt 6.10.2 remains required. Runtime ABI interface level is p27;
patches 28/29/30 do not change public layout or signatures. Original MIT and font OFL license
texts are unchanged; ZeroSlack Apache 2.0 attribution remains installed.

Apply `patches/12-regmap-drawer-settling.patch` after patch 11 for the
2026-09-27 local performance update. It makes settled drawer completion
idempotent and removes redundant geometry invalidation; the public ABI,
animation endpoints, licensing and upstream revision remain unchanged.
