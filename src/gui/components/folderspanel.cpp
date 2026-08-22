/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "folderspanel.h"

#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

FoldersPanel::FoldersPanel(QWidget *parent)
    : QFrame(parent)
{
    setObjectName("foldersPanel");
    setMinimumWidth(180);
    setMaximumWidth(260);

    auto *header = new QLabel("<b>Folders</b>");
    header->setObjectName("panelHeader");

    m_list = new QListWidget(this);
    m_list->setObjectName("foldersList");
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    connect(m_list, &QListWidget::itemSelectionChanged, this, &FoldersPanel::onFolderSelectionChanged);

    auto *addBtn = new QPushButton("+");
    addBtn->setObjectName("secondaryButton");
    addBtn->setToolTip("Add a wallpaper folder");
    addBtn->setFixedWidth(36);
    connect(addBtn, &QPushButton::clicked, this, &FoldersPanel::onAddFolder);

    auto *removeBtn = new QPushButton("-");
    removeBtn->setObjectName("secondaryButton");
    removeBtn->setToolTip("Remove selected folder");
    removeBtn->setFixedWidth(36);
    connect(removeBtn, &QPushButton::clicked, this, &FoldersPanel::onRemoveFolder);

    auto *btnLayout = new QHBoxLayout;
    btnLayout->setSpacing(8);
    btnLayout->addWidget(addBtn);
    btnLayout->addWidget(removeBtn);
    btnLayout->addStretch();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    layout->addWidget(header);
    layout->addWidget(m_list);
    layout->addLayout(btnLayout);
}

void FoldersPanel::setFolders(const QStringList &folders) {
    m_list->clear();
    m_list->addItems(folders);
    if (m_list->count() > 0) {
        m_list->setCurrentRow(0);
    }
}

void FoldersPanel::addFolder(const QString &folder) {
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->text() == folder) {
            m_list->setCurrentRow(i);
            return;
        }
    }
    m_list->addItem(folder);
    m_list->setCurrentRow(m_list->count() - 1);
    emit folderAdded(folder);
}

QString FoldersPanel::folderAt(int row) const {
    if (row < 0 || row >= m_list->count()) return QString();
    return m_list->item(row)->text();
}

void FoldersPanel::removeFolder(int row) {
    if (row < 0 || row >= m_list->count()) return;
    delete m_list->takeItem(row);
    emit folderRemoved(row);
}

QString FoldersPanel::selectedFolder() const {
    auto *item = m_list->currentItem();
    return item ? item->text() : QString();
}

int FoldersPanel::count() const {
    return m_list->count();
}

void FoldersPanel::onAddFolder() {
    QString startDir = QDir::homePath() + "/Pictures/Wallpapers";
    if (!QDir(startDir).exists()) {
        QDir().mkpath(startDir);
    }
    QString dir = QFileDialog::getExistingDirectory(this, "Add wallpaper folder", startDir);
    if (dir.isEmpty()) return;
    addFolder(dir);
}

void FoldersPanel::onRemoveFolder() {
    int row = m_list->currentRow();
    if (row < 0) return;
    delete m_list->takeItem(row);
    emit folderRemoved(row);
}

void FoldersPanel::onFolderSelectionChanged() {
    auto *item = m_list->currentItem();
    if (item) {
        emit folderSelected(item->text());
    }
}
