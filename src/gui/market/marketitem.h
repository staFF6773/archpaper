/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef MARKETITEM_H
#define MARKETITEM_H

#include <QMetaType>
#include <QString>

struct MarketItem {
    QString id;
    QString title;
    QString source;        // "wallhaven" or "moewalls"
    QString thumbnailUrl;  // preview image
    QString fullUrl;       // direct file URL (may be resolved lazily)
    QString pageUrl;       // human-readable page
    QString resolution;
    QString fileType;      // jpeg / png / mp4 / webm
};

Q_DECLARE_METATYPE(MarketItem)

#endif // MARKETITEM_H
