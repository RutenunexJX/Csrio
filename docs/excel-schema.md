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

- row 1: `Page - <Page name>`;
- row 2: Page Base, Address Width, and Page description;
- row 3: visual spacer;
- row 4: filterable column headers;
- row 5 onward: Block bands and register groups.

Each Block starts with a full-width merged band in column A. The band records the Block name, base,
size, and description once. Block bands use saturated, high-contrast background colors with white
text and cycle through distinct colors. Register rows below them use these columns:

1. Address
2. Offset
3. Name
4. Type
5. Width / Bits
6. Access
7. Initial Value
8. Reset Value
9. Tags
10. Range
11. Description

Register groups use alternating background colors. Reserved registers use a gray row with a
bold red `[RESERVED]` label. Only registers whose type is `field` include detail rows: the first
detail row is a static proportional bitfield diagram. Each field block has its numeric MSB and LSB
labels directly above its two ends. Field rows and recursive member rows follow the diagram, use
one consistent warm-yellow background with dark-brown text, and store the field name without an
indentation symbol. Their Access column shows software/host access only. These rows are grouped and
initially collapsed. Scalar registers remain a single row. Description cells are left-aligned; all
other table cells are centered.

Rows 1 through 4 are frozen so the row-4 table header remains visible; no columns are frozen.
AutoFilter covers columns A through K. Addresses, offsets, Block bases and sizes, and initial/reset
values are fixed-width hexadecimal text in a monospaced font, so Excel cannot convert or round them.
