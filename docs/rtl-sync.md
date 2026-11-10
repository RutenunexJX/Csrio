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
`RMW:VALUE` JSON marker. Decimal, binary, octal, and hexadecimal literals are accepted.
Declarations may span lines, including around `=` and before `;`. Ordinary line and block
comments may separate tokens. Keep each `// RMW:VALUE {...}` marker on one line, either
within its declaration or after its terminating semicolon (on that line or a following
comment line), before the next declaration or `RMW:OBJECT`. Each marker must identify one
existing numeric property and appear exactly once. Missing semicolons, multiple assignments,
duplicate markers and unsupported marked declarations produce `RM5003`; they never silently
fall back to the older `RMW:OBJECT` value. Diagnostics retain the marker's physical line.
For based literals, a positive decimal size before the apostrophe determines the value's bit
width: excess high bits are discarded and shorter values are zero-extended. For example,
`8'h1ff`, `8'b111111111`, `8'o777`, and `8'd511` all import as 255; `16'hff` also imports as 255.
Sizes and digits may contain underscores after their first digit. Values wider than 64 bits
are parsed before truncation, so `8'h1_0000_0000_0000_00ff` still imports as 255. A large declared
size does not allocate padding for zero extension.

The optional `s`/`S` marker is honored after truncation. Non-negative results are accepted
(`16'shff` is 255); negative results (`8'shff` or `8'sd255`) cannot be represented by these
unsigned model properties and are rejected. Unsized signed binary, octal, and hexadecimal
literals use at least 32 bits; unsized signed decimal literals retain a sign bit. Unknown
(`x`), high-impedance (`z`), wildcard digits, zero/invalid sizes, expressions, and unbased
fill literals such as `'1` are unsupported and produce `RM5003` with the literal and source
line. Decimal values and the existing Csrio `0x`/`0b`/`0o` numeric forms remain supported.

The resulting value must also fit the target model property. A 65-bit address that still
needs 65 bits after truncation is rejected, as are reset values outside their register width.
The importer reads the literal, not arbitrary SystemVerilog expressions or assignment casts.
Invalid RTL leaves the current model and local edits intact; correct the literal and retry
synchronization. Re-export emits a canonical literal with the preserved numeric value.

Text and token properties are held in the `properties` object of the corresponding
`RMW:OBJECT` JSON comment. Numeric `localparam` values take precedence over the duplicated JSON
property during parsing. Register and field types, register initial/reset values, tags, reserved
state, numeric range bounds, and recursive field-parent relationships are
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
