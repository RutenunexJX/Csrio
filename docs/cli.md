# `regmapc` command contract

`regmapc` provides a non-interactive interface to the same project model, validation, persistence,
and read-only output generators used by Register Map Workbench. It does not modify generated XLSX,
C header, or Markdown content directly, and it exposes no RTL editing command.

Use `--json` for automation. It may appear anywhere before the optional `--` argument
terminator. Place `--` immediately before a positional value that itself begins with `--`;
tokens after it are data rather than global options.

```powershell
regmapc --json schema
regmapc --json version
regmapc --json help status
regmapc --json init device.regmap.yaml --name "Device Register Map"
regmapc --json summary device.regmap.yaml
regmapc --json list device.regmap.yaml --kind register --parent block-control --tag control --limit 100
regmapc --json list device.regmap.yaml --kind register --parent page-main --recursive
regmapc --json list device.regmap.yaml --kind register --offset 100 --limit 100 --expect sha256:...
regmapc --json find device.regmap.yaml control --kind register --tag control --limit 20
regmapc --json find device.regmap.yaml 0x43C00020 --exact --kind register
regmapc --json find device.regmap.yaml CONTROL --exact --require-one --kind register
regmapc --json find device.regmap.yaml interrupt --kind field --parent page-main --recursive
regmapc --json find device.regmap.yaml control --kind register --offset 20 --limit 20 --expect sha256:...
regmapc --json find device.regmap.yaml -- --option-like-name
regmapc --json get device.regmap.yaml
regmapc --json get device.regmap.yaml reg-control --expect sha256:...
regmapc --json get-many device.regmap.yaml reg-control field-enable reg-status --expect sha256:...
regmapc --json validate device.regmap.yaml
regmapc --json diff baseline.regmap.yaml device.regmap.yaml
regmapc --json diff baseline.regmap.yaml device.regmap.yaml --kind register --limit 100
regmapc --json diff baseline.regmap.yaml device.regmap.yaml --require-equal
regmapc --json status device.regmap.yaml
regmapc --json status device.regmap.yaml --require-current
regmapc --json generate device.regmap.yaml --dry-run --expect sha256:...
regmapc --json generate device.regmap.yaml --expect sha256:...
```

## Commands

| Command | Purpose | Writes files |
| --- | --- | --- |
| `help [command]` | Read focused usage and typed arguments without opening a project | No |
| `version` | Read CLI and JSON API versions without opening a project | No |
| `schema` | Discover commands, writable properties, patch shape, and exit codes | No |
| `init <project> [options]` | Create an empty Workbench project and its read-only outputs | Yes |
| `summary <project>` | Read project identity, counts, register tags, generation targets, and revision | No |
| `list <project> [--kind K] [--parent ID [--recursive]] [--tag TAG] [--offset N] [--limit N] [--expect REV]` | List stable IDs and hierarchy paths | No |
| `find <project> <query> [--exact] [--require-one] [--kind K] [--parent ID [--recursive]] [--tag TAG] [--offset N] [--limit N] [--expect REV]` | Find stable IDs by user-facing clues | No |
| `get <project> [stable-id] [--expect REV]` | Read the complete Workspace or one object from an optional required revision | No |
| `get-many <project> <stable-id>... [--expect REV]` | Read multiple objects in request order from one revision | No |
| `validate <project>` | Validate the complete project | No |
| `diff <before-project> <after-project> [--kind K] [--offset N] [--limit N] [--expect-before REV] [--expect-after REV] [--require-equal]` | Compare two complete projects by stable ID | No |
| `status <project> [--require-current]` | Check whether every configured read-only output is synchronized | No |
| `generate <project> --dry-run [--expect REV]` | Build all configured artifacts in memory | No |
| `generate <project> [--expect REV]` | Regenerate XLSX, C header, and Markdown | Yes |
| `apply <project> <patch.json\|-> --dry-run` | Validate a candidate patch and preview outputs | No |
| `apply <project> <patch.json\|-> [--expect REV]` | Atomically save a guarded patch and regenerate outputs | Yes |

`apply --force` explicitly disables the caller-supplied revision precondition. It is intended only
for a caller that has already serialized access to the project. It cannot be combined with
`--expect` or a patch `expected_revision`; `regmapc` still rejects a concurrent disk change that
occurs during the command.

## Initialize a project

```powershell
regmapc --json init device.regmap.yaml `
  --name "Device Register Map" `
  --workspace-id workspace-device
```

`init` creates an empty, valid project with the same defaults as **New Project** in Workbench:
one stable Workspace ID, a `generated` directory, and XLSX, C header, and Markdown targets. It
generates those three empty derivative views by default. It does not create or modify the
configured RTL file.

The command refuses to overwrite an existing project or an existing target output. Use a new
directory when those names are already occupied. `--no-generate` creates only the project file
when the caller intends to add the initial hierarchy immediately:

```powershell
regmapc --json init device.regmap.yaml --no-generate
```

When `--name` is omitted, the Workspace name comes from the project filename. When
`--workspace-id` is omitted, the core generates a unique stable ID. The returned revision can be
used immediately by the first `apply`.
Project and patch paths use the platform's native Unicode file APIs, including paths containing
Chinese characters or spaces. If a Workspace name contains no ASCII identifier characters, new
technical filenames use the stable `register_map` fallback while the displayed Workspace name is
preserved unchanged.

