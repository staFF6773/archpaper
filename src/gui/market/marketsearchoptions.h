/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef MARKETSEARCHOPTIONS_H
#define MARKETSEARCHOPTIONS_H

#include <QString>

struct MarketSearchOptions {
    QString query;
    QString source;     // "all", "wallhaven", "moewalls"
    int page = 1;

    // Wallhaven specific
    QString sorting;    // "relevance", "latest", "random", "views", "favorites", "toplist"
    QString topRange;   // "1d", "3d", "1w", "1M", "3M", "6M", "1y"
    QString resolution; // e.g. "1920x1080"
    QString ratio;      // e.g. "16x9"
};

#endif // MARKETSEARCHOPTIONS_H
