# RegMapWorkbench UI Implementation Plan

Status: active

Date: 2026-08-25

## Information architecture

- Preserve one workbench window and reorganize it into four stable regions:
  compact project/address navigation, register table canvas, bitfield/details
  inspector, and collapsible diagnostics/RTL-sync drawer.
- Add a page header that shows project identity, source path, Saved/Dirty and
  Synced/Stale/Conflict state, plus Generate and Sync as primary actions.
- Move low-frequency import/export/settings actions to grouped menus or
  overflow. Do not duplicate every menu action in the primary toolbar.
- Every empty or failed region states the missing prerequisite and provides one
  corrective action without opening modal loops.

## Theme and components

- Extend `workbench_theme` into semantic light/dark tokens for application,
  canvas, panel, raised surface, input, table, header, selection, focus,
  diagnostic, address, access, reset, reserved, modified, and RTL-sync roles.
- Use the suite spacing and typography scale: 4/8/12/16/24 px, 32 px default
  controls, UI font for chrome, monospace only for addresses/values/identifiers.
- Provide reusable page header, status badge, section header, empty state, and
  compact toolbar helpers rather than per-widget style literals.
- All focusable controls expose visible keyboard focus and accessible names.

## Register and address views

- Keep address-space navigation compact and searchable. Hierarchy, base address,
  count, and validation state form the first visual level.
- Register table headers remain readable while scrolling. Address, offset,
  reset, width, and masks use aligned monospace formatting; names/descriptions
  retain flexible width.
- Access type, modified state, validation severity, generated/RTL state, and
  conflict are rendered as text-backed badges rather than color alone.
- Selection is synchronized with address navigation and bitfield/details view;
  keyboard Up/Down and Enter follow the same model identity as pointer input.

## Bitfield and details view

- Render bitfields proportionally by bit width with stable minimum label rules.
  Reserved spans are visually subordinate but remain discoverable.
- Show msb:lsb, access, reset, enum/constraint summary, description, and source
  identity without duplicating editable facts.
- Pointer and keyboard selection update the same active field. Selected register
  and selected field use distinct hierarchy states.
- Small widths switch from in-segment labels to an aligned legend rather than
  overlapping text.

## Diagnostics and RTL sync

- Diagnostics are grouped by severity and file/source location, searchable,
  and source-navigable. The drawer header shows counts and current filter.
- RTL sync exposes Synced, Local change, RTL change, Conflict, Applying, and
  Failed states with an explicit next action.
- Long-running generation and sync preserve prior valid content, run outside
  the UI thread where already supported, and cannot publish stale completion.

## Persistence and responsive behavior

- Persist navigation width, inspector width, diagnostics height/visibility,
  selected register/field identity, and theme with bounded defaults.
- 960x720 retains project navigation, register table, selected-field summary,
  and primary actions without clipping; 1440x900 exposes the full inspector.
- Splitter handles remain visible and keyboard/pointer reachable.

## Verification

- GUI tests cover theme tokens, shell structure, primary action hierarchy,
  splitter persistence, empty/error/loading states, keyboard selection,
  bitfield proportional layout, badges, diagnostics navigation, and 960/1440
  responsive sizes.
- Existing core, CLI, GUI, and suite integration tests remain required.
- Light/dark primary-workflow screenshots are generated from deterministic
  fixtures without modifying the protected `examples/minimal` user files.
- The repository is independently committed and pushed; packaging is not run.
