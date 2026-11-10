add_library(
    regmap_core
    src/atomic_file_writer.cpp
    src/external_changes.cpp
    src/file_io.cpp
    src/document_layout.cpp
    src/consistency.cpp
    src/generation.cpp
    src/manifest.cpp
    src/model.cpp
    src/model_tokens.cpp
    src/project.cpp
    src/project_creation.cpp
    src/references.cpp
    src/rtl_sync.cpp
    src/serialization.cpp
    src/sync_diff.cpp
    src/three_way_merge.cpp
    src/unsigned_value.cpp
    src/validation.cpp
    src/workspace_store.cpp
    src/xlsx_render_process.cpp
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

add_executable(regmap_xlsx_renderer src/xlsx_renderer_main.cpp src/xlsx_export.cpp)
set_target_properties(regmap_xlsx_renderer PROPERTIES OUTPUT_NAME regmap-xlsx-render)
target_link_libraries(regmap_xlsx_renderer PRIVATE RegMap::Core Qt6::Gui ${REGMAP_XLSX_TARGET})
file(SHA256 "${PROJECT_SOURCE_DIR}/thirdparty/noto-sans-sc/NotoSansSC.ttf" REGMAP_XLSX_FONT_SHA256)
if(NOT REGMAP_XLSX_FONT_SHA256 STREQUAL "a3041811a78c361b1de50f953c805e0244951c21c5bd412f7232ef0d899af0da")
    message(FATAL_ERROR "The pinned XLSX Noto Sans SC font is missing or has changed")
endif()
target_compile_definitions(regmap_xlsx_renderer PRIVATE REGMAP_XLSX_FONT_SHA256="${REGMAP_XLSX_FONT_SHA256}")
regmap_enable_warnings(regmap_xlsx_renderer)
install(TARGETS regmap_xlsx_renderer RUNTIME DESTINATION . COMPONENT Runtime)
install(FILES "${PROJECT_SOURCE_DIR}/thirdparty/noto-sans-sc/NotoSansSC.ttf"
    "${PROJECT_SOURCE_DIR}/thirdparty/noto-sans-sc/OFL.txt"
    DESTINATION resources/xlsx COMPONENT Runtime)
