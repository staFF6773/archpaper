/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef NAVSIDEBAR_H
#define NAVSIDEBAR_H

#include <QFrame>

QT_BEGIN_NAMESPACE
class QButtonGroup;
class QListWidget;
class QPushButton;
class QVBoxLayout;
QT_END_NAMESPACE

class NavSidebar : public QFrame {
    Q_OBJECT

public:
    enum Section { Home, Favorites, Recent, Settings };
    Q_ENUM(Section)

    explicit NavSidebar(QWidget *parent = nullptr);

    Section currentSection() const;

    void setFolders(const QStringList &folders);
    void addFolder(const QString &folder);
    void removeFolder(int row);
    QString selectedFolder() const;
    QString folderAt(int row) const;
    int folderCount() const;

    void setSection(Section section);

signals:
    void sectionChanged(NavSidebar::Section section);

    void folderSelected(const QString &folder);
    void folderAdded(const QString &folder);
    void folderRemoved(int row);

private slots:
    void onAddFolder();
    void onRemoveFolder();
    void onFolderSelectionChanged();
    void onSectionToggled();

private:
    QPushButton *createNavButton(const QString &icon, const QString &text, Section section);
    void rebuildFolderSection();

    QButtonGroup *m_group;
    QVBoxLayout *m_layout;

    QPushButton *m_homeBtn;
    QPushButton *m_favBtn;
    QPushButton *m_recentBtn;
    QPushButton *m_settingsBtn;

    QListWidget *m_folderList;
    QPushButton *m_addFolderBtn;
    QPushButton *m_removeFolderBtn;

    Section m_currentSection = Home;
};

#endif // NAVSIDEBAR_H
