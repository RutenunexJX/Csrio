# ElaWidgetTools migration

Status: all three slices completed (2026-09-22).
Release: 0.3.3, the first RegMapWorkbench Ela portable release. The slice evidence
below records the pre-publication state; release validation is recorded separately
at the end. The portable archive must identify a clean commit, include the Ela
DLL and notices, and pass the isolated-runtime smoke test.

Baseline: `d3c46cebf3500228cfbdae19354c0e9adc5fe831`, clean `main`.
Protected example blobs at the start: `.regmap.yaml`
`e21e0c8c2e46f431df700f0ea26aa899fdec26ce`, `minimal_registers.sv`
`d907228de354039025b16e83d1be2b4c0525fa17`.

The backend is selected once before constructing widgets. Ela, SuiteUi, and
Classic must not paint the same control. Ela uses the pinned upstream revision
`454cac2d57a47d3cc28577dc817793aec1881ca7` with the audited compatibility patch
set from committed ZeroSlack revision `261c90e6d96d9738f7eed2eab27499a25768778d`.
The dependency is copied into this repository; there is no build/runtime
dependency on the ZeroSlack checkout or its build products.

## Checkable migration inventory

| Surface | Planned integration | Status |
| --- | --- | --- |
| Backend/theme | Exclusive ELA/SUITEUI/CLASSIC build choice, frozen runtime Classic fallback, semantic Ela colors | implemented |
| Header and toolbar commands | ElaPushButton/ElaToolButton/ElaToolBar, mirrored QAction metadata and trigger/state behavior | implemented |
| Page/block and batch-edit forms | ElaLineEdit/ElaComboBox/ElaCheckBox with Qt value semantics | implemented |
| Menu bar, navigation and context menus | ElaMenuBar/ElaMenu; Favorites/recent-object navigation retained | implemented |
| Result tabs | ElaTabWidget; Problems/Generated/Diff remain nonclosable and nondetachable | implemented |
| Batch dialogs | Ela form controls and OK/Cancel buttons; native shells, accept/reject roles and validation retained | implemented |
| Other dialogs and settings | Ela appearance menus; native file pickers and destructive confirmations retained | deliberate boundary |
| Table surroundings | Ela filters/actions; semantic badges; scrollable overflow and pinned Register table | implemented |
| Scrollbars | Ela rendering with Qt wheel, keyboard, groove and context-menu semantics | implemented |
| Ordinary feedback | Ela tooltips and supplementary saved/generated notifications; persistent errors unchanged | implemented |
| Status bar | ElaStatusBar with point fonts, font-aware height and persistent messages | implemented |
| Candidate lists | ElaListView/ElaListWidget for search, Tags and Access; Qt item APIs and roles retained | implemented |
| Read-only results | ElaTableView for Problems/Generated/Diff; selection, copy and navigation retained | implemented |
| Type/Access editors | ElaComboBox with queued popup/commit/close and Qt cancel/input semantics | implemented |
| Window chrome | ElaAppBar on the existing QMainWindow; native hit tests and unsaved-close veto retained | implemented |
| Register/tree/field/enum views | Models, selection models, proxies and in-place delegates retained; readable monospace values | preserved |
| BitfieldView | Selection, keyboard movement and drag editing retained; complete 188px canvas in scrollable detail pane | preserved |
| AddressSpaceView | Zoom, geometry, navigation and conflict tooltips retained | preserved |
| External changes and Suite | Preview/Accept/Reject, undo/redo, protocol and deep links retained | preserved |

## First-slice verification

The independent Release build is `build/ela-migration` (Qt 6.10.2, MinGW GCC
13.1, Ninja). Core/CLI sources and file formats are unchanged. Tests and screenshots
use temporary projects and settings, never the protected example files or the
user's application profile. No source publication or package build was run.

Final evidence for this slice:

- Release build succeeded; full CTest passed **7/7**. The unchanged GUI regression
  suite reported **168 passed, 0 failed**; Ela control lifecycle tests reported
  **5 passed**, and the application-control and Classic fallback tests each
  reported **3 passed**. No Debug build was performed for this slice.
- All **16** light/dark × size × scale captures passed the hard checks below and
  were visually inspected. Light/dark state-capture tests each passed **3/3**;
  representative checked, disabled, partial and saved captures were inspected.