## Compare two projects

`diff` compares two complete saved projects by stable object ID without modifying either project
or any generated file:

```powershell
regmapc --json diff baseline.regmap.yaml device.regmap.yaml
```

A successful result contains the absolute `before_project` and `after_project` paths, both
SHA-256 revisions, `changed`, `change_count`, Added/Removed/Modified counts, and a deterministic
`changes` array. Each change identifies the object kind, stable ID, relevant name, summary, and
the available source location on each side. For Removed objects, `name` is the before-side name;
for Added and Modified objects it is the after-side name. The missing side of an Added or Removed object is
JSON `null`. An equal comparison succeeds with `changed: false` and an empty array.

Every returned change also contains flat `before` and `after` object states. A state identifies
the kind, stable ID, direct parent, sibling order, and scalar `properties`; descendants are not
embedded. The absent side of an Added or Removed change is `null`. Modified changes include a
sorted `property_changes` array with `property`, before/after presence flags, and exact JSON
values. Structural moves and reorders therefore report `parent_id` or `order` directly, while a
rename reports values such as:

```json
{
  "property": "name",
  "before_present": true,
  "before": "CONTROL",
  "after_present": true,
  "after": "CONTROL_NEXT"
}
```

Each existing side also has `before_navigation` or `after_navigation` using the same
`workbench_navigation` contract as `list`, `find`, and `get`. Added objects have only after-side
navigation; Removed objects have only before-side navigation. A renamed or moved object retains
both descriptors, including the hierarchy path appropriate to each project, so a host can open
either project at that exact stable ID without another lookup.

`--kind` accepts `workspace`, `page`, `block`, `register`, `field`, `enum`, or `all`.
`changed`, `change_count`, and the three counts describe the complete kind-filtered comparison;
`changes` contains only the requested page. The top-level `result_metadata` uses the same
`total_count`, `offset`, `returned_count`, `has_more`, and `next_offset` contract as `list` and
`find`. Stable-ID ordering makes pages deterministic.

Because one response represents two projects, their paths and revisions are in `result` rather
than the single-project envelope members. Both inputs must open and validate. The command
rechecks both revisions after comparison and returns a revision conflict without a `changes`
array if either file changed during the operation. `writes_performed` is always `false`.
For a continuation call, pass the revisions from the first result as `--expect-before` and
`--expect-after`; a mismatch returns exit code `3` before exposing a comparison result:

```powershell
regmapc --json diff baseline.regmap.yaml device.regmap.yaml `
  --kind register --offset 100 --limit 100 `
  --expect-before sha256:... --expect-after sha256:...
```

The default command succeeds whether the selected scope is equal or different; callers inspect
`changed`. Add `--require-equal` when a shell or CI job must branch on the process status without
parsing JSON. Equality still returns `0`. Any difference in the complete kind-filtered scope
returns code `6`, `exit_status: "differences_found"`, and error code `RMC6001`, while preserving
the normal result and requested page for diagnosis. Pagination never weakens this check.

Text mode prints the two paths and revisions, the three counts, then one tab-separated row per
change and an indented old-to-new line for each modified property:

```text
modified	register	reg-control	CONTROL_NEXT	Properties changed
  name: "CONTROL" -> "CONTROL_NEXT"
```

## Check output status

`status` compares each configured XLSX, C header, and Markdown file with the output that the
current saved project would produce. It does not modify the project or any output:

```powershell
regmapc --json status device.regmap.yaml
```

Each artifact reports one of `synchronized`, `missing`, `modified`, `writable`, or `unreadable`.
The result also contains `outputs_current`, `output_count`, `synchronized_count`, and
`attention_count`. Missing or stale outputs are valid status results, so the command succeeds and
the caller inspects `outputs_current`; project, validation, generation, or concurrent-revision
failures still use the documented nonzero exit codes. The project revision is checked again after
inspection so a caller never receives a synchronized result calculated from a project that changed
during the command.

Use the strict form when a shell, CI job, or embedding host should branch on the process exit code
without parsing the result:

```powershell
regmapc --json status device.regmap.yaml --require-current
```

The strict form returns the same artifact details, but missing, modified, writable, or unreadable
outputs return code `5`, `exit_status: "outputs_out_of_date"`, and error code `RMC5001`. It still
does not modify any file. Current outputs return code `0`.

Whenever `attention_count` is nonzero, `result.recovery` contains `command`, the absolute
`project` path, `expected_revision`, and a ready-to-execute `arguments` array equivalent to:

```powershell
regmapc generate device.regmap.yaml --expect sha256:...
```

The revision is the one used for the status result, so an embedding host can regenerate safely
without constructing or guessing the concurrency guard. Text mode prints the same recovery
command.

## Find a stable ID

Automation commonly starts with a name, address, tag, or description rather than an internal ID.
`find` resolves those clues without writing the project:

```powershell
regmapc --json find device.regmap.yaml status
regmapc --json find device.regmap.yaml 0x43C00020 --kind register
regmapc --json find device.regmap.yaml interrupt --kind field --parent reg-status
regmapc --json find device.regmap.yaml control --kind register --tag control
```

