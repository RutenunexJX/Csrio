# Generated XLSX contract

The XLSX workbook is an output-only presentation of the synchronized register model. Workbench
never imports it and does not observe Excel save events. The generated file is marked read-only;
applications that bypass this protection may change it, but the next generation replaces those
changes.

The workbook uses the same visual theme as Workbench. Its first worksheet is `Overview`, followed
by one worksheet per Page. Page names are converted to valid, unique Excel sheet names and limited
to 31 characters. `Overview` is selected when the workbook opens. There is no separate flat
`Registers` worksheet.

## Overview worksheet

The overview is the only filterable sheet. It contains one row per Block and exposes Page name,
Page Base, Address Width, mapped Page span, Block count, Block name, Block Base, Block Size,
absolute start/end addresses, and Register count. A Page without Blocks still has one summary row.
This flat layout permits ordinary Excel sorting and filtering without including merged Block bands,
bitfield diagrams, or collapsed Field rows in the filter range.

## Page worksheet

Each worksheet normally has the following layout:

- row 1: `Page - <Page name>`;
- row 2: Page Base, Address Width, and Page description;
- row 3: visual spacer;
- row 4: column headers;
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
red `[RESERVED]` label. Only registers whose type is `field` include detail rows: the first
detail row is a static proportional bitfield diagram. Each field block has its numeric MSB and LSB
labels directly above its two ends. Field rows and recursive member rows follow the diagram, use
one consistent warm-yellow background with dark-brown text, and store the field name without an
indentation symbol. Their Access column shows software/host access only. These rows are grouped and
initially collapsed. Scalar registers normally remain a single row. Description and Range cells,
including page/block descriptions, are left/top-aligned for long text. Other table cells are
centered horizontally and vertically, including names, types, bits, access and numeric values.
Body cells continue to wrap; hexadecimal address/value cells retain their monospaced font.

Rows through the table header are frozen (normally rows 1 through 4); no columns are frozen.
Page detail sheets do not use AutoFilter because their Block bands, diagrams, and Field groups are
not a flat record set. Addresses, offsets, Block bases and sizes, and initial/reset values are
fixed-width hexadecimal text in a monospaced font, so Excel cannot convert or round them.


## Text layout and row heights

CLI and GUI exports use the same core layout. Body text uses regular Microsoft YaHei at 10 pt;
only titles and header labels use bold. Description and Range columns receive more space, while
short type, bit-width and access columns remain compact. No text is shortened and no font is
shrunk to fit a fixed row height.

Row heights are measured from each cell's actual font, column width, mixed Chinese/Latin wrapping,
unbroken tokens and explicit line breaks, including empty lines. Measurement uses a fixed 96-DPI
device and retains cell padding and a wrap/line-spacing tolerance. The tallest cell determines
the logical row's height. Horizontally merged page/block descriptions use their combined width.

When content needs more than Excel's 409.5 pt physical-row limit, the logical row spans vertically
merged cells over multiple physical rows. Its complete text stays in the top cell. This can move
subsequent row numbers; frozen headers, field outline groups and enum hyperlinks use the resulting
positions. The workbook's data model and enum definitions are unchanged. `enum_tables: consecutive`
continues to link later fields to the first field's Range cell, including when that cell is merged.
