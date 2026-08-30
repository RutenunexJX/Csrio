add_library(
    regmap_core
    src/external_changes.cpp
    src/generation.cpp
    src/manifest.cpp
    src/model.cpp
    src/model_tokens.cpp
    src/project.cpp
    src/project_creation.cpp
    src/rtl_sync.cpp
    src/serialization.cpp
    src/sync_diff.cpp
    src/three_way_merge.cpp
    src/unsigned_value.cpp
    src/validation.cpp
    src/workspace_store.cpp
    src/xlsx_export.cpp
)
add_library(RegMap::Core ALIAS regmap_core)

target_include_directories(
    regmap_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(regmap_core PUBLIC cxx_std_20)
target_link_libraries(
    regmap_core
    PRIVATE
        Qt6::Core
        Qt6::Gui
        ${REGMAP_XLSX_TARGET}
        ${REGMAP_YAML_TARGET}
)
regmap_enable_warnings(regmap_core)