- `git diff --check` passed. No core, CLI or example diff exists; both protected
  blobs still match the hashes above. HEAD remains the baseline with no staged
  changes: no staging, commit, push, install or packaging was performed.
- Local compatibility patch `08-regmap-control-lifecycle.patch` passed
  `git apply --check` against the committed vendored baseline. Existing upstream
  C++20 warnings remain non-fatal; they are not represented as a warning-free build.

The implementation fixes two integration regressions exposed by unchanged GUI
tests: widget-action toolbar commands must mirror QAction metadata, and tool
buttons must retain TabFocus so a click cannot bypass active-cell validation.
The final scroll-area integration recomputes size hints when project sections
appear, pins the Register table outside header overflow, and reveals keyboard
focus without changing splitter persistence or object navigation.

The screenshot matrix is light/dark × 960×720/1440×900 × 100/125/150/200%.
It retains exact logical-window, visible-command and overlap checks, and adds
10pt UI/numeric-font checks, equal-width Latin numeric font metrics, scrollable
content bounds, a complete bitfield canvas, at least one visible Register data
row, and exact PNG pixel dimensions for each scale. Limited panes use scrollbars;
the matrix does not assert that all rows/columns fit simultaneously.

Reproduction after configuring the matching toolchain and `REGMAP_UI_BACKEND=ELA`:

```powershell
$env:PATH = "E:\QT6\Tools\mingw1310_64\bin;E:\QT6\6.10.2\mingw_64\bin;$env:PATH"
$env:QT_QPA_FONTDIR = "C:\Windows\Fonts"
cmake --build build/ela-migration --parallel 4
ctest --test-dir build/ela-migration --output-on-failure -j 2
./tests/capture_ui_snapshots.ps1 -BuildDirectory build/ela-migration -OutputDirectory build/ela-migration/screenshots
```

Evidence is retained under `build/ela-migration`: `build-final.log`,
`ctest-final.log`, `tests/regmap_gui_tests.log`, `tests/regmap_ela_controls.log`,
`screenshots.log`, and `screenshots/manifest.json` (dimensions and SHA-256 per PNG).
Light/dark batch checked/disabled/partial states and dirty/saved captures are
produced by `regmap_suiteui_control_test` with `REGMAP_UI_ARTIFACT_DIR` set to
`build/ela-migration/states/<theme>` and `REGMAP_TEST_THEME=<theme>`.

Native Windows mouse operation, cross-monitor DPI transitions, frame pacing and
frameless/Mica chrome are not claimed by offscreen validation. No computer-use
mouse interaction was performed; notify the user before any future native input
because other applications are being developed concurrently.

## Second slice: scrollbars, batch dialogs and transient feedback

Scope: Ela scrollbars on application-owned table/tree/panel/list scroll areas;
Ela OK/Cancel buttons in both batch editors; Ela tooltips on factory-created
ordinary controls; supplementary success notifications for saved/generated
results. Native window chrome, file pickers, destructive confirmations,
table editors and professional-view tooltips are not migrated in this slice.

Scrollbars retain Qt wheel/page/key/context-menu behavior without delayed range
or value animation. Batch dialogs keep QDialogButtonBox accept/reject roles and
the existing validation/transaction path. Qt does not register StandardButton
IDs for custom button subclasses, so `WorkbenchControls::standardButton` provides
the lookup; six existing GUI test lookups change to this helper, without
removing or weakening assertions. Tooltips use Qt ToolTip events, plain text,
point-based fonts and screen-bound placement without taking focus. Notifications
are limited to one per window, do not replace status/diagnostic text, and expire
or close safely, including during parent destruction and reduced-motion mode.

Visual review exposed a nested batch Tags row whose 23px container clipped its
28px controls at 200% scale. The row now propagates its layout minimum size;
application-control tests assert every visible form control fits its immediate
parent and has sufficient height for its font. Window-size, font and main-view
clipping assertions remain unchanged.

Final second-slice evidence:

- Release build succeeded; full CTest passed **7/7**. GUI regression tests
  reported **168 passed, 0 failed**; Ela control tests **8 passed**;
  application-control and Classic fallback tests each **3 passed**. No Debug
  build was performed for this slice.
- The final build regenerated all **16** light/dark × 960×720/1440×900 ×
  100/125/150/200% captures. Exact logical window sizes, 10pt UI/numeric fonts,
  visible-command/content bounds and scale-adjusted PNG dimensions passed.
  Main-window captures were visually reviewed; scrolling remains intentional
  for content that cannot fit simultaneously.
