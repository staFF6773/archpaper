cmake_minimum_required(VERSION 3.16)
project(archpaper LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets Network)

# Shared C core (static library)
add_library(archpaper_core STATIC
    src/core/backend.c
    src/core/config.c
    src/core/daemon.c
    src/core/utils.c
    src/core/wallust.c
)

target_include_directories(archpaper_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_compile_definitions(archpaper_core PRIVATE _DEFAULT_SOURCE)

# Main executable: CLI + Qt GUI
add_executable(archpaper
    src/cli/main.c
    src/gui/main.cpp
    src/gui/mainwindow.cpp
    src/gui/mainwindow.h
    src/gui/components/navsidebar.cpp
    src/gui/components/navsidebar.h
    src/gui/components/folderspanel.cpp
    src/gui/components/folderspanel.h
    src/gui/components/wallpapergrid.cpp
    src/gui/components/wallpapergrid.h
    src/gui/components/previewpanel.cpp
    src/gui/components/previewpanel.h
    src/gui/components/settingspanel.cpp
    src/gui/components/settingspanel.h
    src/gui/components/marketgrid.cpp
    src/gui/components/marketgrid.h
    src/gui/components/marketpreviewpanel.cpp
    src/gui/components/marketpreviewpanel.h
    src/gui/components/marketpanel.cpp
    src/gui/components/marketpanel.h
    src/gui/market/marketprovider.h
    src/gui/market/wallhavenprovider.cpp
    src/gui/market/wallhavenprovider.h
    src/gui/market/moewallsprovider.cpp
    src/gui/market/moewallsprovider.h
    src/gui/market/marketservice.cpp
    src/gui/market/marketservice.h
    src/gui/theme/resources.qrc
)

target_link_libraries(archpaper PRIVATE
    archpaper_core
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    Qt6::Network
)

# Installation
include(GNUInstallDirs)
install(TARGETS archpaper
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

