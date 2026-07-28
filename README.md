# Register Map Workbench

Register Map Workbench is a standalone Qt desktop application for FPGA and SystemVerilog
register-map development. Workbench and a controlled region in SystemVerilog RTL are the two
editable endpoints. One internal model keeps them synchronized and produces read-only XLSX,
C header, and Markdown views.

```text
Workbench <-> internal model <-> managed SystemVerilog RTL
                         |
                         +-> XLSX / C header / Markdown (read-only)
```

The model hierarchy is:

```text
Workspace -> Page (Address Space) -> Register Block -> Register -> Field -> Enum Value
                                                               -> Member Field
```

## Current capabilities

- Workbench-first creation and editing of every model level, with stable object IDs.
- Editable hierarchy, register and field tables, bit-field view, page/block context bar, and
  inline enum-value table.
- Hierarchy context menus create Pages, Register Blocks, and Registers at their natural parent;
  rename, delete, expand, and collapse remain local to the navigation pane.
- Structure-register rows expose a dedicated **Open (N)** Fields button.
- Trailing `+` rows for register/field creation; page and block base addresses appear once in
  the context bar above the register table and remain directly editable.
- New Blocks choose the first free Base for the preferred 4 KiB allocation, or the largest
  smaller power-of-two allocation that fits the Page. New 32-bit Registers use the next
  four-byte-aligned Offset. If no valid address range remains, Workbench leaves the model and
  Undo history unchanged and reports which capacity must be adjusted.
- Page and Register Block copy/paste and drag movement stay in the hierarchy. A pasted Block
  keeps its Base when free and otherwise uses the first non-overlapping Base that fits the
  destination Page. A cross-Page Block move preserves its Base and is rejected before changing
  the model when that Base, Page width, or Block name conflicts at the destination.
- Reserved address slots and delete-with-offset-shift operations from the register context menu.
  Converting a populated Register to Reserved first reports the Fields, Enum values, Range
  bounds, non-zero Initial/Reset values, and Description that will be removed or replaced; the
  confirmation defaults to cancel and a confirmed conversion remains one undoable edit.
  Delete-with-shift previews the complete result before asking for confirmation; it leaves the
  model unchanged when a following Offset would underflow or the shifted layout would introduce
  an address conflict.
- Multi-tag register assignment with existing-tag selection, new-tag creation, and tag filtering.
- Single-click cell selection and double-click/F2 editing for register and field properties,
  including register type, value range, initial value, and reset value.
- Editable field width with derived read-only LSB, live MSB/LSB labels, top-layer bit-view
  dragging, and explicit overlap resolution by trimming either the moving field or the fields
  it covers.
- Boolean/enumeration, `intN`/`uintN` numeric ranges, and compound fields with member fields.
- Continuous validation of identity, address ranges and overlap, field ranges and overlap,
  declared Block allocation ranges, access and side-effect combinations, reset values, and
  enum values.
  A pre-existing invalid layout can be repaired incrementally, but each accepted edit must leave
  only diagnostics that already existed before that edit; reducing the count by replacing old
  Problems with a different Problem is rejected.
- Undo, redo, dirty-state tracking, atomic project save, and close-time save protection.
- One visible **Save & Sync** action and a persistent state badge for unsaved, synchronizing,
  synchronized, blocked, partial-output-failure, and conflict states.
- Per-output status and update time for XLSX, C header, and Markdown, with a direct retry action.
- Global `Ctrl+F` search across pages, blocks, register/field names, addresses, tags, enum values,
  and descriptions; Enter/F3 cycles results and selects the matching Workbench object.
- Spreadsheet-style cell selection plus tab-separated copy and paste for editable table cells.
- Managed RTL generation and reverse synchronization through an explicitly marked region.
- Stable-ID, property-level three-way merge using the last synchronized model as the base.
- Automatic merge for independent changes and explicit Workbench/RTL conflict resolution from
  the Diff panel; no conflicting file is overwritten before the user chooses a side.
- Deterministic, read-only XLSX, C header, and Markdown generation. XLSX uses one worksheet per
  Page, separates Blocks with section bands, and embeds collapsible bitfield details only for
  structure registers.
- Problems rows navigate to their Workbench object before falling back to source navigation.
  Problems and Diff are hidden when empty; non-structure registers hide the Field editor, and
  low-frequency Field columns are available from **View > Show Advanced Field Columns**.

`regmap_core` owns the model, validation, persistence, synchronization, and generators.
`RegMapWorkbench` owns the Qt editing experience.

## Use

Create a Workbench-first project with **File > New Project**, or open an existing
`.regmap.yaml` project from **File > Open Project**. A new project starts with an empty,
valid workspace. Right-click the Workspace to create a Page, right-click the Page to create a
Register Block, and right-click the Register Block to create a Register.
An existing project can also be passed at startup:

```powershell
build\dev-debug\src\app\RegMapWorkbench.exe examples\minimal\.regmap.yaml
```