- The reproducible state script generated **80 PNGs** across both themes and
  all four scales: batch checked/disabled/partial states, dirty/saved windows,
  success notifications and tooltips. Application-control tests passed **3/3**
  in each of eight runs, and Ela control tests passed **8/8** in each of four
  scale runs. Representative state captures at every scale were visually
  inspected, including the corrected Tags row and wrapped feedback at 200%.
- `git diff --check` and `git apply --check` for local compatibility patches
  `08` and `09` passed. Core, CLI and examples have no diff; both protected
  hashes above remain unchanged. HEAD and the index remain unchanged; nothing
  was staged, committed, pushed, installed or packaged.

Reproduce the final screenshots after the Release build and CTest commands above:

```powershell
./tests/capture_ui_snapshots.ps1 -BuildDirectory build/ela-migration -OutputDirectory build/ela-migration/second-screenshots
./tests/capture_ela_states.ps1 -BuildDirectory build/ela-migration -OutputDirectory build/ela-migration/second-states
```

Final logs under `build/ela-migration`: `build-second.log`,
`ctest-second-final.log`, `tests/regmap_gui_tests.log`,
`tests/regmap_ela_controls.log`, `second-screenshots.log`, and
`second-states.log`. Both capture directories contain `manifest.json` with PNG
dimensions and SHA-256; `second-states` also contains per-theme/scale test logs.
Validation remained offscreen: no system mouse operation or native cross-monitor
DPI validation was performed.

## Third slice: remaining controls and window chrome

Completed scope: status bar; Tags/Access/search candidate lists; read-only
Problems/Generated/Diff tables; Type/Access cell editors; ElaAppBar title bar.
Retain the existing list/model APIs, validated transactions, edit commit/cancel
ordering, project-drop routing, window-state persistence and unsaved-close guard.
Keep native file pickers and destructive confirmations. Existing uncommitted
first/second-slice work is the starting state, not disposable baseline changes.

The new adapters are `workbench_item_views.cpp`, `workbench_cell_editors.cpp`
and `workbench_window_chrome.cpp`. List and result-table painters retain Qt model
roles, selection, check states, icons, elision and focus through a stable Fusion
base, while Ela owns their surfaces. This prevents a Windows accent from leaking
into semantic selection colors. Result views explicitly disable edit triggers.
The status bar releases the upstream fixed height and keeps authoritative messages.

Cell editors retain the existing delegate data/transaction paths, automatic Type
popup, Escape cancellation, text input and deferred commit/close lifetime guards.
The original GUI suite remains intact apart from the second-slice button lookup
adapter described above. No geometry, font or clipping assertion was weakened.

ElaAppBar is attached to the existing QMainWindow. Its point-font title elides
without overlapping accessible caption buttons and resolves Qt's `[*]` modified
marker. Native hit testing uses the current HWND's DPR and message coordinates;
maximize bounds use the current monitor's work area. Fullscreen removes/restores
the caption margin. Closing passes through the existing close-event guard, not
a forced native-window close. Native 125% testing exposed reentrant stylesheet
cleanup during combo destruction; destroying the popup tree before the shared
Ela style fixes the crash and is exercised by the application-lifecycle test.

Final third-slice evidence:

- Release build succeeded; full CTest passed **8/8**. GUI tests reported
  **168 passed, 0 failed**; existing Ela controls **8 passed**; application and
  Classic fallback controls each **3 passed**. New remaining-control tests
  reported **6 passed, 0 failed, 1 skipped** offscreen: the skipped Win32 case
  is exercised separately below. No Debug build was performed for this slice.
- `run_native_ela_tests.ps1` passed **7/7, no skips** in each of eight Windows
  runs (light/dark × 100/125/150/200%). It asserts actual DPR, exact 960×720 and
  1440×900 sizes, title/font/button bounds, editor cancellation and teardown,
  fullscreen/minimize/maximize/restore, close veto, resize/caption/client/maximize
  hit targets and monitor-work-area bounds. The script normalizes screen scale
  before applying the requested factor, avoiding multiplication by host DPI.
- All **16** main-window captures passed the unchanged exact-size, 10pt font,
  monospace, command/content bounds, complete bitfield and visible Register-row
  checks, plus exact scaled PNG dimensions. They were visually reviewed.