The search is case-insensitive and examines `id`, `name`, hierarchy `path`, base/address/offset,
size and width, bit positions, type, enum value, tags, and description. `--kind` accepts the same
kinds as `list`; `--parent` restricts results to direct children of one stable ID. Add
`--recursive` to include every descendant below that parent while excluding the parent itself:

```powershell
regmapc --json list device.regmap.yaml --parent page-main --recursive --kind register
regmapc --json find device.regmap.yaml ready --parent block-control --recursive --kind field
```

`--recursive` requires `--parent`; using it alone is a usage error. Kind, Tag, query, pagination,
and revision filtering apply to the complete descendant scope, so a host can traverse a Page or
Block in one project read. Without `--recursive`, the direct-child behavior is unchanged. Each result
identifies the winning property in `match_field` and is ordered by `match_rank`: exact ID/name
(`0`), exact path/property (`1`), ID/name prefix (`2`), then substring (`3`). Hierarchy order is
preserved within each rank, so repeated calls over the same revision are deterministic. No match
is a successful empty result rather than an error.
Text mode appends `match_field` and `match_rank` as the final two tab-separated columns.

Add `--exact` when a host already has a complete identifier, name, path, address, Tag, or other
property value. It retains only the existing case-insensitive rank `0` and `1` matches and excludes
name/ID prefixes and all substring matches. It composes with kind, parent/recursive scope, exact
case-sensitive `--tag` filtering, pagination, and `--expect`; omitting it preserves the ranked
search behavior above.

Use `--require-one` when the next action is valid only for one resolved object. The uniqueness
check runs after query matching, `--exact`, kind, parent/recursive scope, and Tag filtering. One
match keeps the normal one-item result array. No match returns project error `RMC2001`; multiple
matches return project error `RMC2002` while preserving every ranked candidate and the normal
result metadata so the caller can disambiguate. Text mode likewise prints the candidates before
the ambiguity diagnostic. `--require-one` cannot be combined with `--offset` or `--limit`, because
uniqueness applies to the complete filtered result. `--expect` remains valid and is checked before
the search; a stale revision therefore returns `RMC3001` without a uniqueness result.

For both `list` and `find`, `--tag` retains only objects containing that exact, case-sensitive
tag. It composes with kind, direct-parent, query, pagination, and revision filters. A missing or
differently cased tag produces a successful empty result, and pagination metadata describes the
filtered result set. `summary` exposes the available tags in deterministic lexical order as
`result.tags`; each item contains `name` and the number of Registers using it in `register_count`.
An embedding host can populate its Tag selector with one read instead of paging through every
Register.

Every JSON `list` and `find` response includes `result_metadata` with `total_count`, `offset`,
`returned_count`, `truncated`, `has_more`, `next_offset`, and `limit`. `--offset` is a
non-negative integer and `--limit` is a positive integer. Results retain deterministic hierarchy
order (`find` first applies its stable match ranking), so a host can pass `next_offset` into the
next invocation without repeating or skipping an object while the returned project revision is
unchanged. Pass the first response's `revision` as `--expect` on every continuation call. If the
project changes between pages, the command returns revision-conflict exit code `3`, includes the
current revision, and returns no result page; the host can discard the earlier pages and restart
instead of combining two project snapshots. Omitting both pagination options preserves the
complete-result behavior. An offset beyond the end
returns an empty successful page with `has_more: false`. Text mode prints the same page position
and continuation offset after the returned rows.

Every JSON item returned by `list` or `find`, every successful JSON object returned by `get`,
and every found `get-many` item object
contains `workbench_navigation`. Its `arguments` array is ready to pass to the desktop executable:

```json
{
  "protocol_version": 1,
  "project": "C:/project/device.regmap.yaml",
  "stable_id": "reg-control",
  "kind": "register",
  "path": "Device Register Map/Main/Control/CONTROL",
  "arguments": [
    "--project",
    "C:/project/device.regmap.yaml",
    "--select",
    "reg-control"
  ]
}
```

The descriptor is self-contained: `kind` and `path` let a host display the exact destination
without reopening or traversing the result, and `protocol_version` versions this handoff
independently of the larger CLI envelope. The CLI does not start a process. A host may launch
Register Map Workbench with `arguments`, route `project` and `stable_id` into an embedded
Workbench surface, or ignore the member. The `schema` response exposes its fields and version as
`workbench_navigation_contract`.

For both `list` and `find`, `--parent` must be an existing stable ID in direct or recursive mode. A misspelled parent returns
exit code 1 and diagnostic `RMC2001`; it is not silently treated as an object with no children.
Repeating `--kind`, `--parent`, or `--tag` is a usage error instead of accepting one value
ambiguously.

Omit the stable ID from `get` to retrieve the complete Workspace hierarchy in one call:

```powershell
regmapc --json get device.regmap.yaml
```

The response envelope already contains the project revision, so an embedding host does not need
to call `summary` first. Supplying a stable ID retains the narrower object-and-descendants lookup.
When that ID came from an earlier `list` or `find`, pass that response's `revision` to prevent a
second command from silently reading a different project snapshot:

```powershell
regmapc --json get device.regmap.yaml reg-control --expect sha256:...
```

