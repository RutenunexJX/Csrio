# Csrio

Current version: `0.4.2`.

Release 0.4.2 reuses project parse results, coalesces derived diagnostic refreshes
and reduces repeated search-result collection while retaining validation and
save gates. The previously observed intermittent RM1103 Unicode atomic-save/RTL
restore failure remains a known limitation; this release does not claim it fixed.

[Source repository](https://github.com/RutenunexJX/Csrio)

Csrio is a standalone Qt desktop application for FPGA and SystemVerilog
register-map development. Workbench, guarded `regmapc` property patches, and a controlled region
in SystemVerilog RTL operate on one internal model, which produces read-only XLSX, C header, and
Markdown views.

Csrio retains the legacy `RegMapWorkbench` organization and `Register Map Workbench`
application storage identities, so existing preferences, layout, Favorites, and recent projects
continue to be read and written in the same location. Only the display name and GUI executable
change. Project-local recovery drafts remain under `.regmap-workbench`; project formats,
`regmapc` commands, `regmap://` links, provider IDs, and `REGMAP_*` environment variables remain
compatible. No data migration or project conversion is required.

```text
Workbench / regmapc <-> internal model <-> managed SystemVerilog RTL
                               |
                               +-> XLSX / C header / Markdown (read-only)
```

The model hierarchy is:

```text
Workspace -> Page -> Register Block -> Register -> Field -> Enum Value
                                                  -> Member Field
```

## Quick start

1. Start `Csrio.exe` and choose **New Project**, or open an existing
   `.regmap.yaml` file.
2. For a new map, use **+ First Register**. To control names and layout explicitly,
   right-click the Workspace/Page/Block tree and create the required Page, Block, and Register.
3. Select a Block. Single-click a cell to select it; double-click or press `F2` to edit it.
   Registers occupy fixed four-byte address slots.
4. Set a Register to the structure type only when it contains Fields, then use its **Open Fields**
   action to edit the bit layout. Scalar Registers need no Field setup.
5. Resolve entries shown in **Problems**. Rejected edits leave the previous valid value unchanged
   and identify the affected cell or object.
6. Choose **Save & Sync**. The state badge must show **Synchronized** before treating outputs as
   current.
7. Open the generated XLSX from **Outputs**. Its first sheet is a Workspace overview; each Page
   has a separate detail sheet.

The minimum portable CLI flow is:

```powershell
.\regmapc.exe --json init .\device.regmap.yaml --name "Device Register Map"
.\regmapc.exe --json validate .\device.regmap.yaml
.\regmapc.exe --json generate .\device.regmap.yaml --target xlsx
```

See [cli.md](docs/cli.md) for guarded edits, target selection, pagination, and JSON response
contracts.

## Current capabilities

- Workbench-first creation and editing of every model level, with stable object IDs.
- One local `.regmap.yaml`, `.yaml`, or `.yml` project can be dropped onto the Workbench window
  to open it directly. Multiple-file drops are rejected, an invalid active edit blocks the
  replacement, and the existing unsaved-change confirmation still protects the current project.
  The same checks apply when the file is dropped directly onto an active table or hierarchy
  editor or a Tag/Access popup; a path is never inserted into the value being edited, and a popup
  belonging to the previous project closes before a replacement opens.
- Editable hierarchy, register and field tables, bit-field view, page/block context bar, and
  inline enum-value table.
- Hierarchy context menus create Pages, Register Blocks, and Registers at their natural parent;
  copy/paste, rename, delete, expand, and collapse remain local to the navigation pane.
  Deleting a Page or Block recursively reports how many Blocks, Registers, Fields/Members, Enum
  values, Range bounds, tags, non-zero Initial/Reset values, and descriptions will be removed.
  The confirmation defaults to cancel, cancellation explicitly reports that the hierarchy was
  kept, and one `Ctrl+Z` restores the complete deleted subtree.
- Structure-register rows expose a dedicated **Open (N)** Fields button.
- Trailing `+` rows for Register, Field, and Enum creation, plus context-sensitive `Insert`
  from the hierarchy or active table; page and block base addresses appear once in
  the context bar above the register table and remain directly editable.
- Window geometry, the Workspace/editor/results splitter proportions, the advanced Field
  column preference, and manually adjusted Register/Field column widths are restored after an
  accepted application close. Columns auto-size only until a user layout exists; project-specific
  selections and filters are not persisted.
- New Blocks choose the first free Base for the preferred 4 KiB allocation, or the largest
  smaller power-of-two allocation that fits the Page. New 32-bit Registers use the next
  four-byte-aligned Offset. If no valid address range remains, Workbench leaves the model and
  Undo history unchanged and reports which capacity must be adjusted.
- Page and Register Block copy/paste and drag movement stay in the hierarchy. `Ctrl+C` carries
  the complete selected subtree through the system clipboard, so it can be pasted into another
  open Workbench window or project without losing Registers, Fields/Members, Enum values, or
  stable contract properties; pasted objects receive new stable IDs. A pasted Block keeps its
  Base when free and otherwise uses the first non-overlapping Base that fits the destination
  Page. A cross-Page Block move preserves its Base and is rejected before changing the model
  when that Base, Page width, or Block name conflicts at the destination. Edit-menu and hierarchy
  context actions name the copied kind and exact destination; incompatible hierarchy clipboard
  data or an invalid destination disables Paste before execution.
- Opening a populated project selects its first Block so Register creation and reordering are
  immediately available. Selecting a Page or Workspace deliberately switches to an aggregate
  view, where the read-only **Location** column identifies every Register's Page and Block and
  Block-specific add and reorder operations require opening a Block again. The Location column
  is hidden in an explicitly selected Block because every visible Register already shares that
  parent. Double-click Location, or select it and press `Enter`/`Space`, to open the containing
  Block while keeping that Register selected; the Register context menu exposes the same action
  with the destination Block name. The
  initial Block is also the active navigation object, so it can be favorited immediately and is
  recorded in Recent.
- Reopening a project resumes at its most recently visited Page, Block, or Register when that
  object still exists. A removed object falls back safely to the first available Block.
- Register context menus duplicate a complete Register contract, including Fields/Members, Enum
  values, Range bounds, tags, values, and descriptions. The copy receives new stable IDs, a unique
  name, and the next four-byte-aligned Offset at the end of the Block. Existing Offsets never
  move; a copy that does not fit the Block/Page is rejected without changing the model or Undo
  history, and a successful duplicate is one undoable edit.
- Reserved address slots and delete-with-offset-shift operations from the register context menu.
  Converting a populated Register to Reserved first reports the Fields, Enum values, Range
  bounds, non-zero Initial/Reset values, and Description that will be removed or replaced; the
  confirmation defaults to cancel and a confirmed conversion remains one undoable edit.
  Delete-with-shift first reports the target Register's Fields/Members, Enum values, Range bounds,
  tags, non-zero Initial/Reset values, and descriptions, then previews the complete Offset result.
  The confirmation defaults to cancel, cancellation explicitly reports that the Register was
  kept, and a confirmed deletion remains one undoable edit. It leaves the model unchanged when a
  following Offset would underflow or the shifted layout would introduce an address conflict.
- Multi-tag register assignment with existing-tag selection, new-tag creation, and tag filtering.
- Single-click cell selection and double-click/F2 editing for register and field properties,
  including register type, value range, initial value, and reset value.
- Editable field width with derived read-only LSB, live MSB/LSB labels, top-layer bit-view
  dragging, one-bit `Alt+Left` / `Alt+Right` movement, and explicit overlap resolution by
  trimming either the moving field or the fields it covers.
- Field context menus duplicate a complete Field contract, including nested Members, Enum values,
  Range bounds, access, effects, Reset, and descriptions. The copy receives new stable IDs, a
  unique sibling name, and the first same-width contiguous free bit range in its current
  Register/compound Field. Existing Fields never move; when a Register Reset exists, the copy's
  Reset follows its destination bits. A copy with no safe placement is rejected without changing
  the model or Undo history, and a successful duplicate is one undoable edit.
- Boolean/enumeration, `intN`/`uintN` numeric ranges, and compound fields with member fields.
- Enum values use one compact section. A scalar enum register shrinks the lower editor to the
  section's content height; opening a Field workspace restores the user's last expanded height
  and keeps the same Enum section directly below the Field table. Hidden and Enum-only states
  do not overwrite the saved expanded Field workspace proportion.
- Continuous validation of identity, address ranges and overlap, field ranges and overlap,
  declared Block allocation ranges, access and side-effect combinations, reset values, and
  enum values.
  A pre-existing invalid layout can be repaired incrementally, but each accepted edit must leave
  only diagnostics that already existed before that edit; reducing the count by replacing old
  Problems with a different Problem is rejected.
- Undo, redo, dirty-state tracking, atomic project save, and unsaved-edit protection when closing
  the application or replacing the current project. The prompt identifies the project and pending
  change count, explains the effect on local edits and Undo history, defaults to Save, and treats
  Escape or closing the prompt as Cancel. A cancelled operation explicitly reports that the edits
  were kept; a blocked save keeps the current project open with its pending edits intact. Selecting
  or dropping the project that is already open is a no-op with explicit feedback; it never presents
  a replacement prompt or discards pending edits.
- One visible **Save & Sync** action and a persistent state badge for unsaved, synchronizing,
  synchronized, blocked, partial-output-failure, and conflict states. Valid active text and Access
  choice cell editors are committed before saving; rejected edits block the save.
  **More** groups the separate Generate and Sync RTL commands beside the primary action.
  Narrow windows give an open Field/Enum editor the full editing area, with **Back to Registers**
  returning to the selected Register. The address map and implicit Boolean values expand on demand;
  wide windows retain the side-by-side editors and their saved proportions.
- Per-output status and update time for XLSX, C header, and Markdown, with a direct retry action.
  Unsaved edits keep these rows visible as **Out of date**; Undo back to the saved model restores
  **Synchronized** without rewriting unchanged files.
  External edits or deletion of these read-only files are detected automatically and surfaced in
  **Generated** with a direct retry path.
- Global `Ctrl+F` search across pages, blocks, register/field names, addresses, tags, enum values,
  and descriptions. Matching object type and full Page/Block path appear in a selectable list;
  Enter/F3 still cycles results and selects the matching Workbench object.
  **All (N)** or `Ctrl+Shift+F` opens every matching result, including matches beyond the first 40
  suggestions, with object-type and Block filters. **Back** / **Forward** (`Ctrl+Alt+Left` /
  `Ctrl+Alt+Right`) return through object jumps, restoring the selected cell, filters, open editor,
  and scroll position within the current project.
- Spreadsheet-style cell selection plus tab-separated copy and paste for editable table cells.
  Copy and matrix paste follow the columns currently visible on screen, so hidden detailed
  properties cannot consume an off-screen clipboard column. **Ctrl+A** selects only visible data
  cells in the Register, Field, and Enum tables; the trailing `+` action row and hidden columns
  never enter the selection. Quoted TSV cells round-trip embedded tabs, line breaks, and quotes
  from spreadsheet applications; malformed quoted input is rejected before any model edit.
  A single plain-text value without table separators is pasted literally, including any leading
  or trailing quote characters. The Register, Field, and Enum context menus expose the same
  ordinary cell Copy/Paste commands and disabled-reason tooltips as the Edit menu. Right-clicking
  an unselected cell targets only that cell; right-clicking within an existing row selection keeps
  the selection intact for multi-cell and full-object commands.
  Popup text inputs retain normal text editing, while Tag/Access choice editors cannot redirect
  copy, paste, Delete, or workspace Undo/Redo into the underlying table. Escape closes either
  popup, returns focus to its original cell, and does not apply typed Tag text or an unconfirmed
  Access choice.
- Multi-row Register and Field editing applies only explicitly checked properties and commits the
  complete change as one undo step. Shared values are prefilled; mixed choices require an explicit
  replacement so merely opening or checking a property cannot silently normalize different rows.
  A local **Batch Edit (N)** button appears only while two or more rows are selected.
  Numeric `minimum .. maximum` ranges can be applied or cleared in the same operation, and the
  dialog cannot submit until at least one property is checked.
  Delete applies to the complete explicit row selection when the current cell belongs to it:
  Registers must share one Block and compact movable surviving Offsets up to fixed-address
  boundaries; Fields must share one parent; Enum values must belong to one Register or Field and
  cannot remove a referenced Initial/Reset value or empty an enumeration. Removing every explicit
  Bool value restores its implicit FALSE/TRUE values. One impact confirmation and one Undo cover
  the whole deletion. If the current cell is outside an older selection, Delete affects only the
  current row.
  Opening a context menu on any selected row keeps the complete selection. Commands that affect
  all selected rows say **Selected** and include the count; commands that affect only the
  right-clicked Register or Field say **Current**, preventing a retained multi-selection from
  implying a batch operation.
  Parent-scoped commands are disabled before execution when selected Registers span Blocks or
  selected Fields have different parents. Their tooltips state the required scope; deleting every
  Member of a compound Field is likewise disabled because the compound must retain one Member.
  Complete Register and Field ranges can be copied separately from ordinary cell text, preserving
  nested Fields/Members, Enum values, ranges, tags, and values while assigning new stable IDs.
  The complete-definition payload uses the system clipboard and can be pasted into another open
  Workbench window or project. Paste starts after the current Register or Field and falls back to
  an earlier free range only when no later range fits. Copy/Paste action names and availability
  follow the active Register or Field panel and display the complete-definition count held for
  that context. Paste tooltips name the destination Block, Register, or compound Field; an
  incompatible or damaged complete-definition payload disables Paste before execution and does
  not replace the last valid in-process copy unless it is itself a compatible range.
- Registers can be reordered within an explicitly selected Block by dragging from the
  Register-name cell. Page and Workspace aggregate views remain available for inspection and
  editing, but require opening a Block before reordering so a drag target is never ambiguous.
  Reordering is disabled while a Tag Filter hides rows; visible name cells explain that the
  filter must be cleared, preventing a drag from silently changing hidden-row relationships.
  Movable Registers exchange
  available Offset slots; selected movable rows move together in their existing order.
  Fixed-address names show a prohibited-drag cursor and are omitted from a mixed drag selection;
  a successful mixed-selection move names the fixed Registers that were not moved.
  A lock icon is shown in every fixed Offset cell and beside the table legend, so fixed state does
  not depend on color alone.
  Contiguous selections also move one slot with `Alt+Up` / `Alt+Down`. Applying the fixed-address
  command to a mixed selection fixes all selected rows instead of deriving an ambiguous toggle
  from the first row. The address map always summarizes the complete Workspace: each Page has one
  lane and every visible segment is a Register Block. All lanes share one linear Block-offset
  scale, so Page mapped spans and Block divisions can be compared directly. Red Blocks identify
  overlap or Page-range violations; clicking a Page lane or Block locates it. The map is
  keyboard-focusable: Left/Right browse Blocks, Home/End jump to the first/last Block, and Enter
  locates the current Block while keeping focus on the map.
- Unsaved edits are written to a project-local recovery draft after a short idle period. A later
  project open offers Restore, Discard, or Not Now; restoring does not modify the project or
  read-only outputs until **Save & Sync**. Each new draft records the saved-project model it was
  based on. If CLI or another process later changes the project, independent disk and draft edits
  are merged. For values changed on both sides, the dialog states the conflict count and requires
  an explicit choice between disk and draft values; disk values are the default.
  Invalid, unchanged, or mismatched drafts cannot replace the loaded project, and restored source
  links continue to target that project.
  The recovery dialog previews changed values and actual conflicts. **Not Now** keeps the earlier
  draft available from **Project > Recovery Draft...** and prevents later autosaves from replacing
  it. A changed project or draft invalidates the preview and requires reopening it before restoring.
  A persistent status badge distinguishes a pending crash backup, its last successful update, and
  a backup-write failure. Every state explicitly says that the project itself remains unsaved
  until **Save & Sync** completes.
- Pages, Blocks, and Registers can be starred as Favorites. The navigation menu keeps Favorites
  and the 12 most recently selected objects per Workspace, displays their complete Page/Block path,
  and opens from **Navigate** or `Ctrl+K`. The star button, View menu, Page/Block/Register context
  menus, and `Ctrl+Alt+F` all expose the same add/remove operation.
- `Ctrl+D` duplicates the current Register or Field only while its table (or active cell editor)
  has focus. It commits a valid in-place edit before copying, rejects an invalid edit without
  creating a copy, and never acts from Tag/Access popups, search, navigation, Enum, or result
  panes.
- Managed RTL generation and reverse synchronization through an explicitly marked region.
- Stable-ID, property-level three-way merge using the last synchronized model as the base.
- Automatic merge for independent changes and explicit Workbench/RTL conflict resolution from
  the Diff panel; no conflicting file is overwritten before the user chooses a side. Resolving
  conflicts reports the affected count and direction, explains which values will be replaced,
  preserves non-conflicting edits, and defaults to cancel. The same confirmation distinguishes
  the complete-source choice required when no initial synchronization baseline exists.
- Deterministic, read-only XLSX, C header, and Markdown generation. XLSX uses one worksheet per
  Page, separates Blocks with section bands, and embeds collapsible bitfield details only for
  structure registers.
- `regmapc` exposes the same project through stable, API-versioned JSON commands for version and
  schema discovery, safe project initialization, summary, object listing, stable-ID lookup, validation,
  read-only stable-ID comparison,
  output synchronization status, generation preview, output generation, and atomic model patches.
  Schema discovery labels each command's project access and file-write behavior, so an embedding
  host can allow only read commands or require a dry run without parsing human-readable help.
  `status` distinguishes synchronized, missing, modified, writable, and unreadable XLSX/C
  header/Markdown outputs without changing files. Already synchronized outputs are skipped during
  generation, preserving timestamps and avoiding a lock failure when an unchanged XLSX is open.
  `status --require-current` keeps the same artifact details but returns a distinct nonzero status
  when any configured output needs regeneration, so CI and embedding hosts need not parse JSON
  merely to make that decision. A stale status also returns the exact revision-guarded `generate`
  recovery arguments.
  `diff <before-project> <after-project>` reports deterministic Added, Removed, and Modified
  objects together with both project revisions and source locations. Modified entries include
  exact before/after property values; structural changes identify direct parent and sibling order,
  and each existing side carries ready-to-pass Workbench navigation arguments. Callers therefore
  need neither follow-up object reads nor manual launch-argument construction merely to explain
  and locate a change. It writes no files and
  rejects a comparison if either saved project changes while the result is being prepared.
  Object-kind filtering and offset/limit pagination keep large comparisons bounded; separate
  before/after revision guards keep continuation calls on the same pair of snapshots.
  `--require-equal` returns a dedicated differences-found status for shell and CI checks while
  retaining the normal diagnostic result.
  Its read-only `find` command
  resolves IDs from case-insensitive names, paths, addresses, offsets, tags, or descriptions and
  returns deterministic match ranks together with the property that matched. Optional `--exact`
  keeps only complete case-insensitive ID, name, path, or property-value matches, excluding prefix
  and substring results without changing the default search. Optional `--require-one` turns a
  zero- or multi-match lookup into a project error; ambiguous responses retain all candidates for
  disambiguation. `list` and `find`
  keep `--parent` as a direct-child filter by default; adding `--recursive` returns every
  descendant below that parent in one project read. They also accept an exact case-sensitive
  `--tag` filter plus `--offset`/`--limit`, and report total,
  returned, next-offset, and continuation metadata,
  allowing a host to page through a large map without loading every object at once. Continuation
  calls accept `--expect` with the first page's revision and fail before returning a mixed-snapshot
  page if the project changed. `summary` exposes the deterministic Tag catalog and per-Tag
  Register counts so a host can populate filters without scanning the hierarchy.
  `get <project>` returns the complete hierarchy and revision
  without a preliminary Workspace-ID lookup; `get ... --expect <revision>` also guarantees that
  a stable ID found by an earlier call is read from that same project snapshot or returns a
  no-write revision conflict. `get-many` reads several requested stable IDs from one project
  revision, preserves request order and duplicates, and retains found objects when other IDs are
  missing. Every successful list/find/get object and each found `get-many` item also carries
  a versioned, self-contained Workbench navigation descriptor with its absolute project path,
  stable ID, kind, hierarchy path, and ready-to-pass `--project/--select` arguments. Patches can
  set properties or add, deep-copy, move,
  or remove hierarchy objects, and may be read from a file or standard input.
  Copy returns a deterministic old-to-new descendant ID map; Move preserves the object and
  descendant stable IDs. Copy may use `new_id: "auto"` and `unique_name: true` to assign a
  collision-free derived ID family and the next Workbench-style sibling name without exposing
  those bookkeeping steps to a host. Add may use `id: "auto"` and add/copy/move `parent_id` may
  reference `{"operation": N}` from an earlier operation, allowing a host to create and place a
  complete Page/Block/Register/Field/Enum hierarchy in one atomic patch without managing
  intermediate IDs. The same reference form works for set/copy/move/remove targets and move
  ordering anchors, so later operations can modify, duplicate, relocate, order, or remove an
  automatically identified object in that same patch. Long patches may assign unique
  case-sensitive operation `ref` names and use `{"ref":"name"}` instead of fragile numeric
  indexes; each change result echoes the name. A rejected operation returns a structured
  zero-based index, JSON Pointer, original operation, completed-operation count, and explicit
  rollback/write flags, so an embedding host does not parse the human error sentence. Candidate
  validation and in-memory generation failures use the same failure member with diagnostic
  indexes, codes, object IDs, and truthful no-write/rollback flags. Real
  patches require the current revision; dry runs validate
  the complete candidate and preview all derivative outputs without writing. If a
  locked or unavailable output prevents regeneration during `generate` or after an `apply` or
  `init` model is saved, the JSON result identifies the revision-guarded `generate` recovery
  action and keeps any
  existing derivative file read-only. `generate
  --expect <revision>` and automatic before/after revision checks prevent stale artifacts from
  being reported as current when another process saves the project concurrently. Automation can
  reposition a Field by setting `lsb`; the operation preserves width, derives MSB, and retains
  the complete Field identity and hierarchy. Page/Block `move` operations accept `before_id` so
  external hosts can reproduce navigation-tree and generated-sheet/section ordering without
  recreating objects. A same-Block movable Register accepts `before_id` with the same Offset-slot
  exchange used by Workbench dragging; fixed Register offsets remain anchored and the response
  reports the moved Register's final Offset. A parseable project with existing validation errors
  can be repaired by one
  guarded patch; remaining errors or replacement Problems reject the complete patch without
  writing, while the response reports before/after, resolved, and introduced Problem counts.
  `--json --help`, `--json version`, and `--json --version` also return the same single-object
  JSON contract, so an embedding host never needs to parse free-form text during capability
  negotiation. Invoking the CLI without a command returns usage error `2` instead of a successful
  no-op; explicit `--help` remains successful. `help <command>` and `<command> --help` return
  focused usage, access, write behavior, and typed arguments without opening a project. Near-miss command names return a structured
  `suggested_command`; near-miss options return `suggested_option`, while invalid object kinds list
  the accepted values. Every JSON response also carries `exit_code` and a stable
  `exit_status`; failed
  responses add a primary `error_code`, so hosts can select usage, project, revision-conflict, or
  write recovery without parsing English error text. Block add/copy operations may use
  `base: "auto"` with a positive explicit or
  copied Size to select the lowest free Page-relative allocation. Register add/copy operations
  may use `offset: "auto"` to
  request Workbench-consistent four-byte-aligned placement: append when possible, otherwise use
  the earliest fitting gap.
  Field add/copy operations may use `lsb: "auto"` to select the lowest contiguous free range in a
  structure Register or compound Field. Both forms reject the complete patch when no range fits
  and never shift existing objects. Block/Register/Field moves accept `placement: "auto"` to
  select the corresponding free destination range while preserving all stable IDs; the response
  returns the final Base, Offset, or LSB/MSB directly.
- Workbench watches the project manifest as well as generated outputs. Every valid external
  `regmapc` or disk revision is retained as a SHA-256-identified, generation-tagged comparison;
  it never replaces even a clean Workbench model automatically. The Diff panel can preview and
  Accept or Reject selected changes or all changes. Accept expands required Page/Block/Register/
  Field/Enum dependencies, validates the complete candidate, and applies one undoable Workbench
  transaction without writing disk. Reject keeps Workbench values only for the observed digest;
  a newer disk revision invalidates those decisions and presents its changes again. Save & Sync
  remains paused until the user explicitly reloads the disk version or confirms the destructive
  Workbench overwrite action.
  Active editors, unsaved edits, invalid external files, and managed RTL conflicts remain visible
  and are never silently replaced.
- Problems rows navigate to their Workbench object before falling back to source navigation.
  `F8` and `Shift+F8` move to the next or previous Problem from anywhere in the window,
  reopen the Results area when needed, and wrap at either end. A successful location reports
  the current Problem position, diagnostic code, and message in the status bar. Keyboard
  browsing never starts an external application for a source-only output Problem; that row
  remains focused until the user explicitly activates it with Enter or a double-click. When
  validation refreshes, the current diagnostic, selected cells, and scroll position stay intact
  if they still exist; resolving the current diagnostic advances to the nearest remaining row
  instead of restarting the review from the beginning.
  Generated and Diff likewise preserve the current cell, selected cells, and scroll position
  while their status or change list refreshes. If a selected change is undone, Diff continues
  from the nearest remaining row; selection is never carried into a different project. Passive
  result refreshes do not redirect Copy or other selection commands away from the editor the
  user was working in. Activating a Removed Diff row explains that the object is no longer in
  the current model and identifies Undo (`Ctrl+Z`) as the recovery path instead of failing
  silently.
  Selecting a Diff row shows its object path and property-level **Before** / **After** values.
  Double-clicking a property or pressing Enter locates its editable cell when it still exists.
  Activating a Problem or Diff row, Favorite, or recent object places keyboard focus on the
  located Page, Block, Register, Field, or Enum value so editing can continue immediately.
  Problems and Diff are hidden when empty. The complete result area stays collapsed during normal
  editing and opens automatically only for Problems, conflicts, or output failures; **Results**
  or `Ctrl+J` reopens or hides it on demand. Both paths validate an active cell edit before
  changing the layout, so an invalid value remains rejected and the panel state stays unchanged.
  Internal Object ID and source columns stay hidden. Hovering an elided Problem message,
  generated path, or Diff summary shows its complete text. After a generated file opens,
  Workbench states whether it is synchronized, last-saved, externally changed, or retained
  after a failed generation, so an older derivative is not mistaken for the current model.
  Non-structure registers hide the Field editor. Initial Value remains visible in the default
  Register layout; the type-specific Range column and advanced Field columns are available from
  the corresponding **View** actions or the table-header context menu.

`regmap_core` owns the model, validation, persistence, synchronization, and generators.
`Csrio` owns the Qt editing experience, and `regmapc` exposes the same core to
automation and embedding hosts.

## Use

Create a Workbench-first project with **File > New Project**, or open an existing
`.regmap.yaml` project from **File > Open Project**. A single project file can also be dragged
from the file manager and dropped anywhere on the Workbench window. A new project starts with an
empty, valid workspace. With no project open, use the toolbar's **New Project** or **Open Project**
commands; the empty-state card provides guidance without duplicating those buttons. When a valid
recent project exists, the card retains its direct **Reopen** action. Click **+ First Register**
once to create the default Page, Register Block,
and first Register as one undoable edit. The hierarchy context menu remains available when explicit
Page or Block naming is required. The window title shows
the Workspace name together with the manifest's containing directory and file name, so separate
projects remain distinguishable in the task switcher. Project directories and filenames support
native Unicode characters and spaces across Workbench and `regmapc`.
New/Open validates and commits the active table editor before displaying a file chooser. If the
cell value is invalid, the chooser is not opened, the current project remains active, and the
status bar states the expected value. The file chooser starts in the current project's directory,
or the system Documents directory when no project is open, so switching projects does not begin
in the application installation folder. If a selected project cannot be created or opened, the
current project and its pending edits remain active; the failure identifies the target and the
first available cause. Successfully created, explicitly opened, or passed-at-startup projects
appear under
**File > Open Recent**, newest first. Selecting a missing entry removes only that entry and
confirms that the current project is unchanged; unsaved-edit protection runs before switching to
a valid recent project. An explicit **Open Project** failure also displays recovery information in
a modal error instead of relying on the status bar alone. Startup-open failures use the same modal
recovery feedback and are not added to recent projects. If the project file is created
successfully but a pre-existing managed RTL file is invalid, the new project still opens as a
recoverable blocked state instead of being misreported as a creation failure; fix the RTL and
invoke **Save & Sync**. An existing project can also be passed at startup:

```powershell
Csrio.exe "C:\projects\device.regmap.yaml"
Csrio.exe --project "C:\projects\device.regmap.yaml"
Csrio.exe --project "C:\projects\device.regmap.yaml" --select reg-status
```

Only one startup project is accepted. Missing `--project` values, unknown options, and additional
project paths are rejected explicitly instead of being ignored. Use `--` before a bare path that
begins with a hyphen. `--select <stable-id>` focuses the requested Workspace, Page, Block,
Register, Field, or Enum value and opens its containing context when needed; a missing target is
reported while the valid project remains open. `--help` and `--version` return without opening
the Workbench window.

**File > Reload from Disk** reloads a clean project directly. If Workbench has local edits, it
reports how many changes will be discarded, identifies the project file, and explains that the
local Undo history will be cleared. **Cancel** is the default and explicitly reports that the
unsaved edits were kept; disk reload occurs only after choosing **Discard and Reload**.

Closing the application or opening another project while Workbench has local edits shows the
pending change count and current project name. **Save** is the default, **Cancel** keeps the
current project and its Undo history, and **Discard** explains that the edits cannot be recovered
after the close or successful replacement. If Save & Sync is blocked, the application remains
open and the same edits remain pending.
If the application stops before this prompt can complete, the next project open detects a
different recovery draft. **Restore Draft** returns it to the editable Workbench state,
**Merge Draft** combines it with nonconflicting newer disk changes, **Discard Draft** removes it,
and **Not Now** leaves it available for a later open. If the same property changed in both places,
the dialog exposes **Use Disk for Conflicts** and **Use Draft for Conflicts** instead of choosing
silently.

```powershell
build\dev-debug\src\app\Csrio.exe examples\minimal\.regmap.yaml
```

Click a register or field cell once to select it; double-click an editable cell or press `F2` to
edit it. The editable tables end with a single `+` row. Press `Insert` while the hierarchy,
Register, Field, or Enum table has focus to add at that active level without scrolling to `+`.
Register creation requires an explicitly selected Block; Page and Workspace aggregate views do
not show a Register `+` row and never infer the first Block as a destination.
To insert between two existing registers, move
the pointer to their boundary within the leftmost 40 pixels of the register table, then click the
displayed `+`; other boundary areas remain inert. Before committing an insertion, Workbench
verifies every shifted Register against the Block Size and Page address range. If the result would
be invalid, all Offsets and the Undo history remain unchanged and the status message identifies
which capacity to adjust.
Select cells across two or more Register or Field rows and use the local **Batch Edit (N)**
button, context menu, or `Ctrl+Shift+E` for one batch edit, including a shared numeric Range.
Use `Ctrl+Shift+C` / `Ctrl+Shift+V` to copy
and paste complete Register or Field definitions; ordinary `Ctrl+C` / `Ctrl+V` continues to
operate on cell text. Ordinary Paste is disabled on calculated/action cells, read-only result
tables, or while the clipboard holds a complete Page/Block/Register/Field definition, preventing
its display label from being written as a value; the tooltip directs the user to the matching
full-object command. Selected Problems, Generated, and Diff cells remain copyable as tabular text.
Their right-click menus expose the same result-cell Copy command together with the relevant
**Locate** or generated-file actions; Paste remains visibly disabled because these tables are
read-only.
Open the containing Block, then start Register reordering from its
**Register** name cell; selected rows
move as one group. Contiguous rows can instead move with `Alt+Up` / `Alt+Down`. Use **Fix Address**
from the Register context menu
or `Ctrl+Alt+L` when an Offset must remain anchored while other Registers are reordered or shifted.
The colored address map above the table shows all Pages together, one lane per Page, with Block as
its smallest unit. Page span is the end of its last Block; a Block without configured Size uses its
Register span for display. Hovering a lane reports Page size and hovering a segment reports Block
offset, size, and absolute range. The common scale makes relative Page size visible; red Blocks
identify overlap or Page-range errors. The star and **Navigate** controls above the hierarchy manage Favorites and recent
objects; `Ctrl+K` opens navigation and `Ctrl+Alt+F` toggles the active favorite. Workspace Undo and
Redo leave a short status message naming the restored operation and its reverse shortcut.
Double-click a register **Tags** cell to open the tag selector: typing filters the drop-down,
clicking an existing tag selects it, and the adjacent `+` creates a tag only when its name is
unique. All selections made before the Tag popup closes remain immediately visible but form one
Undo step, so one `Ctrl+Z` restores the complete Tag edit. A Register created while **Tag Filter** is active inherits that tag and remains visible
in the filtered table. If an edit temporarily leaves the active Tag with no matches, the filter
stays selected so Undo/Redo restores the same view; choose **All tags** to clear it explicitly.
When a selected Block has no match, its empty state can add a Register that inherits the active
Tag in one action; **Clear Filter** remains available as the secondary action.
Search, Favorites, Problems, and address-map navigation keep the active Tag Filter when the
target Register already matches it; the filter is cleared with explicit feedback only when it
would otherwise hide the target.
Navigating to a Workspace, Page, or Block always retains the filter, including a zero-match view,
so the same Tag can be compared across hierarchy scopes without reselecting it.
When edits change an active search result set, the current matching object keeps its position;
Enter and F3 therefore continue to the next result instead of restarting at the first match.
After selecting a Register's **Tags**, **Access**, or **Details** cell, press Enter, Space, or F2
to open the same editor or details workspace available from the mouse. A single click still only
selects the cell, so copy and range selection remain available. Structure Registers show
**Open Fields (N)**; Boolean and enumeration Registers show **Open Enum (N)**. Selecting another
Register closes the previous details, and **Close Details** returns to the compact Register table.
While editing a Register or Field row, Tab and Shift+Tab move between usable cells and skip
read-only or hidden columns, unavailable Range, empty Fields actions, and the trailing add row.
The register context menu can convert a register to a red, bold reserved slot or delete
it while shifting subsequent offsets upward. When a Tag Filter hides following Registers, the
delete confirmation states how many hidden Registers will still shift and defaults to cancel.
The Edit-menu Register delete action and the Delete key use that same shift behavior and state it
before execution. After deletion, selection stays near the removed row using the next visible
filtered Register, then the previous visible Register; it never jumps to a hidden physical
neighbor. One Ctrl+Z restores both the Register and every shifted Offset and selects the restored
Register when it matches the active Tag Filter. Redo returns selection to the nearest surviving
visible Register.
Page and block properties are shown once in the
highlighted context bar above the register table.

Changing a scalar Register to Type `field` creates its first `NEW_FIELD` in the same undoable
edit. A register whose Type is `field` has an **Open Fields (N)** action in the **Details** column. The
button opens its bit-field diagram and Field table directly, including the trailing `+` for
further Fields; selecting an entire register row does not open this workspace. The Fields header
identifies the current register and provides **Close Details**. Search and Problems results that
target a Field open the corresponding Fields workspace automatically.
Field LSB is derived and read-only. Edit **Width** or **MSB**, or drag a top-level field in the
bit-field view. A click only selects the Field; movement starts after the pointer crosses the
system drag threshold, preventing a selection click from shifting its bit range. When the diagram has focus, Left/Right browse Fields from MSB to LSB,
Home/End select the outermost Fields, and Enter locates the current Field in the table.
Press Escape during a pointer drag to cancel it without changing any bits.
`Alt+Left` moves the current top-level Field one bit toward MSB, and
`Alt+Right` moves it one bit toward LSB for precise alignment. Every block shows its bit range
above it; the moving block is painted on top and
its MSB/LSB update continuously. On overlap, Workbench asks whether to trim the moving field or
the overlapping fields. The complete trimmed result is validated before it is committed; a move
that would invalidate an existing Reset, Enum value, numeric Range, access rule, or Member Field
is rejected without changing the model or Undo history. Field types may be selected from the
field context menu or from the Type cell's in-place list. The same editor accepts custom
`intN`/`uintN` widths. If trimming the overlapping side would completely remove a Field, the dialog
lists every Field that will be deleted, labels the destructive choice explicitly, and keeps
Cancel as the default. The confirmed move remains one undoable edit.
`int8`, `uint32`, and the other `intN`/`uintN` forms set signedness and width together. Numeric
ranges use **Minimum/Maximum**. Changing a scalar Field to `field` creates the first
`NEW_MEMBER` in the same undoable edit, so the compound Field is immediately valid. Further
members are added from the compound Field context menu. The final Member cannot be deleted
until another Member exists; Workbench reports how to recover instead of leaving an invalid
compound Field. Boolean fields retain the visible type `bool` and have implicit
`FALSE=0`/`TRUE=1` values.
Deleting a Field that owns Members, Enum values, Range bounds, a non-zero Reset, or descriptions
first reports the attached content and defaults to cancel. Cancellation explicitly reports that
the Field was kept, while a confirmed deletion remains one undoable edit. A simple Field without
attached content deletes directly and still supports `Ctrl+Z`, so the common operation does not
gain an unnecessary confirmation.
Changing a populated structure Register or compound Field back to a scalar type first reports
how many child Fields or Members will be removed and defaults to cancel. A confirmed conversion
is one undoable transaction, so `Ctrl+Z` restores the complete hierarchy. Multi-cell paste never
opens repeated destructive confirmations; it rejects these Type cells and directs the user to
confirm each conversion individually. Changing an Enum Register or Field to a non-enumeration
type uses the same protection, reports how many Enum values will be removed, and preserves all
values when cancelled.
Register types use the same in-place list and custom `intN`/`uintN` input. A register's
**Range** cell uses
`minimum .. maximum`; **Initial** is independent of **Reset**. For `enum` and `bool` registers,
enum values for the selected register are edited in the same table below the field table. Range
cells are enabled only for `intN`/`uintN` values (or invalid legacy data that still needs to be
cleared), and their in-place editors show the expected input form.
Changing a scalar Register to `enum` creates one `NEW_VALUE` for each distinct Initial and Reset
value; a Field conversion creates one for its effective Reset. If none exists, value zero is
created. Type conversion and all required Enum values are one undoable edit, so the object is
valid and immediately editable instead of first producing an empty-enumeration Problem.
**Add Enum Value** applies the same rule when an Enum list is empty. Structure and reserved
objects must first use the Type cell so destructive cleanup cannot bypass its confirmation.
Numeric Range bounds remain valid across Register Width, Field Width/MSB, and numeric Type
changes; an edit that would make a bound exceed the new width or signedness is rejected without
changing the model. Moving from a numeric Type to a non-numeric Type reports the number of Range
bounds that would be removed and defaults to cancel. A confirmed cleanup is one undoable
transaction, while multi-cell paste rejects the destructive Type cell. **Add Enum Value** also
requires the Type cell when conversion would remove a Range.

`Ctrl+S` validates and saves the project, synchronizes managed RTL, advances the merge baseline,
and regenerates all three read-only outputs. The persistent badge confirms whether the operation
completed, was blocked by Problems, stopped for an RTL conflict, or saved with an output failure.
The compact **XLSX** button in the main toolbar and **Open XLSX** in the File menu open the
generated workbook in one step. While Workbench has unsaved edits, the toolbar button uses a
warning color and the File action changes to **Open Last Saved XLSX**; both tooltips and result
feedback state that the opened workbook does not contain those edits. After an output failure,
the File action changes to **Open Existing XLSX** so an older workbook is never presented as the
current synchronized result. The compact toolbar label stays fixed so the global search remains
available in narrower windows.
The Generated view lists each output's path, result, and last update time. If Excel is holding the
XLSX file open, the model and RTL remain saved, the XLSX row reports failure, and **Retry outputs**
runs the output step again after the workbook is closed.
When an output is missing, double-clicking its Generated row reports the exact path and directs
the user to **Retry outputs** instead of attempting an invalid file launch. **Open File** is
disabled in that row's context menu while **Open Containing Folder** remains available when the
directory exists.
If managed RTL is invalid while Workbench has edits, the state badge explicitly reports that the
operation is blocked and the changes remain unsaved. The project file and synchronization baseline
are not advanced. The status message directs the user to fix RTL and invoke **Save & Sync** again;
the same pending Workbench edit is then saved without re-entry.

Use `Ctrl+F` to focus global search. Press Enter or `F3` for the next result and `Shift+F3` for the
previous result. Press `F8` or `Shift+F8` to cycle through Problems. Double-clicking a Problem or
Diff row selects the corresponding Workbench object.

The RTL file is created on the first successful synchronization. Numeric properties are exposed
as `localparam` declarations carrying `RMW:VALUE` metadata. Text and token properties are stored
in `RMW:OBJECT` JSON comments. Code outside the `RMW:BEGIN` and `RMW:END` markers is user-owned
and preserved. Saving a valid RTL edit triggers synchronization. Independent Workbench and RTL
changes merge automatically; competing edits to the same property appear in **Diff** and must be
resolved with **Keep Workbench changes** or **Use RTL changes** in the conflict bar.

XLSX, C header, and Markdown files are derivative views. Their filesystem permissions are set
read-only after generation, they are never imported, and any external changes are replaced by
the next successful generation. The workbook opens on an **Overview** worksheet containing one
row per Block, grouped by Page. Page base, mapped span, Block allocation, absolute address range,
and Register count can therefore be inspected and filtered without expanding detail rows. Each
Page also has a separate detail worksheet. Page base and address width appear once at the top;
each Block has a section band containing its base and size.
Register rows expose address, offset, type, width, access, initial/reset values,
tags, range or enum summary, and description. Structure registers can expand their initially
collapsed proportional bitfield diagram and field rows. Only the table header stays visible
while scrolling. Filters are confined to the flat Overview sheet so Block bands and collapsed
Field groups cannot be mistaken for data rows. There is no duplicate flat
`Registers` worksheet.

See [architecture.md](docs/architecture.md), [manifest-schema.md](docs/manifest-schema.md),
[cli.md](docs/cli.md), [rtl-sync.md](docs/rtl-sync.md), and
[excel-schema.md](docs/excel-schema.md) for the contracts.

## Suite application protocol

Csrio provides `regmap://project?...` resources through
`suite-app/v1`. A URI may identify the whole project or a stable object ID.
Register-focused navigation also accepts
`regmap://register/<stable-id>?file=<project>&field=<optional-field-id>`.
The optional `field` value must belong to the addressed Register; invalid
Register or Field IDs return an explicit provider error and never change the
open Workbench project.
The provider exposes `regmap.project.open` and the model Surface
`regmap.workbench`, while project loading and object lookup remain delegated to
the existing RegMap core.

The optional neutral Runtime owns discovery and routing only. It does not own
the register-map project or call `regmapc` through an application-specific
broker branch. If the Runtime is absent, Csrio and `regmapc` continue
to run as independent products.

## Configure, build, and test

Requirements are CMake 3.24 or newer, a C++20 compiler, Ninja, and Qt. The Ela backend
requires exactly Qt 6.10.2, including private headers; Classic and SuiteUi retain the
Qt 6.5 minimum. The compiler must match the Qt package. For a MinGW Qt installation:

```powershell
$env:PATH="C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;C:\Qt\Tools\CMake_64\bin;$env:PATH"
cmake --preset dev-debug -DCMAKE_PREFIX_PATH="C:\Qt\6.10.2\mingw_64"
cmake --build --preset dev-debug
ctest --preset dev-debug
```

The first configure obtains the pinned fallback dependencies when they are not installed.

### Qt Creator

Open the repository's top-level `CMakeLists.txt` with **File > Open File or Project**. Select a
Desktop Qt kit whose C and C++ compilers are configured and match that Qt installation. A kit
showing an empty `CMAKE_CXX_COMPILER` cannot configure the project; set the compiler under
**Preferences > Kits > Compilers**, then select it in the kit and run **Build > Reconfigure
Project**. Keep `REGMAP_BUILD_APP`, `REGMAP_BUILD_CLI`, and `REGMAP_BUILD_TESTS` enabled.

### Windows portable directory package

Csrio uses a clean directory staging for coordinated AppSuite publication.
Validate the test-enabled Release/ELA build, then configure a separate
Release directory with the same Qt/compiler/SuiteApp dependencies and
`REGMAP_UI_BACKEND=ELA` / `REGMAP_BUILD_TESTS=OFF`. Commit the validated sources and run:

```powershell
cmake -S . -B build/csrio-release -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.10.2\mingw_64" -DREGMAP_UI_BACKEND=ELA -DREGMAP_BUILD_TESTS=OFF
./tests/stage_directory_package.ps1 -BuildDirectory build/csrio-release
```

The script requires a clean checkout and Release/ELA/test-OFF cache, uses Qt's deployment
API, and creates `out/Csrio-<version>-<revision>-staging/Csrio`.
It refuses to overwrite existing staging. The directory contains both executables,
Qt/MinGW libraries and plugins, documentation, Ela patches/attribution, runtime
license notices, `BUILD-INFO.txt` with the full revision, and per-file `SHA256SUMS.txt`.
The directory smoke check uses an isolated profile/project and a Windows-only PATH
to verify Csrio version resources, executable version/help, and CLI init/validate/generate/current
contracts. Only `Csrio.exe` and `regmapc.exe` are allowed in the runtime, so an obsolete GUI
executable or a test binary cannot silently enter the package.
It does not operate the desktop mouse. The coordinating task owns replacement of
`AppSuite/Apps/Csrio` and shared manifests; this script does not write them.
The legacy archive target remains available for historical builds but is not used
for coordinated directory publication. Its current archive also contains one `Csrio` root.
Only the Ela backend is maintained; older backends are retained
for compatibility checks.

For a local preview from uncommitted sources, pass `-DevelopmentPreview`. The script creates
`out/Csrio-<version>-<revision>-dirty-preview-<id>/Csrio` and retains `Source state: dirty`
in `BUILD-INFO.txt`; it never relaxes the default clean-release requirement. To recheck that
preview independently, run `check_directory_package.ps1` with `-ExpectedSourceState dirty`.
The portable checker accepts the same explicit source-state option and a full commit SHA for
`-ExpectedRevision`; archive filenames use the first seven characters of that SHA.
`regmapc --json version` reports both its API version and supported project
schema versions before a host opens a project.

The checked-in [minimal project](examples/minimal/.regmap.yaml) is a complete schema-version-2
example.

### Control backends (Ela migration)

Fresh builds now select the pinned, vendored ElaWidgetTools backend. Select
`REGMAP_UI_BACKEND=ELA|SUITEUI|CLASSIC` at configure time; only one third-party
renderer is linked. Existing caches using `REGMAP_ENABLE_SUITEUI` keep their old
choice until `REGMAP_UI_BACKEND` is set explicitly. Version 0.3.2 used SuiteUi;
Windows portable releases use Ela starting with version 0.3.3.

Ela replaces command buttons, ordinary form inputs, menus, toolbars, result
tabs, scrollbars, batch-dialog OK/Cancel buttons, status bar, candidate lists,
read-only result tables, Type/Access editor controls and the window title bar.
Ordinary controls use Ela tooltips; successful saves and generation add transient
notifications without replacing persistent status or diagnostic text. Models,
delegate commit/cancel and validation contracts, specialized views, native file
pickers and destructive confirmations remain application-owned. The title bar
retains the unsaved-close guard and Windows resize/maximize behavior. Its source revision,
font license and compatibility patches are recorded under
`thirdparty/elawidgettools`; it does not depend on another application's checkout.
See [the migration record](docs/ela-migration.md) for scope and validation.

For `REGMAP_UI_BACKEND=SUITEUI`, point `SuiteUi_DIR` at an installed
`SuiteUi 0.1.0` package (`lib/cmake/SuiteUi`). Qt/compiler and configuration must
match the SDK's build-info.json. Core and CLI do not link either control backend.

Use `REGMAP_UI_STYLE=classic` at process startup for a local Fusion/QSS comparison.
The choice is fixed before widget creation; theme switches do not swap backends.
To remove third-party UI dependencies, configure `REGMAP_UI_BACKEND=CLASSIC` and
rebuild. Theme preferences and project data require no migration. Install rules
include the selected renderer's license and provenance notices. Native cross-DPI
and frame-pacing validation are distinct from the offscreen screenshot matrix.

Tests use temporary settings and apply the actual theme before GUI cases.
`REGMAP_TEST_THEME=light|dark`, `QT_SCALE_FACTOR`, `QT_REDUCE_MOTION` and
`REGMAP_UI_STYLE` select comparison conditions. `regmap_suiteui_control_test`
checks the real batch-edit and header workflows; `REGMAP_UI_ARTIFACT_DIR` optionally
retains screenshots, while `REGMAP_UI_REVIEW=1` opens a temporary fixture for five
minutes on the chosen Qt platform. Offscreen tests require an available font set.
