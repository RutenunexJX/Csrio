find_package(Qt6 6.5 REQUIRED COMPONENTS Test)

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
    target_compile_features(regmap_gui_tests PRIVATE cxx_std_20)
    regmap_enable_warnings(regmap_gui_tests)
    add_test(NAME regmap_gui_tests COMMAND regmap_gui_tests)
    set_tests_properties(
        regmap_gui_tests
        PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
            ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
    )
endif()
