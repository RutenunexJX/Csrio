cmake_minimum_required(VERSION 3.24)

project(
    RegMapWorkbench
    VERSION 0.1.0
    DESCRIPTION "Register map workbench for FPGA and SystemVerilog development"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(REGMAP_BUILD_APP "Build the Qt desktop application" ON)
option(REGMAP_BUILD_TESTS "Build unit tests" ON)

if(WIN32 AND REGMAP_BUILD_APP)
    set(CMAKE_INSTALL_BINDIR "." CACHE STRING "Runtime installation directory" FORCE)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(RegMapWarnings)

set(QT_NO_PRIVATE_MODULE_WARNING ON)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui GuiPrivate)
include(RegMapDependencies)

add_subdirectory(src/core)

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
