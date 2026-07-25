# Managed RTL synchronization

The SystemVerilog file combines a Workbench-owned register-map region with user-owned RTL.

```systemverilog
module example_registers ();

  // RMW:BEGIN schema=1
  // RMW:OBJECT {"id":"reg-control", ...}
  localparam logic [0:0] RMW_REG_CONTROL_..._OFFSET = 1'h0;
  // ...
  // RMW:END

  // User-owned RTL may be placed here.
endmodule
```

## Ownership rules

- Exactly one complete `RMW:BEGIN schema=1` / `RMW:END` region is required.
- Workbench may replace the complete managed region during a successful save.
- Text outside the markers is preserved verbatim.
- An existing file without a managed region is rejected and never overwritten.
- Stable IDs and the `RMW:OBJECT` / `RMW:VALUE` metadata must remain present and unique.
- Stable IDs and object kinds are identity metadata, not editable values. Perform structural
  creation and deletion in Workbench.

## Editable values

Numeric model properties are emitted as SystemVerilog `localparam` literals followed by an
`RMW:VALUE` JSON marker. Decimal, binary, octal, and hexadecimal unsigned literals are accepted;
underscores are allowed. Unknown (`x`), high-impedance (`z`), and wildcard digits are rejected.

Text and token properties are held in the `properties` object of the corresponding
`RMW:OBJECT` JSON comment. Numeric `localparam` values take precedence over the duplicated JSON
property during parsing. Register and field types, register initial/reset values, tags, reserved
state, numeric range bounds, field reset domains, and recursive field-parent relationships are
synchronized through the same stable-ID object graph.

The managed region is a synchronization representation, not a protocol implementation. Register
logic and bus adaptation remain user-owned RTL outside the markers.

## Three-way merge

The application compares the baseline, current Workbench model, and parsed RTL by stable ID and
property:

- one-sided or equal edits merge automatically;
- edits to different properties of the same object merge automatically;
- different edits to the same property create an explicit conflict;
- malformed or invalid RTL leaves the current Workbench model unchanged.

The **Diff** tab reports base, Workbench, and RTL values for conflicts. Choosing **Resolve
Conflicts Using Workbench** or **Resolve Conflicts Using RTL** applies that preference to the
current conflict set, validates the result, and persists a new synchronized baseline.
