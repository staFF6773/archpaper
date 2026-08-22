/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef FOLDERSPANEL_H
#define FOLDERSPANEL_H

#include <QFrame>

QT_BEGIN_NAMESPACE
class QListWidget;
QT_END_NAMESPACE

class FoldersPanel : public QFrame {
    Q_OBJECT

public:
    explicit FoldersPanel(QWidget *parent = nullptr);

    void setFolders(const QStringList &folders);
    void addFolder(const QString &folder);
    void removeFolder(int row);
    QString selectedFolder() const;
    QString folderAt(int row) const;
    int count() const;

signals:
    void folderSelected(const QString &folder);
    void folderAdded(const QString &folder);
    void folderRemoved(int row);

private slots:
    void onAddFolder();
    void onRemoveFolder();
    void onFolderSelectionChanged();

private:
    QListWidget *m_list;
};

#endif // FOLDERSPANEL_H
