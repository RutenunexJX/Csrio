cmake_minimum_required(VERSION 3.24)

project(
    RegMapWorkbench
    VERSION 0.3.1
    DESCRIPTION "Register map workbench for FPGA and SystemVerilog development"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(REGMAP_BUILD_APP "Build the Qt desktop application" ON)
option(REGMAP_BUILD_CLI "Build the regmapc command-line interface" ON)
option(REGMAP_BUILD_TESTS "Build unit tests" ON)
option(REGMAP_ENABLE_SUITEUI "Use the pinned optional SuiteUi control renderer" OFF)

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
