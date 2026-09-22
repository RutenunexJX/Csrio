# RegMapWorkbench Plan

Current version: `0.3.3`.

External disk-change accept/reject, large-map editing and semantic UI/density are implemented.
No previous competition-UI delivery remains an active task.

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
