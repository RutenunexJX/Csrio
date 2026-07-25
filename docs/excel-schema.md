# Generated XLSX contract

The XLSX workbook is an output-only presentation of the synchronized register model. Workbench
never imports it and does not observe Excel save events. The generated file is marked read-only;
applications that bypass this protection may change it, but the next generation replaces those
changes.

The workbook uses the same visual theme as Workbench and contains two sheets. Headers are on
row 3, review-oriented column widths are applied, and internal identity columns are hidden.

## `Registers`

One row per register:

- Page, Page Base, Block, Block Base, Register
- Absolute Address, Offset, Width, Type, Range or Enum Values, Initial
- Reset, Access, State, Tags, Description
- hidden Register ID and Block ID

## `Register Map`

The default review sheet combines registers and fields so ordinary review does not require
switching worksheets:

- each light-blue group row contains register address, page, block, width, type, access,
  initial value, reset, range, tags, and description;
- the first detail row contains a static proportional bitfield diagram with MSB/LSB orientation,
  field names and ranges, reserved fields, and unassigned space;
- subsequent indented rows contain that register's fields and recursive member fields;
- the diagram and field rows are grouped and initially collapsed; Excel's outline `+`/`-`
  controls expand or collapse each register's complete field detail;
- field rows contain absolute bit range, type, SW/HW access, reset, reset domain, numeric range
  or enum summary, and description.

Values that must remain exact are emitted as hexadecimal text rather than floating-point Excel
numbers. Hidden IDs support traceability only; they do not make the workbook editable input.
