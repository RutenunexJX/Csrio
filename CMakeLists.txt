cmake_minimum_required(VERSION 3.24)

project(
    Csrio
    VERSION 0.4.1
    DESCRIPTION "Csrio register-map editor for FPGA and SystemVerilog development"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(REGMAP_BUILD_APP "Build the Qt desktop application" ON)
option(REGMAP_BUILD_CLI "Build the regmapc command-line interface" ON)
option(REGMAP_BUILD_TESTS "Build unit tests" ON)
if(DEFINED REGMAP_ENABLE_SUITEUI AND NOT DEFINED REGMAP_UI_BACKEND)
    if(REGMAP_ENABLE_SUITEUI)
        set(REGMAP_UI_BACKEND "SUITEUI" CACHE STRING "Control backend")
    else()
        set(REGMAP_UI_BACKEND "CLASSIC" CACHE STRING "Control backend")
    endif()
endif()
set(REGMAP_UI_BACKEND "ELA" CACHE STRING "Control backend: ELA, SUITEUI, or CLASSIC")
set_property(CACHE REGMAP_UI_BACKEND PROPERTY STRINGS ELA SUITEUI CLASSIC)
if(NOT REGMAP_UI_BACKEND MATCHES "^(ELA|SUITEUI|CLASSIC)$")
    message(FATAL_ERROR "REGMAP_UI_BACKEND must be ELA, SUITEUI, or CLASSIC")
endif()
set(REGMAP_ENABLE_SUITEUI OFF)
set(REGMAP_ENABLE_ELA OFF)
if(REGMAP_UI_BACKEND STREQUAL "SUITEUI")
    set(REGMAP_ENABLE_SUITEUI ON)
elseif(REGMAP_UI_BACKEND STREQUAL "ELA")
    set(REGMAP_ENABLE_ELA ON)
endif()

if(WIN32 AND REGMAP_BUILD_APP)
    set(CMAKE_INSTALL_BINDIR "." CACHE STRING "Runtime installation directory" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(RegMapWarnings)

set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui GuiPrivate)
find_package(SuiteApp 1 CONFIG QUIET)
if(REGMAP_BUILD_APP AND REGMAP_ENABLE_SUITEUI)
    find_package(SuiteUi 0.1.0 EXACT CONFIG REQUIRED)
endif()
if(REGMAP_BUILD_APP AND REGMAP_ENABLE_ELA)
    # ElaTabBar uses Qt private APIs; this revision is validated with Qt 6.10.2.
    find_package(Qt6 6.10.2 EXACT REQUIRED COMPONENTS Widgets WidgetsPrivate)
    set(ELAWIDGETTOOLS_BUILD_STATIC_LIB OFF CACHE BOOL "Use the Ela DLL" FORCE)
    add_subdirectory(thirdparty/elawidgettools EXCLUDE_FROM_ALL)
    set_target_properties(ElaWidgetTools PROPERTIES AUTOMOC ON AUTORCC ON)
endif()
include(RegMapDependencies)

add_subdirectory(src/core)

if(REGMAP_BUILD_CLI)
    add_subdirectory(src/cli)
endif()

if(REGMAP_BUILD_APP)
    add_subdirectory(src/app)
endif()

if(REGMAP_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()

if(WIN32 AND REGMAP_BUILD_APP)
    include(RegMapPackaging)
endif()