- The state matrix generated **120 PNGs**; the native matrix adds **40 PNGs**.
  Representative application, selected/checkable/diagnostic-role, editor-popup
  and elided-title states were inspected across all four scales and both themes.
  Both offscreen capture directories contain dimension/SHA-256 manifests.
- `git diff --check` and patch `10`'s `git apply --check` against the second-slice
  source snapshot passed. Core/CLI/examples have no diff; protected example
  hashes remain exactly as recorded above. HEAD remains
  `d3c46cebf3500228cfbdae19354c0e9adc5fe831`, with an empty index. No staging,
  commit, push, install, package or AppPackage operation was performed.

Reproduce with the same Release toolchain and PATH used above:

```powershell
cmake --build build/ela-migration --parallel 4
ctest --test-dir build/ela-migration --output-on-failure -j 2
./tests/capture_ui_snapshots.ps1 -BuildDirectory build/ela-migration -OutputDirectory build/ela-migration/third-screenshots
./tests/capture_ela_states.ps1 -BuildDirectory build/ela-migration -OutputDirectory build/ela-migration/third-states
./tests/run_native_ela_tests.ps1 -BuildDirectory build/ela-migration -OutputDirectory build/ela-migration/native-third
```

Final logs under `build/ela-migration`: `build-third-final.log`,
`ctest-third-final.log`, `third-screenshots.log`, `third-states.log`,
`native-third.log`, and the per-theme/scale logs under `native-third`.
Native validation requires a Windows desktop large enough for the 2880×1800
physical test window; it does not relax logical-size assertions to fit a screen.
All fixtures/settings are temporary. QtTest events do not inject system mouse
input. Computer-use discovery did not expose a targetable preview window, so no
physical mouse input was sent. Real mouse drag/Snap, cross-monitor DPI transitions
and frame pacing remain outside the verified claims; give advance notice before
future computer-use input. Upstream C++20 warnings remain non-fatal.

## Release 0.3.3 validation (2026-09-22)

The release contains the completed three slices above, not the later candidate
work on ordinary cell editors, dialog shells, labels/cards or bitfield tooltips.
Version metadata and portable-package verification are the only release additions.

- Release rebuild passed. Full CTest passed **8/8**; GUI tests passed **168/168**.
  Evidence: `build/ela-migration/build-release-0.3.3.log`,
  `ctest-release-0.3.3.log` and `tests/regmap_gui_tests.log`.
- All **16** new screenshots passed the unchanged exact logical-size, 10pt font,
  content-bound and physical-PNG-size assertions and were visually inspected.
  Evidence: `release-0.3.3-screenshots/manifest.json` and
  `release-0.3.3-screenshots.log` under the same build directory.
- Fresh native light/dark runs at 100% each passed **7/7**. The current desktop
  reports 1920x1080. At 125%, Windows clamps the requested 1440x900 logical window
  to 1440x880; the hard assertion fails and the matrix stops before higher scales.
  This reproduces outside the sandbox. Logs are `release-0.3.3-native.log` and
  `release-0.3.3-native-desktop.log`. The earlier complete native matrix remains
  historical evidence, not a claim that this release rerun passed every scale.
  No test assertion or display setting was changed to bypass the limitation.
- Protected example hashes remain the baseline values above. Core, CLI,
  generators and file-format sources are unchanged. No system mouse input or
  `E:\PinloomRoot\AppPackage` operation is part of this release.

The archive is built from the clean release commit using the existing CPack ZIP
target, retaining prior archives. `BUILD-INFO.txt` records `UI backend: ELA`;
the package includes the Ela DLL, provenance, font license and compatibility
patches. Check the resulting archive with PowerShell 7:

```powershell
cmake --build build/ela-migration --target package --parallel 4
$revision = git rev-parse --short=7 HEAD
./tests/check_portable_package.ps1 -Archive "out/RegMapWorkbench-0.3.3-win64-$revision.zip" -ExpectedVersion 0.3.3 -ExpectedRevision $revision
```

The checker verifies SHA-256, clean-source metadata and required package entries,
then extracts into a fresh `.tmp/portable-smoke` directory. With only Windows
system directories on PATH and no developer Qt/plugin environment, it checks GUI
version/help and CLI version/init/validate/generate/current-output status. It uses
its own project/profile, retains logs, and never opens the protected examples.
