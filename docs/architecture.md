# Architecture baseline

## User-visible contract

Workbench and the managed region of a SystemVerilog source file are the interactive editing
surfaces. `regmapc` provides a guarded automation interface to the same project model. The
application owns one register model and derives read-only XLSX, C header, and Markdown files from
it.

```text
Workbench / regmapc <-> internal model <-> managed RTL
                               |
                               +-> XLSX / C header / Markdown (read-only)
```

The `.regmap.yaml` file is application-maintained project persistence, not a third editing
surface. A `.sync.json` sidecar stores the last synchronized model and is internal state.

## Components and ownership

- `regmap_core` owns the only domain model, schema-version-2 serialization, validation,
  stable-ID diff, managed-RTL parsing and replacement, three-way merge, baseline persistence,
  default-project construction, and all three read-only generators. It is independent of Qt
  Widgets and application state.
- `Csrio` owns editing, source navigation, live diagnostics, synchronization status,
  conflict resolution, and generated-output presentation.
- `regmapc` owns the API-versioned command envelope, stable-ID queries and project comparison,
  validation/generation entry points, revision guards, dry runs, and atomic model-patch
  workflow. It delegates model behavior and persistence to `regmap_core`.

There is no spreadsheet import path, second register model, or additional code-generation family
in this product boundary.

## Persistence and identity

The user-facing hierarchy is `Workspace -> Page -> Register Block -> Register -> Field -> Enum
Value`. The core retains the `AddressSpace` type as the serialized representation of a Page; it is
not exposed as a separate navigation level. A compound field may recursively own
member fields. Every object has a stable ID that survives renames, reordering, and round trips.
Display names and row positions never identify objects.

Workbench presents one Workspace-wide address map with one lane per Page and Register Block as
the smallest visual unit. Register and Field geometry remains in the tables and bit-field view.

Register base addressing remains normalized as `page base + block base + register offset`.
Register type, numeric range, initial/reset values, tags, and the reserved-slot flag are
properties of the single model and therefore take part in YAML persistence, managed-RTL
synchronization, diff, validation, and generation. Every Register occupies one fixed four-byte
address slot and starts on a four-byte boundary. Legacy array count/stride properties are accepted
only while loading older projects, normalized to a scalar Register, and omitted from newly saved
projects and generated views.

Workbench edits are transactions over a working copy. Each accepted transaction is validated
and enters the undo history. Save uses atomic replacement for each project-owned file and marks
the working copy clean only after project YAML, managed RTL, and the synchronization baseline
have all been written successfully.

CLI model patches use stable IDs and an expected project-file revision. The complete candidate
is validated and its derivative outputs are generated in memory before the project is atomically
replaced. A stale revision or any rejected operation leaves the project unchanged. CLI commands
do not edit RTL. Structural move operations preserve the moved object and descendant stable IDs;
deep-copy operations deterministically replace every copied stable ID and clear old source
locations. Parent compatibility and the complete resulting address/bit layout are validated
before saving. A project that parses but has existing model validation errors remains available
to `apply`: one guarded patch may remove those diagnostics, but it must leave an error-free
candidate and cannot replace an old Problem with a different one. Failed or partial repairs write
neither the model nor generated outputs.

Workbench keeps a content digest for the manifest revision it loaded and watches both the file and
its parent directory so atomic replacements are detected. A valid external change is reloaded
automatically only while the Workbench model is clean. If local edits exist, or if the manifest
changes during Save & Sync, persistence stops before replacement and the two versions remain
separate. Reloading or overwriting then requires an explicit user choice; the overwrite path
rechecks the current disk digest before saving. A focused model editor counts as local input even
before its value reaches the WorkspaceStore. External reload is deferred without forcing focus,
validation, or cancellation, so partially typed and currently invalid text is not discarded by a
background CLI save.

Crash recovery stores three project-local files as one verified set: the editable draft, the
saved Workspace state on which that draft was based, and metadata containing both content
digests. Metadata is written last, so an interrupted autosave cannot pair a new draft with an old
base. On restart, Workbench performs a three-way merge between the recorded base, the draft, and
the current project. Independent changes survive from both sides. Property conflicts remain an
explicit user choice, with current disk values as the default; restoring still changes only the
in-memory Workbench model until Save & Sync. Legacy drafts without a verified base remain
available but are labeled as whole-model recovery when the project may have changed.

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
never merged. XLSX opens on a flat Overview worksheet with one row per Block and contains one
additional worksheet per Page, with Page metadata at the top and Block
metadata in section bands. Register groups alternate background colors; only structure registers
contain a generated static bitfield diagram and field tree. Diagram and field rows are initially
collapsed and can be expanded with Excel outline controls. Header rows are frozen. AutoFilter is
limited to the flat Overview sheet; mixed Page detail layouts are not filterable. A later
successful save or explicit generation
replaces any externally modified copy. A byte-identical output with intact read-only permission
is not replaced, so its timestamp and an existing read handle remain undisturbed. The CLI
`status` command exposes the same content-and-permission check without writing.

## Failure semantics

- Schema, model, RTL, or merge errors block persistence and output generation.
- A failed synchronized save does not advance the merge baseline.
- A corrupt baseline blocks automatic RTL merge and is reported as a diagnostic.
- User-owned RTL outside the managed markers remains untouched.
- Multi-file persistence is recoverable rather than globally atomic: if a later file write
  fails, the unchanged baseline makes the next synchronization recompute the pending changes.


## External disk decisions

Disk/CLI manifest changes are compared as stable, dependency-aware items. Selected or all changes can be
accepted or rejected after validation. Accept applies one undoable WorkspaceStore transaction; reject keeps
the Workbench value and records the decision against the observed disk digest. New disk revisions invalidate
old decisions. Parent/child changes cannot leave dangling objects. Neither decision silently overwrites disk.
This workflow is distinct from the managed RTL three-way merge described above.

## Workbench presentation

The shell separates address/project navigation, register table, bitfield/details and diagnostics/RTL sync.
Selection is linked by stable identity. Text-backed status communicates dirty/sync/conflict and diagnostics;
color alone is insufficient. Technical addresses and values use aligned monospace presentation.
Bit spans are proportional, with a legend when labels do not fit. Keyboard and pointer selection use the same
model state. Bounded splitter persistence and light/dark semantic colors retain useful content at compact sizes.
UI refresh and asynchronous completion preserve the last valid model and reject stale results.
