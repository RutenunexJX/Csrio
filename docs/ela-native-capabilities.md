# Ela native capabilities — Csrio

Status: completed — UI implementation and validation (2026-09-24), release 0.3.5.
Baseline: `dd413042` / 0.3.4. AppSuite publication is owned by the coordinating task.
Verified shared source: ZeroSlack `75180fad5e5f5142684cf092649deffe5720994d`
(includes patch 26 at `3f1c4af` and menu patch 27), plus xIPs patch 28 for the
ElaListView style lifetime and Wave patch 29 for overlay origin-bar lifetime.
Patch 29 SHA-256: `c292256d9d23cc391b2a185b88d7335f79410ef08e491f727916829c627a88f8`.
Shared patch 30 fixes combo popup padding and repeated-show stability; SHA-256:
`e69b815ba035831e2a84484acb0c46f957c83f346fff230a8c2b3034d4a44046`.
RegMap integrates selected files, not a wholesale tree. API/ABI capability level
remains p27; patches 28/29/30 do not change public layout or signatures.

## Scope and release boundary

Incrementally integrate applicable shared Ela controls and native interaction
contracts, preserving RegMap patches 08–10, semantic colors, 10pt typography,
keyboard access, transaction guards and model ownership. The register model,
generators, CLI and project formats are outside this change. The two protected
minimal example files must not be modified or staged.

Only Ela is maintained. Qt splitters, professional register/field views, bitfield
and address-space canvases remain application-owned. Fixed Problems/Generated/Diff
pages do not become detachable documents. Components without an existing product
use case are not added merely to increase a migration count.

Validate interruption, reversal, hide, resize, destruction, keyboard/focus,
light/dark and DPI. Compare the same window/content workload before and after;
report dispatch time, paint/layout events and bounded snapshot memory, not FPS.
Preserve exact 960×720/1440×900, 10pt and clipping assertions at 100/125/150/200%.
Background tests do not claim desktop compositor, physical drag or mixed-monitor
DPI acceptance. No desktop mouse/focus operation is authorized without notice.

After tests and audit, commit/push only this delivery and build a clean directory
staging. The coordinating task owns final AppSuite directory replacement and
shared manifests. No ZIP, old-package backup or independent AppSuite write.
Shared menu patch 27 is integrated; runtime QWidgetAction editors remain live.

## Actual components and ownership

| Surface | Actual component / enabled behavior | Application responsibilities |
| --- | --- | --- |
| Commands, forms and status | ElaPushButton, ElaToolButton, ElaLineEdit, ElaCheckBox, ElaToolBar, ElaStatusBar | Commands, semantic tokens, point fonts, focus order and accessible names |
| Type/Access and filters | ElaComboBox; 180 ms height/position and 150 ms indicator animation, immediate interruption | Options, custom widths, transactional commit/cancel and English text menus |
| Menus | ElaMenu; 160 ms translated snapshot, native QAction checks/shortcuts/submenus | Command ownership; live QWidgetAction input menus; reduced-motion preference |
| Lists and read-only results | ElaListView / ElaListWidget / ElaTableView with ElaScrollBar | Models, roles, checks, sorting, selection and programmatic navigation |
| Ordinary navigation tree | Existing HierarchyTreeView with ElaTreeView style factory and Qt expansion | Stable IDs, rename, drag/drop transactions; new input ends expansion first |
| Ordinary scrolling | ElaScrollBar smooth wheel, 160 ms; immediate precision pixels, keys and range changes | Selected browsing viewports only; register/field/address/bitfield precision is unchanged |
| Field and Results panels | ElaDrawerArea headerless content, bounded translated/faded snapshot | QSplitter endpoints, orientation/restore, command validation, focus intent and page identity |
| Labels | ElaText with host-palette opt-out | Semantic errors/disabled colors, 10pt typography, wrapping and links; this is component reuse, not a new interaction |
| Batch edit | ElaContentDialog with original Qt button roles and Ela buttons | Atomic edits, validation, explicit accept/cancel, default button and keyboard focus |
| Hints and feedback | ElaToolTip / ElaMessageBar | Qt hover timing, English text, persistent diagnostics and success semantics |
| Title and fixed results tabs | Existing ElaAppBar / ElaTabWidget integration | Dirty-close veto, DPI hit tests, three fixed page indices and lifetime |

QMessageBox destructive decisions and native file pickers remain their existing
Qt/system workflows. Validated in-cell text delegates and the register/field
tables, bitfield/address canvases retain their precise application editing
contracts. There is no ordinary multiline text editor to migrate. RegMap has no
document-tab drag, dock-floating workspace, calendar, Ribbon, navigation-page
catalogue or promotion carousel; adding these would create unrelated features.
Qt QSplitter is not an Ela component. Shared drawer/style/content-host changes
are local compatibility extensions to Ela, not unmodified upstream APIs.

