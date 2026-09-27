find_package(Qt6 6.5 REQUIRED COMPONENTS Test)
get_filename_component(
    REGMAP_TEST_TOOLCHAIN_RUNTIME_DIR
    "${CMAKE_CXX_COMPILER}"
    DIRECTORY
)

qt_add_executable(regmap_core_tests core_tests.cpp)
set_target_properties(regmap_core_tests PROPERTIES AUTOMOC ON)
target_link_libraries(
    regmap_core_tests
    PRIVATE
        RegMap::Core
        Qt6::Test
        ${REGMAP_XLSX_TARGET}
)
target_compile_features(regmap_core_tests PRIVATE cxx_std_20)
target_compile_definitions(
    regmap_core_tests
    PRIVATE
        REGMAP_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)
regmap_enable_warnings(regmap_core_tests)

qt_add_executable(regmap_workspace_performance workspace_performance.cpp)
target_link_libraries(regmap_workspace_performance PRIVATE RegMap::Core)
if(WIN32)
    target_link_libraries(regmap_workspace_performance PRIVATE psapi)
endif()
regmap_enable_warnings(regmap_workspace_performance)

add_test(NAME regmap_core_tests COMMAND regmap_core_tests)
set_tests_properties(
    regmap_core_tests
    PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

if(TARGET RegMap::Cli)
    qt_add_executable(
        regmap_cli_tests
        cli_tests.cpp
    )
    set_target_properties(
        regmap_cli_tests
        PROPERTIES
            AUTOMOC ON
    )
    target_link_libraries(
        regmap_cli_tests
        PRIVATE
            RegMap::Cli
            Qt6::Test
    )
    target_compile_features(
        regmap_cli_tests
        PRIVATE
            cxx_std_20
    )
    if(TARGET regmapc)
        add_dependencies(
            regmap_cli_tests
            regmapc
        )
        target_compile_definitions(
            regmap_cli_tests
            PRIVATE
                REGMAP_CLI_EXECUTABLE="$<TARGET_FILE:regmapc>"
                REGMAP_TEST_QT_RUNTIME_DIR="$<TARGET_FILE_DIR:Qt6::Core>"
                REGMAP_TEST_TOOLCHAIN_RUNTIME_DIR="${REGMAP_TEST_TOOLCHAIN_RUNTIME_DIR}"
        )
    endif()
    regmap_enable_warnings(
        regmap_cli_tests)
    add_test(
        NAME regmap_cli_tests
        COMMAND regmap_cli_tests
    )
    set_tests_properties(
        regmap_cli_tests
        PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    )
endif()

if(TARGET RegMap::WorkbenchUi)
    find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
    qt_add_executable(regmap_usability_tests usability_tests.cpp)
    set_target_properties(regmap_usability_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(regmap_usability_tests PRIVATE RegMap::WorkbenchUi Qt6::Test)
    regmap_enable_warnings(regmap_usability_tests)
    add_test(NAME regmap_usability_tests COMMAND regmap_usability_tests)
    set_tests_properties(regmap_usability_tests PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    qt_add_executable(regmap_gui_tests gui_smoke_tests.cpp)
    set_target_properties(regmap_gui_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(
        regmap_gui_tests
        PRIVATE
            RegMap::WorkbenchUi
            Qt6::Test
            ${REGMAP_XLSX_TARGET}
    )
    if(TARGET RegMap::Cli)
        target_link_libraries(
            regmap_gui_tests
            PRIVATE
                RegMap::Cli
        )
        target_compile_definitions(
            regmap_gui_tests
            PRIVATE
                REGMAP_GUI_TEST_HAS_CLI=1
        )
    endif()
    if(SuiteApp_FOUND)
        target_link_libraries(
            regmap_gui_tests
            PRIVATE SuiteApp::suiteapp
        )
        target_compile_definitions(
            regmap_gui_tests
            PRIVATE REGMAP_GUI_TEST_HAS_SUITEAPP=1
        )
    endif()
    if(TARGET regmap_workbench)
        add_dependencies(
            regmap_gui_tests
            regmap_workbench
        )
        target_compile_definitions(
            regmap_gui_tests
            PRIVATE
                REGMAP_WORKBENCH_EXECUTABLE="$<TARGET_FILE:regmap_workbench>"
                REGMAP_TEST_TOOLCHAIN_RUNTIME_DIR="${REGMAP_TEST_TOOLCHAIN_RUNTIME_DIR}"
        )
    endif()
    target_compile_features(regmap_gui_tests PRIVATE cxx_std_20)
    target_compile_definitions(regmap_gui_tests PRIVATE
        REGMAP_TEST_UI_INITIALIZATION REGMAP_EXPECTED_VERSION="${PROJECT_VERSION}")
    regmap_enable_warnings(regmap_gui_tests)
    add_test(
        NAME regmap_gui_tests
        COMMAND regmap_gui_tests
            -o "${CMAKE_CURRENT_BINARY_DIR}/regmap_gui_tests.log,txt"
    )
    set_tests_properties(
        regmap_gui_tests
        PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    )

    qt_add_executable(
        regmap_ui_snapshot
        ui_snapshot.cpp
    )
    set_target_properties(
        regmap_ui_snapshot
        PROPERTIES AUTOMOC ON
    )
    target_link_libraries(
        regmap_ui_snapshot
        PRIVATE
            RegMap::WorkbenchUi
            Qt6::Widgets
            ${REGMAP_XLSX_TARGET}
    )
    target_compile_features(
        regmap_ui_snapshot
        PRIVATE cxx_std_20
    )
    regmap_enable_warnings(regmap_ui_snapshot)

    if(SuiteApp_FOUND)
        qt_add_executable(
            regmap_suiteapp_integration_test
            suiteapp_integration_test.cpp
        )
        set_target_properties(
            regmap_suiteapp_integration_test
            PROPERTIES AUTOMOC ON
        )
        target_link_libraries(
            regmap_suiteapp_integration_test
            PRIVATE
                RegMap::WorkbenchUi
                SuiteApp::suiteapp
                Qt6::Test
        )
        target_compile_features(
            regmap_suiteapp_integration_test
            PRIVATE cxx_std_20
        )
        regmap_enable_warnings(regmap_suiteapp_integration_test)
        add_test(
            NAME regmap_suiteapp_integration_test
            COMMAND regmap_suiteapp_integration_test
        )
        set_tests_properties(
            regmap_suiteapp_integration_test
            PROPERTIES
                ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
                ENVIRONMENT_MODIFICATION
                    "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        )
    endif()
endif()

foreach(target regmap_gui_tests regmap_core_tests regmap_cli_tests regmap_suiteapp_integration_test)
    if(TARGET ${target})
        target_sources(${target} PRIVATE test_profile.cpp)
    endif()
endforeach()
if(REGMAP_BUILD_APP)
    qt_add_executable(regmap_suiteui_control_test suiteui_controls_test.cpp)
    set_target_properties(regmap_suiteui_control_test PROPERTIES AUTOMOC ON)
    target_link_libraries(regmap_suiteui_control_test PRIVATE RegMap::WorkbenchUi Qt6::Test)
    target_compile_definitions(regmap_suiteui_control_test PRIVATE
        REGMAP_TEST_SUITEUI_ENABLED=$<BOOL:${REGMAP_ENABLE_SUITEUI}>
        REGMAP_TEST_ELA_ENABLED=$<BOOL:${REGMAP_ENABLE_ELA}>)
    add_test(NAME regmap_suiteui_controls COMMAND regmap_suiteui_control_test
        -o "${CMAKE_CURRENT_BINARY_DIR}/regmap_suiteui_controls.log,txt")
    set_tests_properties(regmap_suiteui_controls PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
endif()

if(REGMAP_BUILD_APP AND REGMAP_ENABLE_ELA)
    qt_add_executable(regmap_ela_native_tests ela_native_capabilities_test.cpp test_profile.cpp)
    set_target_properties(regmap_ela_native_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(regmap_ela_native_tests PRIVATE RegMap::WorkbenchUi Qt6::Test Ela::WidgetTools)
    foreach(scale 1 2)
        add_test(NAME regmap_ela_native_${scale} COMMAND regmap_ela_native_tests
            -o "${CMAKE_CURRENT_BINARY_DIR}/regmap_ela_native_${scale}.log,txt")
        set_tests_properties(regmap_ela_native_${scale} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen;REGMAP_UI_STYLE=ela;QT_SCALE_FACTOR=${scale};QT_FONT_DPI=96;QT_SCREEN_SCALE_FACTORS=1"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    endforeach()
    qt_add_executable(regmap_ui_performance ui_performance.cpp)
    set_target_properties(regmap_ui_performance PROPERTIES AUTOMOC ON)
    target_link_libraries(regmap_ui_performance PRIVATE RegMap::WorkbenchUi Qt6::Test)
    qt_add_executable(regmap_ela_remaining_tests ela_remaining_controls_test.cpp)
    set_target_properties(regmap_ela_remaining_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(regmap_ela_remaining_tests PRIVATE RegMap::WorkbenchUi Qt6::Test)
    add_test(NAME regmap_ela_remaining COMMAND regmap_ela_remaining_tests
        -o "${CMAKE_CURRENT_BINARY_DIR}/regmap_ela_remaining.log,txt")
    set_tests_properties(regmap_ela_remaining PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;REGMAP_UI_STYLE=ela"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    qt_add_executable(regmap_ela_control_tests ela_controls_test.cpp test_profile.cpp)
    set_target_properties(regmap_ela_control_tests PROPERTIES AUTOMOC ON)
    target_link_libraries(regmap_ela_control_tests PRIVATE RegMap::WorkbenchUi Qt6::Test)
    add_test(NAME regmap_ela_controls COMMAND regmap_ela_control_tests
        -o "${CMAKE_CURRENT_BINARY_DIR}/regmap_ela_controls.log,txt")
    set_tests_properties(regmap_ela_controls PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;REGMAP_UI_STYLE=ela"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    add_test(NAME regmap_classic_controls COMMAND regmap_suiteui_control_test
        -o "${CMAKE_CURRENT_BINARY_DIR}/regmap_classic_controls.log,txt")
    set_tests_properties(regmap_classic_controls PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen;REGMAP_UI_STYLE=classic"
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")
    if(WIN32)
        foreach(target regmap_ela_native_tests regmap_ui_performance regmap_gui_tests regmap_ui_snapshot regmap_suiteapp_integration_test
                       regmap_suiteui_control_test regmap_ela_control_tests regmap_ela_remaining_tests)
            if(TARGET ${target})
                add_custom_command(TARGET ${target} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                        $<TARGET_FILE:ElaWidgetTools> $<TARGET_FILE_DIR:${target}>)
            endif()
        endforeach()
    endif()
endif()