Click a register or field cell once to select it; double-click an editable cell or press `F2` to
edit it. Both tables end with a single `+` row. To insert between two existing registers, move
the pointer to their boundary within the leftmost 40 pixels of the register table, then click the
displayed `+`; other boundary areas remain inert. Before committing an insertion, Workbench
verifies every shifted Register against the Block Size and Page address range. If the result would
be invalid, all Offsets and the Undo history remain unchanged and the status message identifies
which capacity to adjust.
Double-click a register **Tags** cell to open the tag selector: typing filters the drop-down,
clicking an existing tag selects it, and the adjacent `+` creates a tag only when its name is
unique. The register context menu can convert a register to a red, bold reserved slot or delete
it while shifting subsequent offsets upward. Page and block properties are shown once in the
highlighted context bar above the register table.

Changing a scalar Register to Type `field` creates its first `NEW_FIELD` in the same undoable
edit. A register whose Type is `field` has an **Open (N)** button in the **Fields** column. The
button opens its bit-field diagram and Field table directly, including the trailing `+` for
further Fields; selecting an entire register row does not open this workspace. The Fields header
identifies the current register and provides **Close Fields**. Search and Problems results that
target a Field open the corresponding Fields workspace automatically.
Field LSB is derived and read-only. Edit **Width** or **MSB**, or drag a top-level field in the
bit-field view. Every block shows its bit range above it; the moving block is painted on top and
its MSB/LSB update continuously. On overlap, Workbench asks whether to trim the moving field or
the overlapping fields. The complete trimmed result is validated before it is committed; a move
that would invalidate an existing Reset, Enum value, numeric Range, access rule, or Member Field
is rejected without changing the model or Undo history. Field types may be selected from the
field context menu;
`int8`, `uint32`, and the other `intN`/`uintN` forms set signedness and width together. Numeric
ranges use **Minimum/Maximum**. Changing a scalar Field to `field` creates the first
`NEW_MEMBER` in the same undoable edit, so the compound Field is immediately valid. Further
members are added from the compound Field context menu. The final Member cannot be deleted
until another Member exists; Workbench reports how to recover instead of leaving an invalid
compound Field. Boolean fields retain the visible type `bool` and have implicit
`FALSE=0`/`TRUE=1` values.
Changing a populated structure Register or compound Field back to a scalar type first reports
how many child Fields or Members will be removed and defaults to cancel. A confirmed conversion
is one undoable transaction, so `Ctrl+Z` restores the complete hierarchy. Multi-cell paste never
opens repeated destructive confirmations; it rejects these Type cells and directs the user to
confirm each conversion individually. Changing an Enum Register or Field to a non-enumeration
type uses the same protection, reports how many Enum values will be removed, and preserves all
values when cancelled.
Register types use the same value-type vocabulary. A register's **Range** cell uses
`minimum .. maximum`; **Initial** is independent of **Reset**. For `enum` and `bool` registers,
enum values for the selected register are edited in the same table below the field table.
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
The Generated view lists each output's path, result, and last update time. If Excel is holding the
XLSX file open, the model and RTL remain saved, the XLSX row reports failure, and **Retry outputs**
runs the output step again after the workbook is closed.

Use `Ctrl+F` to focus global search. Press Enter or `F3` for the next result and `Shift+F3` for the
previous result. Double-clicking a Problem or Diff row selects the corresponding Workbench object.

The RTL file is created on the first successful synchronization. Numeric properties are exposed
as `localparam` declarations carrying `RMW:VALUE` metadata. Text and token properties are stored
in `RMW:OBJECT` JSON comments. Code outside the `RMW:BEGIN` and `RMW:END` markers is user-owned
and preserved. Saving a valid RTL edit triggers synchronization. Independent Workbench and RTL
changes merge automatically; competing edits to the same property appear in **Diff** and must be
resolved with **Keep Workbench changes** or **Use RTL changes** in the conflict bar.

XLSX, C header, and Markdown files are derivative views. Their filesystem permissions are set
read-only after generation, they are never imported, and any external changes are replaced by
the next successful generation. The workbook opens on the first Page worksheet. Page base and
address width appear once at the top; each Block has a section band containing its base and size.
Register rows expose address, offset, type, width, access, initial/reset values,
tags, range or enum summary, and description. Structure registers can expand their initially
collapsed proportional bitfield diagram and field rows. Only the table header stays visible
while scrolling, and the table includes Excel filters. There is no duplicate flat
`Registers` worksheet.

See [architecture.md](docs/architecture.md), [manifest-schema.md](docs/manifest-schema.md),
[rtl-sync.md](docs/rtl-sync.md), and [excel-schema.md](docs/excel-schema.md) for the contracts.

## Configure, build, and test

Requirements are CMake 3.24 or newer, a C++20 compiler, Ninja, and Qt 6.5 or newer. The compiler
must match the Qt package. For a MinGW Qt installation:

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
Project**. Keep `REGMAP_BUILD_APP` and `REGMAP_BUILD_TESTS` enabled.

The checked-in [minimal project](examples/minimal/.regmap.yaml) is a complete schema-version-2
example.
