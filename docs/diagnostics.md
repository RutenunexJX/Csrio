# Diagnostic code ranges

Diagnostics carry a stable code, severity, message, object ID, and source location when one is
available.

| Range | Area |
|---|---|
| `RM1000`–`RM1099` | Project manifest structure and path contract |
| `RM1100`–`RM1199` | Workspace model serialization and atomic project writing |
| `RM3000`–`RM3099` | Identity, address, field, access, reset, enum, tag, reserved-slot, numeric-range, and compound-field validation |
| `RM4000`–`RM4099` | C header and Markdown symbol generation and file writing |
| `RM4100`–`RM4199` | XLSX export |
| `RM5000`–`RM5099` | Managed RTL parsing, markers, values, and writing |
| `RM5200`–`RM5299` | Synchronization baseline persistence |
| `RM5300`–`RM5399` | Workbench/RTL merge conflicts |

Messages are presentation text. Workbench logic and tests use `code`, `severity`, `object_id`,
and source coordinates rather than parsing messages.

`RM4004` reports a numeric range bound that cannot be emitted as a C `int64_t` or `uint64_t`
constant. C header generation parses bounds with the same rules as model validation, then
emits normalized `INT64_C` / `UINT64_C` expressions. Leading zeroes remain decimal, `0o`
prefixes are converted, and digit separators are removed. Invalid or unrepresentable bounds
produce a generation error instead of an invalid or truncated macro; source values are unchanged.
