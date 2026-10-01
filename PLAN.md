# Csrio Plan

Current version: `0.3.5`.

Completed UI implementation and validation (2026-09-24):
[native Ela capabilities and release evidence](docs/ela-native-capabilities.md).
Release CTest passed 10/10, including 168 GUI cases and the 100%/200% native
interaction suites. The 16 main-window light/dark × size × DPI hard checks and
120 state captures retain exact-size, 10pt-font and clipping contracts.
Final delivery uses a clean Release/ELA test-OFF directory staging; no ZIP.
The coordinating task owns AppSuite replacement and shared manifests.

External disk-change accept/reject, large-map editing and semantic UI/density are implemented.
No previous competition-UI delivery remains an active task.

- Completed UI fix (2026-09-23): remove duplicate New/Open Project buttons from
  the no-project card while retaining toolbar/menu/shortcut commands and direct
  recent-project reopening. Release 0.3.4 contains this scoped fix. Full CTest
  passed 8/8, including 168 GUI cases; all 16 light/dark × 960×720/1440×900 ×
  100/125/150/200% empty-state captures passed exact-size, 10pt-font and control-bound
  assertions and were visually inspected. Evidence is under
  `build/ela-migration/release-0.3.4-empty-screenshots` and the same build directory's
  `ctest-release-0.3.4.log`.
  Protected example hashes, core/CLI behavior and file formats remain unchanged.
- Completed goal (2026-09-22): all three
  [ElaWidgetTools migration slices](docs/ela-migration.md), including status bar,
  candidate lists, read-only result tables, Type/Access editors and ElaAppBar.
  Release validation passed CTest 8/8 and 168 GUI cases. Native Windows contracts
  passed 7/7 in every light/dark × 100/125/150/200% run. The unchanged 16-case
  main-window hard checks, 120 state captures and 40 native captures are retained.
  Protected examples, models, transactions, generators and CLI remain unchanged.
  Release 0.3.3 publishes these completed slices using the repository's portable
  ZIP target; the archive must identify a clean commit and include SHA-256.
  Physical mouse drag/Snap and cross-monitor DPI transitions are not claimed as tested.
- Preserve stable object IDs, validated transactions, undo, external revision checks and managed-region ownership.
- Keep disk/CLI external decisions distinct from managed RTL three-way merge.
- Update schema, CLI and generated-output documentation when their contracts change.
- Run core/CLI/UI/integration checks appropriate to the change; record fresh evidence rather than old test totals.
