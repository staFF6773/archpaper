/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include <QApplication>
#include <QImageReader>
#include "mainwindow.h"

extern "C" {
#include "archpaper/cli.h"
}

int main(int argc, char *argv[]) {
    /* If arguments are passed, act as CLI (useful for autostart/scripts). */
    if (argc > 1) {
        return archpaper_cli(argc, argv);
    }

    QApplication app(argc, argv);
    app.setApplicationName("archpaper");
    app.setApplicationDisplayName("archpaper");

    /* Remove Qt's 256 MB allocation limit for large images. */
    QImageReader::setAllocationLimit(0);

    MainWindow window;
    window.show();

    return app.exec();
}
