# Minimal example

Open `.regmap.yaml` in Csrio. The project contains a complete editable model.

Edit it through the page/block hierarchy, context bar, register and field tables, bit view, and
inline enum table, then save. The tables' final `+` rows create registers and fields. Register
context actions manage tags, reserved slots, and delete-with-offset-shift; fields can be resized
or dragged in the bit view.
The first
successful synchronization creates `rtl/minimal_registers.sv`; later numeric changes inside its
managed region synchronize back to Workbench. User code outside the managed markers is
preserved.

The generated XLSX, C header, and Markdown views are written to `generated/`. They are read-only
derivatives and are not version-controlled.