A mismatch returns revision-conflict code `3`, `RMC3001`, the expected and current revisions, and
`writes_performed: false`; no Workspace or object result is returned. The same guard works when
the stable ID is omitted.

Use `get-many` when a host already has several stable IDs and needs their complete objects without
reopening the project for each ID:

```powershell
regmapc --json get-many device.regmap.yaml `
  reg-control field-enable reg-control reg-missing `
  --expect sha256:...
```

The result contains `requested_count`, `found_count`, `missing_count`, `duplicate_count`, and an
`items` array in the exact request order. Every item contains its zero-based `request_index`,
`requested_id`, `found`, `object`, `error_code`, and `duplicate_of_index`. A repeated ID remains a
separate item; its `duplicate_of_index` points to the first occurrence. `found_count` counts found
request occurrences, including duplicates.

If any ID is absent, found objects remain available in the same response, missing items contain
`object: null` and `error_code: "RMC2001"`, and the command returns project-error exit code `1`
with primary error code `RMC2001`. This lets a host identify every missing request without losing
the successful reads or issuing another command. The project is opened only once and all objects
come from the envelope's single `revision`. A stale `--expect` returns revision-conflict code `3`
before object lookup; that response contains no `items` or object payload.

## Stable JSON envelope

Every `--json` response is one compact JSON object, including `--help`, `version`, and
`--version`. This lets an embedding host negotiate the API before it opens a project:

```powershell
regmapc --json version
regmapc --json --help
```

Both responses include `project_schema.current_version` and
`project_schema.supported_versions`. A host can therefore reject an incompatible project format
before opening a user project; the current CLI supports schema version `2`.
They also include `json_transport`: UTF-8, one compact JSON object followed by one LF on stdout,
CLI diagnostics inside the envelope's `diagnostics` array, and no CLI response text on stderr.
The contract also identifies `-` as the token for reading one UTF-8 patch document from stdin.
These fields let an embedding host configure framing and channels without parsing this document.
`version` also returns `capabilities_command: "schema"`, `help_option: "--help"`, and
`help_command: "help"`, so a host can negotiate the API with the smallest response and then
request the full contract. JSON `--help` returns that same full discovery document plus the
human-readable usage text.

`help <command>` returns only the selected command's usage, project-access class, file-write
behavior, and typed argument schema. It never opens a project or writes a file:

```powershell
regmapc help generate
regmapc --json help generate
regmapc generate --help
```

An unknown help topic uses the same `result.suggested_command` recovery as an unknown command.
`<command> --help` and `<command> <otherwise required arguments> --help` return the same focused
response without opening the supplied project path. Omitting the command from `--help` retains
the complete global discovery response.

Running `regmapc` without a command, including `regmapc --json`, is a usage error with exit code
`2`; it never reports a successful no-op. Use the explicit `--help` or `-h` option when help is the
intended successful operation. The discovery document exposes this distinction in
`result.no_command_contract`.

Every entry in `result.commands` also declares `project_access` (`none`, `read`, `create`, or
`write`) and `file_write_behavior` (`never`, `always`, or `unless_dry_run`). The accompanying
`command_discovery_contract` defines those tokens. A host or Codex integration can therefore
restrict execution to read-only commands, or require `--dry-run`, without parsing the English
usage string.

`result.global_options` describes `--json`, `--help`/`-h`, and the `--` argument terminator,
including their position and repeatability. `result.command_argument_schemas` provides the command line shape
without parsing that usage string. Every command has ordered `positionals` and typed `options`;
optional positionals carry `required: false`, the repeated stable-ID positional for `get-many`
carries `repeatable: true`, closed choices carry `allowed_values`, and option
conflicts such as `apply --expect` versus `--force` appear in
`mutually_exclusive_options`. These additions are backward-compatible within JSON API version 1.
An unknown command within edit distance two of a known command returns
`result.suggested_command` and includes the same suggestion in text mode; unrelated tokens retain
a null result. A near-miss command option returns `result.suggested_option` and also names the
closest option in the error; unrelated options retain a null result. The corresponding legal
option set remains machine-readable in `command_argument_schemas`. Invalid `--kind` errors list
every accepted kind directly.

Project commands use this envelope:

```json
{
  "api_version": 1,
  "command": "get",
  "ok": true,
  "exit_code": 0,
  "exit_status": "success",
  "project": "C:/project/device.regmap.yaml",
  "revision": "sha256:...",
  "diagnostics": [],
  "result": {}
}
```

Read commands may add a command-specific `result_metadata` object beside `result`; clients must
use it when present. `list`, `find`, and `diff` use it for deterministic pagination.

`exit_code` and `exit_status` are present in every JSON response, including help, version,
unknown-command, project-load, revision-conflict, and write-failure responses. Their stable
mapping is `0=success`, `1=project_error`, `2=usage_error`, `3=revision_conflict`, and
`4=write_error`, `5=outputs_out_of_date`, and `6=differences_found`. A nonzero response also contains `error_code`. It is
the first error diagnostic code when one exists; otherwise it is the category code `RMC1000`,
`RMC2000`, `RMC3000`, `RMC4000`, `RMC5000`, or `RMC6000`. `error` remains the human-readable explanation,
but an embedding host does not need to parse it to select its recovery path.

