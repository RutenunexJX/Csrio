# Generated XLSX contract

The XLSX workbook is an output-only presentation of the synchronized register model. Workbench
never imports it and does not observe Excel save events. The generated file is marked read-only;
applications that bypass this protection may change it, but the next generation replaces those
changes.

The workbook uses the same visual theme as Workbench and contains one worksheet per Page. Page
names are converted to valid, unique Excel sheet names and limited to 31 characters. The first
Page is selected when the workbook opens. There is no separate flat `Registers` worksheet.

## Page worksheet

Each worksheet has the following fixed layout:

- row 1: workbook and Page title;
- row 2: Page Base, Address Width, and Page description;
- row 3: visual spacer;
- row 4: filterable column headers;
- row 5 onward: Block bands and register groups.

A Block band records the Block name, base, size, and description once. Register rows below it use
these columns:

1. Address
2. Offset
3. Register / Field
4. Type
5. Width / Bits
6. Access
7. Initial
8. Reset
9. Tags
10. Range / Enum
11. Description

Register groups use alternating background colors. Reserved registers use a gray row with a
bold red `[RESERVED]` label. Only registers whose type is `field` include detail rows: the first
detail row is a static proportional bitfield diagram, followed by indented fields and recursive
member fields. These rows are grouped and initially collapsed. Scalar registers remain a single
row. Description cells are left-aligned; all other table cells are centered.

Rows 1 through 4 and columns A through C are frozen. AutoFilter covers columns A through K.
Addresses, offsets, Block bases and sizes, and initial/reset values are fixed-width hexadecimal
text in a monospaced font, so Excel cannot convert or round them.
