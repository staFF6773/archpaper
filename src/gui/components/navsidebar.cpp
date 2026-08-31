/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "navsidebar.h"

#include <QButtonGroup>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

constexpr int SIDEBAR_WIDTH = 200;

QFrame *createSeparator(QWidget *parent) {
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setObjectName("sidebarSeparator");
    line->setFixedHeight(1);
    return line;
}

} // namespace

NavSidebar::NavSidebar(QWidget *parent)
    : QFrame(parent)
{
    setObjectName("navSidebar");
    setFixedWidth(SIDEBAR_WIDTH);

    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(10, 14, 10, 14);
    m_layout->setSpacing(4);

    /* App title */
    auto *title = new QLabel("<b style='font-size:15px; color:#f0f6fc;'>archpaper</b>");
    title->setObjectName("sidebarTitle");
    m_layout->addWidget(title);
    m_layout->addSpacing(12);

    /* Main sections */
    m_homeBtn = createNavButton("\u2302", "Home", Home);
    m_favBtn = createNavButton("\u2605", "Favorites", Favorites);
    m_recentBtn = createNavButton("\u21bb", "Recent", Recent);

    m_layout->addWidget(m_homeBtn);
    m_layout->addWidget(m_favBtn);
    m_layout->addWidget(m_recentBtn);

    m_layout->addSpacing(8);
    m_layout->addWidget(createSeparator(this));
    m_layout->addSpacing(8);

    /* Folders section header */
    auto *folderHeader = new QHBoxLayout;
    folderHeader->setSpacing(6);
    auto *folderTitle = new QLabel("<span style='color:#8b949e; font-size:11px; font-weight:700;'>FOLDERS</span>");
    folderTitle->setObjectName("sidebarSectionTitle");
    folderHeader->addWidget(folderTitle, 1);

    m_addFolderBtn = new QPushButton("+");
    m_addFolderBtn->setObjectName("sidebarSmallButton");
    m_addFolderBtn->setToolTip("Add a wallpaper folder");
    m_addFolderBtn->setFixedSize(22, 22);
    connect(m_addFolderBtn, &QPushButton::clicked, this, &NavSidebar::onAddFolder);

    m_removeFolderBtn = new QPushButton("-");
    m_removeFolderBtn->setObjectName("sidebarSmallButton");
    m_removeFolderBtn->setToolTip("Remove selected folder");
    m_removeFolderBtn->setFixedSize(22, 22);
    connect(m_removeFolderBtn, &QPushButton::clicked, this, &NavSidebar::onRemoveFolder);

    folderHeader->addWidget(m_addFolderBtn);
    folderHeader->addWidget(m_removeFolderBtn);
    m_layout->addLayout(folderHeader);

    m_folderList = new QListWidget(this);
    m_folderList->setObjectName("folderList");
    m_folderList->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_folderList, &QListWidget::itemSelectionChanged,
            this, &NavSidebar::onFolderSelectionChanged);
    m_layout->addWidget(m_folderList, 1);

    m_layout->addSpacing(8);
    m_layout->addWidget(createSeparator(this));
    m_layout->addSpacing(8);

    /* Settings */
    m_settingsBtn = createNavButton("\u2699", "Settings", Settings);
    m_layout->addWidget(m_settingsBtn);

    m_layout->addStretch();

    m_homeBtn->setChecked(true);
    m_currentSection = Home;
}

QPushButton *NavSidebar::createNavButton(const QString &icon, const QString &text, Section section) {
    auto *btn = new QPushButton(QString("%1   %2").arg(icon, text));
    btn->setCheckable(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setProperty("navButton", true);
    btn->setProperty("section", section);
    m_group->addButton(btn);
    connect(btn, &QPushButton::toggled, this, &NavSidebar::onSectionToggled);
    return btn;
}

NavSidebar::Section NavSidebar::currentSection() const {
    return m_currentSection;
}

void NavSidebar::setSection(NavSidebar::Section section) {
    m_currentSection = section;
    switch (section) {
        case Home: m_homeBtn->setChecked(true); break;
        case Favorites: m_favBtn->setChecked(true); break;
        case Recent: m_recentBtn->setChecked(true); break;
        case Settings: m_settingsBtn->setChecked(true); break;
    }
}

void NavSidebar::setFolders(const QStringList &folders) {
    m_folderList->clear();
    m_folderList->addItems(folders);
    if (m_folderList->count() > 0) {
        m_folderList->setCurrentRow(0);
    }
}

void NavSidebar::addFolder(const QString &folder) {
    for (int i = 0; i < m_folderList->count(); ++i) {
        QString text = m_folderList->item(i)->text();
        if (text == folder || text == QDir(folder).dirName()) {
            m_folderList->setCurrentRow(i);
            return;
        }
    }
    m_folderList->addItem(folder);
    m_folderList->setCurrentRow(m_folderList->count() - 1);
    onFolderSelectionChanged();
    emit folderAdded(folder);
}

void NavSidebar::removeFolder(int row) {
    if (row < 0 || row >= m_folderList->count()) return;
    delete m_folderList->takeItem(row);
    emit folderRemoved(row);
}

QString NavSidebar::selectedFolder() const {
    auto *item = m_folderList->currentItem();
    return item ? item->text() : QString();
}

QString NavSidebar::folderAt(int row) const {
    if (row < 0 || row >= m_folderList->count()) return QString();
    return m_folderList->item(row)->text();
}

int NavSidebar::folderCount() const {
    return m_folderList->count();
}

void NavSidebar::onAddFolder() {
    QString startDir = QDir::homePath() + "/Pictures/Wallpapers";
    if (!QDir(startDir).exists()) {
        QDir().mkpath(startDir);
    }
    QString dir = QFileDialog::getExistingDirectory(this, "Add wallpaper folder", startDir);
    if (dir.isEmpty()) return;
    addFolder(dir);
}

void NavSidebar::onRemoveFolder() {
    int row = m_folderList->currentRow();
    if (row < 0) return;
    delete m_folderList->takeItem(row);
    emit folderRemoved(row);
}

void NavSidebar::onFolderSelectionChanged() {
    auto *item = m_folderList->currentItem();
    if (!item) return;

    setSection(Home);
    emit folderSelected(item->text());
}

void NavSidebar::onSectionToggled() {
    auto *btn = qobject_cast<QPushButton *>(m_group->checkedButton());
    if (!btn) return;

    Section section = btn->property("section").value<Section>();
    m_currentSection = section;
    emit sectionChanged(section);
}

void NavSidebar::rebuildFolderSection() {
    /* Reserved for dynamic folder grouping if needed in the future. */
}