An `exit_status` of `usage_error` also returns `usage`. For a recognized command this is its exact
invocation syntax; for an unknown command it is the complete CLI usage text. Text mode prints the
same recovery syntax immediately after the error, so a missing or invalid argument does not require
a separate help invocation.

An option that requires a value never consumes a following `--option` token as that value. The
command returns `usage_error` before opening or writing the project and identifies the option whose
value is missing.

`revision` is the SHA-256 digest of the project manifest. An automation client should read it from
`summary`, `list`, `get`, `get-many`, or `validate`, place it in a patch, run a dry run, then perform the real
apply. A stale revision returns exit code 3 and writes nothing.

Exit codes are stable:

| Code | Meaning |
| --- | --- |
| 0 | Success |
| 1 | Project load or model validation failure |
| 2 | Command, option, patch shape, property, or value error |
| 3 | Revision conflict |
| 4 | Project/output write or generation failure |
| 5 | Outputs are not current under `status --require-current` |
| 6 | Differences exist under `diff --require-equal` |

Pass `-` instead of a patch path to read one JSON document from standard input. This avoids a
temporary file when a host application or Codex already has the patch in memory:

```powershell
$patch | regmapc --json apply device.regmap.yaml - --dry-run
```

## Atomic model patch

An apply patch uses stable object IDs rather than names or row positions:

```json
{
  "api_version": 1,
  "expected_revision": "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "operations": [
    {
      "op": "set",
      "id": "reg-control",
      "property": "description",
      "value": "Enables the device."
    },
    {
      "op": "set",
      "id": "reg-control",
      "property": "tags",
      "value": ["control", "startup"]
    }
  ]
}
```

When adding a Block, use the exact string `"auto"` for `base` together with a
positive explicit `size`:

```json
{
  "op": "add",
  "kind": "block",
  "id": "block-status",
  "parent_id": "page-main",
  "value": {
    "name": "Status",
    "base": "auto",
    "size": "0x100"
  }
}
```

Automatic Block placement selects the lowest Page-relative range that does not
overlap an existing Block and fits the Page Address Width. It never moves an
existing Block. A Size is mandatory because an empty Block without a declared
allocation has no safe capacity to reserve.

An embedding host can create a complete hierarchy without first inventing or
reading back intermediate IDs. Set an add operation's `id` to the exact string
`"auto"`, then reference an earlier operation result from `parent_id`:

```json
{
  "api_version": 1,
  "expected_revision": "sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
  "operations": [
    {
      "op": "add",
      "kind": "page",
      "id": "auto",
      "parent_id": "workspace-main",
      "value": {"name": "Main"}
    },
    {
      "op": "add",
      "kind": "block",
      "id": "auto",
      "parent_id": {"operation": 0},
      "value": {"name": "Control", "base": "auto", "size": "0x100"}
    },
    {
      "op": "add",
      "kind": "register",
      "id": "auto",
      "parent_id": {"operation": 1},
      "value": {"name": "CONTROL", "offset": "auto", "type": "field"}
    },
    {
      "op": "add",
      "kind": "field",
      "id": "auto",
      "parent_id": {"operation": 2},
      "value": {"name": "ENABLE", "lsb": "auto", "type": "bool"}
    }
  ]
}
```

Automatic add IDs are deterministic for the guarded revision and earlier
operations in the same patch. They use the object kind and name, adding a numeric
suffix when that ID is occupied. Each add change reports `automatic_id` and its
resolved `id`. A `{"operation": N}` parent reference uses the `id` returned by an
earlier zero-based operation; self, forward, fractional, negative, malformed, or
wrong-parent-kind references reject the complete patch. Add, copy, and move accept
this parent-reference form. The same reference object may replace `id` for set,
copy, move, and remove, and may replace a move `before_id` ordering anchor. For
example, `{"op":"set","id":{"operation":0},...}` edits the object created by
operation zero, while a copy can use an earlier add as both its source or
destination parent. A dry run and the subsequent real apply therefore use the
same hierarchy IDs when the revision remains unchanged.

For a long or hand-reviewed patch, define an optional operation `ref` and use a
named reference instead of maintaining numeric indexes:

```json
[
  {
    "op": "add",
    "ref": "main_page",
    "kind": "page",
    "id": "auto",
    "parent_id": "workspace-main",
    "value": {"name": "Main"}
  },
  {
    "op": "add",
    "ref": "control_block",
    "kind": "block",
    "id": "auto",
    "parent_id": {"ref": "main_page"},
    "value": {"name": "Control", "base": "auto", "size": "0x100"}
  }
]
```

Operation refs are patch-local, case-sensitive, unique, non-empty, and cannot
have leading or trailing whitespace. A named reference can point only to an
earlier successful operation, exactly like the numeric form. Each change result
echoes its defining `ref`. Inserting or reordering unrelated operations therefore
does not require rewriting every named reference.

When adding a Register, use the exact string `"auto"` for `offset` when the caller
does not need to choose an address:

```json
{
  "op": "add",
  "kind": "register",
  "id": "reg-status",
  "parent_id": "block-control",
  "value": {
    "name": "STATUS",
    "offset": "auto",
    "width": 32,
    "type": "unsigned"
  }
}
```