Search acceleration caches only derived labels/searchable display values and
invalidates on project/edit-state changes. Exact/partial ranking, stable target
IDs and navigation remain unchanged; no model or file-format cache is introduced.

## Delivery evidence

- Incremental patch 11 replays on `dd413042` and reproduces all 411 vendor C++
  source/header files after Git line-ending normalization; 35 source files change.
  Original Ela MIT, font OFL and ZeroSlack Apache notices are unchanged. The
  existing eight runtime license texts are now installed reproducibly from source.
- `cmake --build build/ela-migration --parallel 4` and
  `ctest --test-dir build/ela-migration --output-on-failure -j 2`: 10/10 passed;
  GUI 168/168, native capabilities 13/13 at both 100% and 200%. The separate native
  Win32 hit-test case remains explicitly skipped in the offscreen state suite.
  Log: `build/ela-migration/ela-native-ctest-final.log` (87.86 s).
- `tests/capture_ui_snapshots.ps1`: 16/16 light/dark × 960×720/1440×900 ×
  100/125/150/200%, exact logical/pixel dimensions, 10pt fonts and clipping checks.
  All 16 were visually inspected. `tests/capture_ela_states.ps1`: 120 state PNGs;
  batch dialogs and settled combo popups were also visually inspected. Artifacts
  and SHA-256 manifests: `build/ela-migration/ela-native-screenshots` and
  `build/ela-migration/ela-native-states`.
- Capture timing now observes the 300 ms drawer and 180 ms combo endpoints.
  Assertions were not weakened: combo checks additionally require every row to
  fit and repeated show/open operations to preserve the complete popup size.
  Native regression covers 1/3/5 options, first/last selection, editable and
  non-editable controls, both themes, hide/reopen and already-visible show.
- Invalid cell edits still veto auxiliary commands. Panel scroll containers keep
  keyboard Tab focus but no longer steal mouse focus before the validation guard.
  Drawer input cancels pending focus intent, and cancelled opens leave no snapshot.
- Protected `git hash-object` values remain
  `e21e0c8c2e46f431df700f0ea26aa899fdec26ce` (`examples/minimal/.regmap.yaml`) and
  `d907228de354039025b16e83d1be2b4c0525fa17`
  (`examples/minimal/rtl/minimal_registers.sv`). Core, CLI, controller and canvas
  sources have no diff. Protected files are excluded from staging.
- Independent clean-directory production configuration:
  `build/ela-release-0.3.5`, Release/ELA, `REGMAP_BUILD_TESTS=OFF`, Qt 6.10.2,
  GCC 13.1.0, existing SuiteApp static integration retained. The staging script
  rejects a dirty checkout and verifies all deployed file hashes plus isolated
  GUI version/help and CLI generation. Final revision/source-state evidence is
  written into the package's `BUILD-INFO.txt`; publication is separate from UI
  validation and remains with the coordinating task. No ZIP or AppSuite write.

## Same-workload performance

Reproduction: `regmap_ui_performance.exe <output.json>`, offscreen, light theme,
DPR 1, 10pt, 1,000 registers, 24 dispatch samples per scenario and window size,
350 ms settling between samples. Before: `ela-native-before.json` at the baseline;
after: `ela-native-after.json`, both under `build/ela-migration`. Times are local
dispatch measurements, not desktop frame rate or a cross-machine benchmark.

| Scenario / logical size | Median before → after (ms) | Paint events before → after | Layout events before → after |
| --- | --- | --- | --- |
| Results, 12 open/close cycles, 960×720 | 0.9016 → 0.8374 | 1476 → 4230 | 144 → 581 |
| Results, 12 open/close cycles, 1440×900 | 0.8786 → 1.0085 | 1524 → 4338 | 144 → 581 |
| Search, 24 edits, 960×720 | 1.7619 → 0.6051 | 184 → 184 | 49 → 49 |
| Search, 24 edits, 1440×900 | 1.7082 → 0.5595 | 176 → 168 | 49 → 49 |

Derived search caching lowers median dispatch by approximately 66–67% in this
workload. Results animation introduces additional paint/layout work; it is not
claimed as a universal speedup. Peak snapshots were 679,680 and 1,310,080 bytes
(960/1440), below the 32 MiB cap, and retained bytes returned to zero. Last capture
preparation measured 0.7339 and 0.9083 ms respectively.
