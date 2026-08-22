/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef MARKETGRID_H
#define MARKETGRID_H

#include "../market/marketitem.h"

#include <QFrame>
#include <QList>
#include <QPixmap>

QT_BEGIN_NAMESPACE
class QListWidget;
class QListWidgetItem;
QT_END_NAMESPACE

class MarketGrid : public QFrame {
    Q_OBJECT

public:
    explicit MarketGrid(QWidget *parent = nullptr);

    void clear();
    void setItems(const QList<MarketItem> &items);
    void appendItems(const QList<MarketItem> &items);

    MarketItem selectedItem() const;
    QList<MarketItem> items() const;

    void setThumbnail(const QString &url, const QPixmap &pixmap);

    int count() const;

signals:
    void itemSelected(const MarketItem &item);
    void itemDoubleClicked(const MarketItem &item);
    void thumbnailNeeded(const QString &url);
    void countChanged(int count);

private slots:
    void onSelectionChanged();
    void onItemDoubleClicked(QListWidgetItem *item);

private:
    void setupUi();
    void addItem(const MarketItem &item, const QPixmap &thumb);
    static QPixmap roundedThumb(const QPixmap &source, const QSize &targetSize);

    QListWidget *m_list;
    QHash<QString, QList<int>> m_urlToRows;
};

#endif // MARKETGRID_H
