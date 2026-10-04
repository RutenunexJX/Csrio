# Third-party components

Original Csrio application code is Apache-2.0; see LICENSE and NOTICE. The
following components retain their original copyrights and licenses.

| Component | License / provenance | Full notices |
| --- | --- | --- |
| ElaWidgetTools | MIT; Liniyous, upstream `454cac2d57a47d3cc28577dc817793aec1881ca7` | `thirdparty/elawidgettools/LICENSE`, `REGMAP-NOTICE.md`, `UPSTREAM-REVISION.md`, patches |
| ZeroSlack compatibility changes | Apache-2.0; exact imports recorded in REGMAP-NOTICE.md | `thirdparty/elawidgettools/ZeroSlack-Apache-2.0.txt` |
| Font Awesome Free Solid 6.7.2 | SIL OFL 1.1; Fonticons, Inc. | `thirdparty/elawidgettools/Font/FontAwesome-LICENSE.txt` |
| yaml-cpp | MIT; Jesse Beder | `thirdparty/runtime-licenses/yaml-cpp-MIT.txt`; pinned fallback 0.8.0 in `cmake/RegMapDependencies.cmake` |
| QXlsx | MIT; QXlsx contributors | `thirdparty/runtime-licenses/QXlsx-MIT.txt`; commit `8a13e1c86e5d4fb5e3b2fb09c7b632514f1d54ca` and local patches in `cmake/patches` |
| Qt 6.10.2 | LGPLv3 for selected modules, plus bundled dependencies' own terms | `thirdparty/runtime-licenses/Qt-LGPLv3.txt`, `GCC-COPYING3.txt` |
| MinGW GCC, MinGW-w64, winpthreads | Upstream licenses and GCC runtime exception | `thirdparty/runtime-licenses/` |

The optional SuiteApp/SuiteUi SDKs are separately installed dependencies. Their
redistribution permission is not supplied by this repository's license.
See [asset provenance](docs/ASSET-PROVENANCE.md) for the application icon.

Before distributing public binaries, provide the corresponding sources and
required relinking/installation information for the actual Qt build and review
its bundled dependency notices. The application does not restrict the rights
granted by those library licenses. See
https://www.qt.io/development/open-source-lgpl-obligations and the remaining
[public-release checks](docs/PUBLIC-RELEASE-REVIEW.md).
