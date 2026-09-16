cmake_minimum_required(VERSION 3.16)
project(archpaper LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
option(ARCHPAPER_BUILD_GUI "Build the optional Qt interface" ON)
find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONC REQUIRED IMPORTED_TARGET json-c)
pkg_check_modules(PNG REQUIRED IMPORTED_TARGET libpng)
pkg_check_modules(LZ4 REQUIRED IMPORTED_TARGET liblz4)

# Shared C core (static library)
add_library(archpaper_core STATIC
    src/core/backend.c
    src/core/config.c
    src/core/daemon.c
    src/core/utils.c
    src/core/wallust.c
    src/core/error.c
    src/core/process.c
    src/core/storage.c
    src/core/cache.c
    src/core/library.c
    src/core/history.c
    src/core/wallpaper.c
    src/core/engine.c
    src/core/export.c
    src/core/texture.c
)

target_include_directories(archpaper_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_definitions(archpaper_core PRIVATE _DEFAULT_SOURCE)
target_link_libraries(archpaper_core PUBLIC PkgConfig::JSONC PkgConfig::PNG PkgConfig::LZ4)
target_compile_options(archpaper_core PRIVATE -Wall -Wextra -Wpedantic)

if(ARCHPAPER_BUILD_GUI)
    set(CLI_TARGET archpaper-cli)
else()
    set(CLI_TARGET archpaper)
endif()
add_executable(${CLI_TARGET} src/cli/entry.c src/cli/main.c)
target_link_libraries(${CLI_TARGET} PRIVATE archpaper_core)

if(ARCHPAPER_BUILD_GUI)
enable_language(CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Main executable: CLI + Qt GUI
add_executable(archpaper
    src/cli/main.c
    src/gui/main.cpp
    src/gui/mainwindow.cpp
    src/gui/mainwindow.h
    src/gui/components/navsidebar.cpp
    src/gui/components/navsidebar.h
    src/gui/components/wallpapergrid.cpp
    src/gui/components/wallpapergrid.h
    src/gui/components/previewpanel.cpp
    src/gui/components/previewpanel.h
    src/gui/components/settingspanel.cpp
    src/gui/components/settingspanel.h
    src/gui/components/exportdialog.cpp
    src/gui/components/exportdialog.h
    src/gui/theme/resources.qrc
)

target_link_libraries(archpaper PRIVATE
    archpaper_core
    Qt6::Widgets
)
set_target_properties(archpaper PROPERTIES AUTOMOC ON AUTORCC ON)
endif()

# Installation
include(GNUInstallDirs)
install(TARGETS archpaper
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
if(ARCHPAPER_BUILD_GUI)
    install(TARGETS archpaper-cli RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(process-helper tests/process_helper.c)
    add_executable(core-test tests/core_test.c)
    target_link_libraries(core-test PRIVATE archpaper_core)
    add_test(NAME core COMMAND core-test $<TARGET_FILE:process-helper> $<TARGET_FILE:${CLI_TARGET}>)
    set_tests_properties(core PROPERTIES TIMEOUT 30)
    add_executable(export-test tests/export_test.c)
    target_link_libraries(export-test PRIVATE archpaper_core)
    add_test(NAME export COMMAND export-test)
    set_tests_properties(export PROPERTIES TIMEOUT 30)
    if(ARCHPAPER_BUILD_GUI)
        add_executable(export-gui-test tests/export_gui_test.cpp src/gui/components/exportdialog.cpp)
        target_include_directories(export-gui-test PRIVATE src/gui)
        target_link_libraries(export-gui-test PRIVATE archpaper_core Qt6::Widgets)
        add_test(NAME export-gui COMMAND export-gui-test)
        set_tests_properties(export-gui PROPERTIES TIMEOUT 15 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
    endif()
endif()
