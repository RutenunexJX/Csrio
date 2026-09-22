# RegMapWorkbench integration notice

ElaWidgetTools upstream revision: `454cac2d57a47d3cc28577dc817793aec1881ca7`.
The library subtree and patches 01–07 were copied from committed ZeroSlack
revision `261c90e6d96d9738f7eed2eab27499a25768778d`. No files from that
checkout are needed to build or run RegMapWorkbench.

ElaWidgetTools is MIT licensed (`LICENSE`). The unmodified Font Awesome Free
Solid 6.7.2 font is covered by SIL OFL 1.1 (`Font/FontAwesome-LICENSE.txt`).
The compatibility changes originating in ZeroSlack are accompanied by its
Apache 2.0 license (`ZeroSlack-Apache-2.0.txt`). Retain these notices.

RegMapWorkbench changes, 2026-09-21 (`patches/08-regmap-control-lifecycle.patch`):

- Explicit ownership and detachment of menu, menu-bar, toolbar, tab-bar,
  line-edit, check-box and scroll-bar styles before widget destruction.
- Null-safe menu-bar extension lookup; parent-owned popup/focus animations;
  reduced-motion menu/focus transitions; avoid overlapping menu painters.
- Nonmovable result tabs cannot initiate Ela's floating-tab drag. Tab size hints
  use actual content rather than the parent's maximum width.
- Primary tool-button token painting; menu mnemonic rendering and null-safe
  menu measurement.

The application adapter restores point-based typography and density and preserves
Qt combo popup and text context-menu contracts. Models, delegate transactions,
bitfields, address maps, generators and CLI are not replaced. Native file pickers
and destructive confirmations remain unchanged; window chrome is covered below.

Second-slice changes, 2026-09-21 (`patches/09-regmap-scroll-and-feedback.patch`):

- Opt-in Qt scrollbar groove semantics and 14px hit area; owned, cancellable
  hover animations with reduced-motion support. The application subclass keeps
  Qt wheel and context-menu behavior.
- Message bars use application point fonts, semantic light/dark surfaces,
  wrapped body text and parent-bounded widths. Accessible close controls and
  theme changes are supported; the application provides success accent tokens.
- Parent-owned message animations, idempotent close, independent expiry,
  active-map cleanup and safe resize/parent teardown. Existing status and
  diagnostic records remain authoritative; success notifications are supplementary.

Third-slice changes, 2026-09-22 (`patches/10-regmap-remaining-surfaces.patch`):

- ElaListWidget preserves QListWidget APIs; list/table item rendering keeps Qt
  model roles, checks, selection, icons, elision and focus. The item styles use
  Fusion as their Qt base so semantic colors do not inherit the Windows accent.
- Owned list/table/status styles detach before destruction. Combo popup trees
  are destroyed before their shared Ela style, avoiding reentrant native
  stylesheet cleanup discovered by the Windows 125% lifecycle regression.
- ElaAppBar preserves point fonts, elides long titles, renders Qt's modified
  marker, exposes accessible caption buttons and retains the application's
  close-event veto. Fullscreen hides the bar and restores content margins.
- Win32 hit testing uses message coordinates and the current window's DPR;
  resize borders, caption controls, maximize work-area bounds and frame geometry
  are adapted for the pinned Qt 6.10.2 integration.
