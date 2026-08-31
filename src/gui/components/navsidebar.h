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
class QToolButton;
QT_END_NAMESPACE

class NavSidebar : public QFrame {
    Q_OBJECT

public:
    enum Section { Home, Favorites, Recent, Settings };
    Q_ENUM(Section)

    explicit NavSidebar(QWidget *parent = nullptr);

    Section currentSection() const;
    bool settingsActive() const;

public slots:
    void setSection(Section section);

signals:
    void sectionChanged(NavSidebar::Section section);
    void settingsToggled(bool active);

private slots:
    void onSectionButtonToggled();

private:
    QButtonGroup *m_group;
    QToolButton *m_homeBtn;
    QToolButton *m_favBtn;
    QToolButton *m_recentBtn;
    QToolButton *m_settingsBtn;
    Section m_currentSection = Home;
};

#endif // NAVSIDEBAR_H
