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