Automatic placement uses the same four-byte alignment as Workbench. It first tries
the aligned range after the last Register, then the earliest aligned gap when the
Block or Page boundary prevents appending. If no range fits, the complete patch is
rejected without writing. Omitting `offset` still means `0x0`, and explicit numeric
offsets retain their existing behavior.

Field creation supports the same explicit opt-in for bit placement:

```json
{
  "op": "add",
  "kind": "field",
  "id": "field-ready",
  "parent_id": "reg-status",
  "value": {
    "name": "READY",
    "lsb": "auto",
    "width": 1,
    "type": "bool"
  }
}
```

`lsb: "auto"` selects the lowest contiguous free range inside the structure
Register or compound Field. `width` defaults to one when omitted. An explicit
`msb` cannot be combined with automatic placement because it would define an
ambiguous width and position. If no range fits, the complete patch is rejected.

Operations are applied to an in-memory copy in array order. The complete candidate is validated
and all configured outputs are generated in memory before a real save. If any operation or
validation fails, the project remains unchanged. Immediately before saving, `regmapc` checks that
the project revision has not changed. The project manifest is then replaced atomically and the
read-only outputs are written.

If an individual operation fails, the JSON response retains the existing exit code and `error`
text and also returns `result.failure`:

```json
{
  "stage": "operation",
  "operation_index": 1,
  "json_pointer": "/operations/1",
  "operation_count": 4,
  "completed_operation_count": 1,
  "operation": {"op": "set", "id": {"operation": 4}, "property": "description", "value": "..."},
  "message": "Operation 'id' reference index must identify an earlier operation.",
  "writes_performed": false,
  "completed_operations_rolled_back": true
}
```

`operation_index` is zero-based and `operation` is the original failing JSON object. A host can
therefore select the exact editor row or request element without parsing the English error
sentence. `completed_operation_count` describes work performed only on the in-memory candidate;
those operations are rolled back and do not need a compensating patch. Patch-file parsing,
revision, and filesystem failures continue to use their existing structured envelope,
diagnostics, and recovery fields. The envelope's `exit_status` identifies the failure category
and `error_code` identifies its primary diagnostic without inspecting the English message.

When every operation succeeds but the candidate cannot be saved, `result.failure.stage` is
`validation` or `generation`. In these stages `operation_index`, `json_pointer`, and `operation`
are `null` because no single operation necessarily owns the failure. The object instead returns:

```json
{
  "stage": "validation",
  "operation_count": 3,
  "completed_operation_count": 3,
  "error_diagnostic_indexes": [0, 2],
  "error_codes": ["RM3010"],
  "object_ids": ["reg-control"],
  "writes_performed": false,
  "completed_operations_rolled_back": true
}
```

`error_diagnostic_indexes` points directly into the response's top-level `diagnostics` array;
`error_codes` and `object_ids` are de-duplicated conveniences. A generation-stage failure means
the candidate model passed validation but one or more configured read-only outputs could not be
constructed in memory. Neither stage writes the project or outputs. Failures after the model has
already been saved intentionally remain represented by `saved`, `outputs_current`, and
`recovery`, because claiming rollback in that state would be incorrect.

### Repair a project with validation errors

`apply` can start from a project that parsed successfully but currently has model validation
errors, such as overlapping Registers or an out-of-range Field. Put every required correction in
one patch and use the revision returned in the failing `validate`, `summary`, or `get` JSON
envelope. Parse/schema/load errors still block mutation because no trustworthy model is available.

The candidate must resolve every validation error before a real write. It may remove existing
diagnostics but may not replace them with a different error or warning. A patch that leaves an
error or introduces a different Problem returns exit code 1 and leaves the project and every
generated output unchanged.

Dry-run and real responses include a `repair` object when the starting model was invalid:

```json
{
  "baseline_invalid": true,
  "candidate_valid": true,
  "before_problem_count": 2,
  "after_problem_count": 0,
  "before_error_count": 2,
  "after_error_count": 0,
  "resolved_problem_count": 2,
  "introduced_problem_count": 0
}
```

This allows a host to distinguish a complete repair from a patch that only moved the error.
Text mode prints the same before/after, resolved, and introduced counts. A successful repair then
uses the normal revision guard, atomic project replacement, and output generation path.

If an `apply` or `init` project save succeeds but an output cannot be written, the response reports
`saved: true`, `generated: false`, and exit code 4. The JSON result also contains a structured
`recovery` object:

```json
{
  "command": "generate",
  "project": "C:/project/device.regmap.yaml",
  "reason": "saved_model_has_pending_outputs",
  "expected_revision": "sha256:...",
  "arguments": [
    "generate",
    "C:/project/device.regmap.yaml",
    "--expect",
    "sha256:..."
  ]
}
```

Close the locked file and execute the returned `arguments`. The saved revision is included as an
`--expect` guard whenever it could be calculated; if another process edits the project first,
recovery stops with a revision conflict instead of generating from an unreviewed model. A failed replacement restores the existing generated
file's read-only permission, so a temporary generation failure does not silently turn XLSX into
an editable source. The next successful `generate` repairs all configured outputs from the saved
model. Files whose content and read-only permission are already correct are not replaced; this
preserves their timestamps and lets generation succeed while an unchanged workbook is open in
Excel. Text mode prints the equivalent `Recovery: regmapc generate "<project>"` command to
standard error.

## Generation revision guard

