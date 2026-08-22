cmake_minimum_required(VERSION 3.16)
project(archpaper LANGUAGES C CXX)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# Núcleo compartido en C (biblioteca estática)
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

# Ejecutable principal: CLI + GUI Qt (UI programática)
add_executable(archpaper
    src/cli/main.c
    src/gui/main.cpp
    src/gui/mainwindow.cpp
    src/gui/mainwindow.h
)

target_link_libraries(archpaper PRIVATE
    archpaper_core
    Qt6::Widgets
)

# Instalación
include(GNUInstallDirs)
install(TARGETS archpaper
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

