# `.regmap.yaml` schema version 2

The project file persists the complete Workbench model and synchronization configuration. It is
maintained by the application. All paths are relative to the project file, must remain inside
the project directory, and are normalized before use.

```yaml
schema_version: 2

workspace:
  id: example-workspace
  name: Example Register Map
  address_spaces:
    - id: space-main
      name: Main
      base: 0x0
      address_width: 32
      description: Main register page.
      blocks:
        - id: block-control
          name: Control
          base: 0x0
          size: 0x1000
          description: Control registers.
          registers:
            - id: reg-control
              name: CONTROL
              offset: 0x0
              fixed: true
              width: 32
              type: field
              initial: 0x0
              reset: 0x0
              access: rw
              tags: [control, startup]
              description: Global control.
              enum_values: []
              fields:
                - id: field-enable
                  name: ENABLE
                  msb: 0
                  lsb: 0
                  type: bool
                  sw_access: rw
                  hw_access: ro
                  read_side_effect: none
                  write_side_effect: write
                  description: Enables the block.
                  enum_values: []
                - id: field-count
                  name: COUNT
                  msb: 7
                  lsb: 1
                  type: unsigned
                  minimum: 0
                  maximum: 100
                  sw_access: rw
                  hw_access: ro
                  read_side_effect: none
                  write_side_effect: write
                  enum_values: []

rtl:
  path: rtl/example_registers.sv
  module: example_registers

generation:
  output_directory: generated
  targets:
    - kind: xlsx
      path: register-map.xlsx
    - kind: c-header
      path: example_regs.h
      options:
        guard: EXAMPLE_REGS_H
    - kind: markdown
      path: register-map.md
      options:
        title: Example Register Map
```

## Model fields

All `id` values are non-empty, contain no whitespace, and are unique across the complete
workspace. Names are user-facing identifiers. Addresses are unsigned 64-bit values. Register
Reset and enum values are arbitrary-width unsigned values. Numeric scalars may use decimal or
hexadecimal notation.

- Page (serialized under `address_spaces`): `id`, `name`, `base`, `address_width`, `description`, `blocks`.
- Register block: `id`, `name`, `base`, optional `size`, `description`, `registers`.
- Register: `id`, `name`, `offset`, optional `fixed`, `width`, `type`, optional `minimum`, optional `maximum`,
  optional `initial`, optional `reset`, `access`, optional `reserved`, optional `tags`,
  `description`, `enum_values`, and `fields`.
  `fixed: true` keeps the Register Offset anchored during drag reorder and delete-with-shift;
  it is omitted for movable Registers.
  Register `enum_values` use the same stable-ID objects as field enum values. A reserved
  register has `type: reserved`, `access: none`, no fields, and a zero or absent reset while
  retaining its address slot. Every Register occupies one four-byte slot and its Offset must be
  four-byte aligned. Legacy `array.count` and `array.stride` input is accepted, normalized to one
  four-byte slot, and omitted on the next save.
- Field: `id`, `name`, `msb`, `lsb`, `type`, `sw_access`, `hw_access`,
  `read_side_effect`, `write_side_effect`, optional `minimum`, optional `maximum`,
  `description`, `enum_values`, and optional recursive `members`.
  A structure Register's Field Reset values are derived from the corresponding bits of the
  Register Reset. Legacy Field `reset` input is accepted only for migration and is not an
  independent persisted value. If a legacy structure Register has no Reset, valid Field resets
  are composed into a Register Reset, uncovered bits default to zero, and a migration warning is
  reported. If both forms exist and disagree, the Register Reset remains authoritative and the
  replacement is reported as a warning. Conflicting, overlapping, out-of-range, or width-invalid
  legacy Field resets cannot be migrated without an explicit Register Reset and prevent the
  project from being saved.
- Enum value: `id`, `name`, `value`, `description`.

A Block with `size` reserves the half-open Page-relative interval `[base, base + size)`.
Declared Block intervals must not overlap and their complete extent must fit the Page address
width. Registers must also remain inside their containing Block interval.

Legacy `reset_domain` keys are ignored when read and are omitted on the next save.

Accepted tokens:

- Access: `none`, `ro`, `wo`, `rw`.
- Register/field type: `bits`, `bool`, `unsigned`, `signed`, `enum`, `field`, `reserved`.
- Read side effect: `none`, `clear`, `set`.
- Write side effect: `none`, `write`, `w1c`, `w1s`, `w0c`, `w0s`, `toggle`.

Workbench accepts `intN` and `uintN` in register and field type editors and normalizes them to
`signed` or `unsigned` plus the object width. `minimum` and `maximum` are integer literals and
must fit that signedness and width. A `field` type is a compound container whose `members` use
bit positions relative to the container. A `bool` remains displayed as `bool`; when no explicit
enum values are stored, `FALSE=0` and `TRUE=1` are implicit.

## RTL and generation contract

`rtl.path` names the synchronized SystemVerilog file. `rtl.module` must be a valid
SystemVerilog identifier and is used when the application creates the file.

`generation.targets` must contain exactly one target of each kind: `xlsx`, `c-header`, and
`markdown`. Target paths are relative to `output_directory`, unique, and may not escape that
directory or overwrite the project or RTL file. The C header supports the scalar `guard` option;
Markdown supports `title`. XLSX has no options.

The synchronization baseline is stored beside the project as
`<project-file-name>.sync.json`. It is implementation state and is not part of the editable
project contract.
