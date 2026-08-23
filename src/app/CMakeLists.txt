find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)

add_library(
    regmap_workbench_ui
    STATIC
    address_space_view.cpp
    address_space_view.hpp
    bitfield_view.cpp
    bitfield_view.hpp
    main_window.cpp
    main_window.hpp
    project_controller.cpp
    project_controller.hpp
    source_navigation.cpp
    source_navigation.hpp
    startup_options.cpp
    startup_options.hpp
    table_selection_utils.cpp
    table_selection_utils.hpp
    workbench_theme.cpp
    workbench_theme.hpp
)
add_library(RegMap::WorkbenchUi ALIAS regmap_workbench_ui)
if(SuiteApp_FOUND)
    target_sources(
        regmap_workbench_ui
        PRIVATE
            suite_integration.cpp
            suite_integration.hpp
    )
    target_link_libraries(regmap_workbench_ui PRIVATE SuiteApp::suiteapp)
endif()
set_target_properties(regmap_workbench_ui PROPERTIES AUTOMOC ON)
target_link_libraries(regmap_workbench_ui PUBLIC RegMap::Core Qt6::Widgets)
target_include_directories(regmap_workbench_ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(regmap_workbench_ui PUBLIC cxx_std_20)
regmap_enable_warnings(regmap_workbench_ui)

qt_add_executable(regmap_workbench WIN32 main.cpp)
if(WIN32)
    target_sources(regmap_workbench PRIVATE regmap_workbench.rc)
endif()
qt_add_resources(
    regmap_workbench
    regmap_workbench_assets
    PREFIX "/icons"
    BASE "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/regmap_workbench_icon.png"
)
set_target_properties(regmap_workbench PROPERTIES AUTOMOC ON)
set_target_properties(regmap_workbench PROPERTIES OUTPUT_NAME RegMapWorkbench)
target_link_libraries(regmap_workbench PRIVATE RegMap::WorkbenchUi)
target_compile_definitions(
    regmap_workbench
    PRIVATE REGMAP_APP_VERSION="${PROJECT_VERSION}"
)
if(SuiteApp_FOUND)
    target_compile_definitions(regmap_workbench PRIVATE REGMAP_HAS_SUITEAPP=1)
endif()
target_compile_features(regmap_workbench PRIVATE cxx_std_20)
regmap_enable_warnings(regmap_workbench)

if(WIN32)
    install(
        TARGETS regmap_workbench
        RUNTIME DESTINATION .
        COMPONENT Runtime
    )
    qt_generate_deploy_app_script(
        TARGET regmap_workbench
        OUTPUT_SCRIPT regmap_workbench_deploy_script
        NO_TRANSLATIONS
    )
    install(
        SCRIPT "${regmap_workbench_deploy_script}"
        COMPONENT Runtime
    )
    install(FILES "${PROJECT_SOURCE_DIR}/README.md" DESTINATION . COMPONENT Runtime)
    install(
        FILES "${PROJECT_SOURCE_DIR}/docs/cli.md"
        DESTINATION docs
        COMPONENT Runtime
    )
endif()
