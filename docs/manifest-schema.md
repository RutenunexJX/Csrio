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
      description: Main bus address space.
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
              width: 32
              type: field
              array:
                count: 1
                stride: 0x4
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
                  reset: 0x0
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
                  reset: 0x0
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
workspace. Names are user-facing identifiers. Addresses and strides are unsigned 64-bit values.
Reset and enum values are arbitrary-width unsigned values. Numeric scalars may use decimal or
hexadecimal notation.

- Address space (shown as a Page): `id`, `name`, `base`, `address_width`, `description`, `blocks`.
- Register block: `id`, `name`, `base`, optional `size`, `description`, `registers`.
- Register: `id`, `name`, `offset`, `width`, `type`, optional `minimum`, optional `maximum`,
  optional `initial`, optional `reset`, `access`, optional `reserved`, optional `tags`,
  `description`, `enum_values`, `fields`, and compatibility-only `array.count` / `array.stride`.
  Register `enum_values` use the same stable-ID objects as field enum values. A reserved
  register has `type: reserved`, `access: none`, no fields, and a zero or absent reset while
  retaining its address slot. Array count and stride remain serialized for backward
  compatibility but are not exposed in Workbench or XLSX.
- Field: `id`, `name`, `msb`, `lsb`, `type`, `sw_access`, `hw_access`, optional `reset`,
  `read_side_effect`, `write_side_effect`, optional `minimum`, optional `maximum`,
  `description`, `enum_values`, and optional recursive `members`.
- Enum value: `id`, `name`, `value`, `description`.

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