`generate` always rereads the project revision after building artifacts in memory and again after
writing them. `--expect <revision>` additionally requires the project to match a revision obtained
from `summary`, `list`, `get`, `get-many`, or `validate`:

```powershell
regmapc --json generate device.regmap.yaml --expect sha256:...
```

A mismatch before writing returns exit code 3 and leaves every output untouched. If another
process saves the project while outputs are being replaced, the command reports the new current
revision, sets `outputs_current: false`, and returns a revision-guarded `result.recovery` command.
An output-write failure with a readable current revision returns the same recovery shape, so a
host can close the locked file or repair the output path and execute the supplied arguments
without guessing which revision to generate. A successful write reports `written: true`,
`outputs_current: true`, `generated_from_revision`, and
`current_revision`. Dry-run reports `outputs_current: null` because it does not inspect or replace
the existing derivative files.

Supported writable properties are returned by `regmapc --json schema`. Setting Field `lsb`
repositions the Field while preserving its current width and deriving the new MSB; setting
`width` keeps the current LSB and also derives MSB. Workbench still keeps direct LSB cell editing
disabled and performs this action through Field dragging. JSON `null` clears optional Block size,
range bounds, Initial Value, or Reset Value. Addresses and values accept strings such as
`"0x43C00000"` or safe JSON integers. Values above `2^53-1` must be strings so JSON number
rounding cannot change an address or reset value.

A Field drag can therefore be expressed without deleting or recreating the Field:

```json
{
  "op": "set",
  "id": "field-enable",
  "property": "lsb",
  "value": 8
}
```

The Field, nested members, Enum Values, and all stable IDs remain unchanged. The complete patch
is rejected if the new range exceeds its containing Register/Field or overlaps another Field.

Five operation types are supported:

- `set` changes one writable property on an existing stable ID.
- `add` creates a Page, Block, Register, Field, or Enum Value. Its `id` may be explicit or
  `"auto"`, and `parent_id` may be a stable ID or an earlier-operation reference.
- `copy` deep-copies an existing Page, Block, Register, Field, or Enum Value under a new root ID.
- `move` relocates an existing Page, Block, Register, Field, or Enum Value to a
  `parent_id` without changing its stable ID or descendants. Block, Register, and Field moves may
  request `placement: "auto"`; Page, Block, and same-parent Register moves may also specify
  `before_id`.
- `remove` deletes one stable ID. If it owns any descendants, `"cascade": true` is mandatory.

Where an operation consumes an existing object ID, set/copy/move/remove `id`, add/copy/move
`parent_id`, and move `before_id` accept either a stable ID string or
`{"operation": N}` or `{"ref": "name"}`. References are patch-local and can point only to an
earlier operation. Every successful operation returns its resolved `id` and any defining `ref`,
so references can be chained through add, set, copy, and move without inspecting an intermediate
response.

The machine-readable required members, allowed kinds, parent kinds, and writable properties are
returned by `schema`.

An add operation uses the same property names returned by `get`:

```json
{
  "op": "add",
  "kind": "register",
  "id": "reg-status",
  "parent_id": "block-control",
  "value": {
    "name": "STATUS",
    "offset": "0x4",
    "width": 32,
    "type": "unsigned",
    "access": "ro"
  }
}
```

Page, Block, and Register creation requires `name`. A Page defaults to Base `0`, Address Width
`32`; a Block defaults to Base `0` with no declared Size; a Register defaults to Offset `0`,
Width `32`, Type `unsigned`, Access `rw`, and zero Initial/Reset values. Block Base, Register
Offset, and Field LSB remain explicit unless their value is the exact string `"auto"`; automatic
placement never shifts an existing object. An automatic Block Base additionally requires a
positive explicit Size. Field creation requires `name` and `lsb`, and accepts either `width` or
`msb` when LSB is numeric. A Field parent must already be a structure Register or compound Field
at that point in the operation array. Enum creation requires `name` and `value`, and its parent
must already be a `bool` or `enum` Register/Field. An explicit add ID retains its existing
behavior; only the exact lowercase token `"auto"` requests automatic identity.

Deep copy preserves every model property and descendant while assigning new stable IDs:

```json
{
  "op": "copy",
  "id": "block-control",
  "new_id": "block-control-copy",
  "parent_id": "page-secondary",
  "value": {
    "name": "Copied Control",
    "base": "0x400"
  }
}
```

`new_id` identifies the copied root. Each descendant ID is deterministically formed as
`<new_id>--<source-descendant-id>`, so dry-run and real apply return the same identities. The
change result contains an `id_mapping` object for every copied object. Optional `value` members
override writable properties on the copied root only; the example changes the Block name and
base while preserving its Registers, Fields, Enum Values, and descriptions. Source locations are
cleared before insertion, then normal persistence assigns locations belonging to the new YAML
objects.

When the host does not need to choose internal identity or a display-name suffix, request both
explicitly:

```json
{
  "op": "copy",
  "id": "reg-status",
  "new_id": "auto",
  "parent_id": "block-diagnostics",
  "unique_name": true,
  "value": {
    "offset": "auto"
  }
}
```

