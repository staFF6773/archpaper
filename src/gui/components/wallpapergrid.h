/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef WALLPAPERGRID_H
#define WALLPAPERGRID_H

#include <QFrame>
#include <QPixmap>
#include <QSize>

QT_BEGIN_NAMESPACE
class QListWidget;
class QListWidgetItem;
class QLineEdit;
QT_END_NAMESPACE

class WallpaperGrid : public QFrame {
    Q_OBJECT

public:
    explicit WallpaperGrid(QWidget *parent = nullptr);

    void clear();
    void addWallpaper(const QString &path);
    void loadFromFolder(const QString &folder);
    void setWallpapers(const QStringList &paths);

    QString selectedPath() const;
    QString pathAt(int row) const;
    int count() const;
    int visibleCount() const;

    QStringList currentPaths() const;

public slots:
    void selectRandom();
    void setFilter(const QString &text);
    void refreshFilter();

signals:
    void imageSelected(const QString &path);
    void imageDoubleClicked(const QString &path);
    void countChanged(int visible, int total);

private slots:
    void onSelectionChanged();
    void onItemDoubleClicked(QListWidgetItem *item);
    void onFilterTextChanged(const QString &text);

private:
    void setupUi();
    static QPixmap createThumbnail(const QString &path, const QSize &targetSize);

    QListWidget *m_list;
    QLineEdit *m_filter;
};

#endif // WALLPAPERGRID_H
