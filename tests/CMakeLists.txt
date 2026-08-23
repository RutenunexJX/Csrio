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
