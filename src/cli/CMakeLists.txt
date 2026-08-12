add_library(
    regmap_cli_support
    cli_app.cpp
    cli_app.hpp
)
add_library(RegMap::Cli ALIAS regmap_cli_support)

target_include_directories(
    regmap_cli_support
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_features(regmap_cli_support PUBLIC cxx_std_20)
target_compile_definitions(
    regmap_cli_support
    PUBLIC
        REGMAP_CLI_VERSION="${PROJECT_VERSION}"
)
target_link_libraries(
    regmap_cli_support
    PUBLIC
        RegMap::Core
        Qt6::Core
        Qt6::Gui
)
regmap_enable_warnings(regmap_cli_support)

add_executable(regmapc main.cpp)
target_compile_features(regmapc PRIVATE cxx_std_20)
target_link_libraries(regmapc PRIVATE RegMap::Cli)
regmap_enable_warnings(regmapc)

if(WIN32)
    install(
        TARGETS regmapc
        RUNTIME DESTINATION .
        COMPONENT Runtime
    )
endif()
