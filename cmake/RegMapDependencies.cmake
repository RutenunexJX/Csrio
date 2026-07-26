find_package(yaml-cpp 0.8 CONFIG QUIET)

if(NOT TARGET yaml-cpp::yaml-cpp AND NOT TARGET yaml-cpp)
    include(FetchContent)

    set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
    set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG 0.8.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(yaml-cpp)
endif()

if(TARGET yaml-cpp::yaml-cpp)
    set(REGMAP_YAML_TARGET yaml-cpp::yaml-cpp)
elseif(TARGET yaml-cpp)
    set(REGMAP_YAML_TARGET yaml-cpp)
else()
    message(FATAL_ERROR "A yaml-cpp target was not provided")
endif()

include(FetchContent)
find_package(Git REQUIRED)
set(QT_VERSION_MAJOR 6)
FetchContent_Declare(
    QXlsx
    GIT_REPOSITORY https://github.com/QtExcel/QXlsx.git
    GIT_TAG 8a13e1c86e5d4fb5e3b2fb09c7b632514f1d54ca
    SOURCE_SUBDIR QXlsx
    PATCH_COMMAND
        "${CMAKE_COMMAND}"
        "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
        "-DSOURCE_DIR=<SOURCE_DIR>"
        "-DPATCH_FILE=${CMAKE_CURRENT_LIST_DIR}/patches/qxlsx-absolute-opc-targets.patch"
        -P "${CMAKE_CURRENT_LIST_DIR}/ApplyPatch.cmake"
        COMMAND
        "${CMAKE_COMMAND}"
        "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
        "-DSOURCE_DIR=<SOURCE_DIR>"
        "-DPATCH_FILE=${CMAKE_CURRENT_LIST_DIR}/patches/qxlsx-summary-rows-above.patch"
        -P "${CMAKE_CURRENT_LIST_DIR}/ApplyPatch.cmake"
        COMMAND
        "${CMAKE_COMMAND}"
        "-DGIT_EXECUTABLE=${GIT_EXECUTABLE}"
        "-DSOURCE_DIR=<SOURCE_DIR>"
        "-DPATCH_FILE=${CMAKE_CURRENT_LIST_DIR}/patches/qxlsx-freeze-filter.patch"
        -P "${CMAKE_CURRENT_LIST_DIR}/ApplyPatch.cmake"
)
FetchContent_MakeAvailable(QXlsx)

if(TARGET QXlsx::QXlsx)
    set(REGMAP_XLSX_TARGET QXlsx::QXlsx)
else()
    message(FATAL_ERROR "A QXlsx::QXlsx target was not provided")
endif()
