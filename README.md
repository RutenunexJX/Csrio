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
- Trailing `+` rows for register/field creation; page and block base addresses appear once in
  the context bar above the register table and remain directly editable.
- Reserved address slots and delete-with-offset-shift operations from the register context menu.
- Multi-tag register assignment with existing-tag selection, new-tag creation, and tag filtering.
- Single-click cell editing for register and field properties, including register type, value
  range, initial value, reset value, and reset domain.
- Editable field width with derived read-only LSB, live MSB/LSB labels, top-layer bit-view
  dragging, and explicit overlap resolution by trimming either the moving field or the fields
  it covers.
- Boolean/enumeration, `intN`/`uintN` numeric ranges, and compound fields with member fields.
- Continuous validation of identity, address ranges and overlap, field ranges and overlap,
  access and side-effect combinations, reset values, and enum values.
- Undo, redo, dirty-state tracking, atomic project save, and close-time save protection.
- Managed RTL generation and reverse synchronization through an explicitly marked region.
- Stable-ID, property-level three-way merge using the last synchronized model as the base.
- Automatic merge for independent changes and explicit Workbench/RTL conflict resolution.
- Deterministic, read-only XLSX, C header, and Markdown generation. The XLSX Register Map embeds
  each register's static bitfield diagram and field tree as collapsible rows directly below its
  register row.
- Problems, Generated, and Diff views with source navigation.

`regmap_core` owns the model, validation, persistence, synchronization, and generators.
`RegMapWorkbench` owns the Qt editing experience.

## Use

Create a Workbench-first project with **File > New Project**, or open an existing
`.regmap.yaml` project from **File > Open Project**. A new project starts with an empty,
valid workspace; add the first page and register block from the **Edit** menu.
An existing project can also be passed at startup:

```powershell
build\dev-debug\src\app\RegMapWorkbench.exe examples\minimal\.regmap.yaml
```

Click an editable register or field cell once to edit it. Both tables end with a single `+` row.
To insert between two existing registers, move the pointer to their boundary within the leftmost
40 pixels of the register table, then click the displayed `+`; other boundary areas remain inert.
Click a register **Tags** cell once to open the tag selector: typing filters the drop-down,
clicking an existing tag selects it, and the adjacent `+` creates a tag only when its name is
unique. The register context menu can convert a register to a red, bold reserved slot or delete
it while shifting subsequent offsets upward. Page and block properties are shown once in the
highlighted context bar above the register table.

Field LSB is derived and read-only. Edit **Width** or **MSB**, or drag a top-level field in the
bit-field view. Every block shows its bit range above it; the moving block is painted on top and
its MSB/LSB update continuously. On overlap, Workbench asks whether to trim the moving field or
the overlapping fields. Field types may be selected from the field context menu;
`int8`, `uint32`, and the other `intN`/`uintN` forms set signedness and width together. Numeric
ranges use **Minimum/Maximum**. A `field` type can contain member fields added from its context
menu. Boolean fields retain the visible type `bool` and have implicit `FALSE=0`/`TRUE=1` values.
Register types use the same value-type vocabulary. A register's **Range** cell uses
`minimum .. maximum`; **Initial** is independent of **Reset**. For `enum` and `bool` registers,
enum values for the selected register are edited in the same table below the field table.

`Ctrl+S` validates and saves the project, synchronizes managed RTL, advances the merge baseline,
and regenerates all three read-only outputs.

The RTL file is created on the first successful synchronization. Numeric properties are exposed
as `localparam` declarations carrying `RMW:VALUE` metadata. Text and token properties are stored
in `RMW:OBJECT` JSON comments. Code outside the `RMW:BEGIN` and `RMW:END` markers is user-owned
and preserved. Saving a valid RTL edit triggers synchronization. Independent Workbench and RTL
changes merge automatically; competing edits to the same property appear in **Diff** and must be
resolved with one of the two conflict-resolution actions.

XLSX, C header, and Markdown files are derivative views. Their filesystem permissions are set
read-only after generation, they are never imported, and any external changes are replaced by
the next successful generation. The workbook opens on **Register Map**, where light-blue
register group rows can expand their initially collapsed field rows, including bit range, type,
SW/HW access, reset, numeric range, and enum summary. The expanded group begins with a
proportional bitfield diagram showing MSB/LSB orientation, named fields, bit ranges, reserved
fields, and unassigned space. The workbook contains only **Register Map** and the flat
**Registers** view.

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
