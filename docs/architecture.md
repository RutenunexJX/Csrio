# Architecture baseline

## User-visible contract

Workbench and the managed region of a SystemVerilog source file are the only supported editing
surfaces. The application owns one register model and derives read-only XLSX, C header, and
Markdown files from it.

```text
Workbench <-> internal model <-> managed RTL
                         |
                         +-> XLSX / C header / Markdown (read-only)
```

The `.regmap.yaml` file is application-maintained project persistence, not a third editing
surface. A `.sync.json` sidecar stores the last synchronized model and is internal state.

## Components and ownership

- `regmap_core` owns the only domain model, schema-version-2 serialization, validation,
  stable-ID diff, managed-RTL parsing and replacement, three-way merge, baseline persistence,
  and all three read-only generators. It is independent of Qt Widgets and application state.
- `RegMapWorkbench` owns editing, source navigation, live diagnostics, synchronization status,
  conflict resolution, and generated-output presentation.

There is no command-line product, spreadsheet import path, second register model, or additional
code-generation family in this product boundary.

## Persistence and identity

The hierarchy is `Workspace -> Address Space -> Register Block -> Register -> Field -> Enum
Value`; Workbench presents an address space as a **Page**. A compound field may recursively own
member fields. Every object has a stable ID that survives renames, reordering, and round trips.
Display names and row positions never identify objects.

Register base addressing remains normalized as `page base + block base + register offset`.
Register type, numeric range, initial/reset values, tags, and the reserved-slot flag are
properties of the single model and therefore take part in YAML persistence, managed-RTL
synchronization, diff, validation, and generation. Array count and stride remain internal
compatibility properties and are not presented in Workbench or XLSX.

Workbench edits are transactions over a working copy. Each accepted transaction is validated
and enters the undo history. Save uses atomic replacement for each project-owned file and marks
the working copy clean only after project YAML, managed RTL, and the synchronization baseline
have all been written successfully.

## Synchronization

Synchronization compares three normalized states:

1. the last successful `.sync.json` baseline;
2. the current Workbench working copy;
3. the parsed managed RTL region.

Changes are compared by stable object ID and property. Independent changes are combined.
Different changes to the same property create a conflict containing the base, Workbench, and RTL
values. The user resolves all current conflicts by selecting either Workbench or RTL as the
preference. Invalid RTL is rejected without replacing the last valid Workbench model.

Managed RTL contains one `RMW:BEGIN schema=1` / `RMW:END` region. Synchronization may replace only
that region; text outside it is preserved. An existing RTL file without the markers is never
overwritten. Details are specified in [rtl-sync.md](rtl-sync.md).

## Generated views

XLSX, C header, and Markdown generation is deterministic for a normalized model. Files are
written by atomic replacement and then marked read-only. They are not watched as input and are
never merged. The workbook's default Register Map view groups each register with its field tree;
each group begins with a generated static bitfield diagram. The diagram and field rows are
initially collapsed and can be expanded with Excel outline controls. The flat Registers sheet
remains the secondary register-only view. A later successful save or explicit generation
replaces any externally modified copy.

## Failure semantics

- Schema, model, RTL, or merge errors block persistence and output generation.
- A failed synchronized save does not advance the merge baseline.
- A corrupt baseline blocks automatic RTL merge and is reported as a diagnostic.
- User-owned RTL outside the managed markers remains untouched.
- Multi-file persistence is recoverable rather than globally atomic: if a later file write
  fails, the unchanged baseline makes the next synchronization recompute the pending changes.