The exact `new_id: "auto"` token selects the first deterministic root ID whose complete derived
descendant-ID family is free. `unique_name: true` selects `<source name> Copy`, then `Copy 2`,
`Copy 3`, and so on using the same sibling rule as Workbench. It cannot be combined with an
explicit root `value.name`. Because selection uses the guarded project revision and prior
operations in the same patch, Dry-run and real apply return the same generated root `id` and
`id_mapping`. The change also reports `automatic_id` and `unique_name`.

Copy preserves the source placement when its root placement property is omitted. To duplicate
without calculating a destination range, set the copied Block `base`, Register `offset`, or Field
`lsb` root override to the exact string `"auto"`:

```json
{
  "op": "copy",
  "id": "reg-status",
  "new_id": "reg-status-copy",
  "parent_id": "block-diagnostics",
  "value": {
    "name": "STATUS_COPY",
    "offset": "auto"
  }
}
```

Automatic copy placement uses the same rules as automatic creation and never moves an existing
object. A copied Block may reuse its positive Size or override it; a copied Field may reuse its
width or provide `width`, but cannot combine `lsb: "auto"` with `msb`. An automatically placed
Field and its Members derive their Reset values from the destination Register bits, matching
Workbench duplication. Without `unique_name: true`, Copy retains the source display name unless
`value.name` overrides it. Any generated-ID collision, duplicate name, unavailable placement, or
invalid destination parent rejects the complete patch without leaving a partial copy.

Move uses the same parent rules as creation:

```json
{
  "op": "move",
  "id": "reg-status",
  "parent_id": "block-diagnostics"
}
```

A Page remains under the Workspace, a Block moves to a Page, a Register to a Block, a Field to a
structure Register or compound Field, and an Enum Value to a `bool` or `enum` Register/Field. The
object, its descendants, and all stable IDs are preserved. Moving an object to its current parent
without `before_id` is a no-op and does not rewrite the project. Moving a Field under itself or one
of its descendants is rejected. If a move changes addresses, bit placement validity, or creates
overlap, validation rejects the complete patch and writes nothing. A separate `set` operation in
the same patch may update the Register offset or Field width before final validation.

To move an allocated object without calculating a destination position, use the exact string
`"auto"`:

```json
{
  "op": "move",
  "id": "reg-status",
  "parent_id": "block-diagnostics",
  "placement": "auto"
}
```

The object is excluded from its old location before placement is calculated. Blocks select the
lowest Page-relative range that fits their existing positive Size; Registers use the normal
four-byte-aligned append/gap rule; Fields use the lowest contiguous same-width bit range. An
automatically moved Field and its Members derive Reset values from the destination Register bits.
The change result returns `automatic_placement: true` and a `placement_result` containing the
final Base, Offset, or LSB/MSB, so a host does not need a follow-up `get` merely to learn where the
object landed. Omit `placement` to preserve the current Base, Offset, or LSB. Page and Enum Value
moves reject automatic placement.

Page and Block order can mirror a Workbench navigation drag:

```json
{
  "op": "move",
  "id": "block-diagnostics",
  "parent_id": "page-main",
  "before_id": "block-control"
}
```

For a Register drag inside its current Block, the same member uses Workbench's Offset-slot
semantics:

```json
{
  "op": "move",
  "id": "reg-status",
  "parent_id": "block-control",
  "before_id": "reg-command"
}
```

The moved Register must be address-movable. All movable Registers retain the Block's existing
sorted set of Offset slots, while fixed Register offsets remain unchanged. The result's
`placement_result` reports the moved Register's final Offset. Final validation rejects a
different-width arrangement that would overlap or exceed the Block/Page range. Register
`before_id` is same-Block only and cannot be combined with `placement: "auto"`; use automatic
placement for a cross-Block move. This deliberately matches Workbench drag behavior rather than
silently recalculating or moving fixed addresses.

`before_id` must identify a sibling in the relevant parent. Set it to JSON `null` to move a Page,
Block, or movable Register to the end. Field order remains bit-driven and is changed through
`lsb`.

A recursive deletion must state its effect explicitly:

```json
{
  "op": "remove",
  "id": "page-obsolete",
  "cascade": true
}
```

The result reports the removed object and descendant count. Without `cascade: true`, deletion of
an object that owns Blocks, Registers, Fields/Members, or Enum Values is rejected before any file
is written.

## Recommended automation sequence

```powershell
regmapc --json summary device.regmap.yaml
regmapc --json status device.regmap.yaml --require-current
regmapc --json apply device.regmap.yaml patch.json --dry-run
regmapc --json apply device.regmap.yaml patch.json
regmapc --json validate device.regmap.yaml
```

The second apply uses `expected_revision` stored in `patch.json`. After success, use the returned
new revision for the next patch.

Workbench can remain open while an automation client uses `regmapc`. A clean Workbench session
automatically reloads a valid external project save. Unsaved Workbench edits are never silently
replaced, and a later Workbench Save & Sync is blocked until the user explicitly chooses the disk
version or the Workbench version. CLI callers must still use `expected_revision`; the Workbench
guard protects the other direction of the same concurrent-edit workflow. If the user is still
typing in an active model editor, Workbench defers automatic reload without committing or
discarding that text. Project-local crash drafts also retain their saved-project base: if
`regmapc` changes the project after a draft is written, the next Workbench open merges independent
changes and asks which side wins only for properties changed by both.
