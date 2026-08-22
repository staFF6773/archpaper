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
#include <QHBoxLayout>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

constexpr int SIDEBAR_WIDTH = 64;

QToolButton *createSectionButton(const QString &text, const QString &tooltip, QButtonGroup *group) {
    auto *btn = new QToolButton;
    btn->setText(text);
    btn->setToolTip(tooltip);
    btn->setCheckable(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setProperty("navButton", true);
    group->addButton(btn);
    return btn;
}

QToolButton *createToggleButton(const QString &text, const QString &tooltip) {
    auto *btn = new QToolButton;
    btn->setText(text);
    btn->setToolTip(tooltip);
    btn->setCheckable(true);
    btn->setCursor(Qt::PointingHandCursor);
    btn->setProperty("navButton", true);
    return btn;
}

}

NavSidebar::NavSidebar(QWidget *parent)
    : QFrame(parent)
{
    setObjectName("navSidebar");
    setFixedWidth(SIDEBAR_WIDTH);

    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);

    m_homeBtn = createSectionButton("\u2302", "Home", m_group);
    m_favBtn = createSectionButton("\u2605", "Favorites", m_group);
    m_recentBtn = createSectionButton("\u21bb", "Recent", m_group);
    m_marketBtn = createSectionButton("\u263a", "Market", m_group);
    m_settingsBtn = createToggleButton("\u2699", "Settings");

    m_homeBtn->setProperty("section", Home);
    m_favBtn->setProperty("section", Favorites);
    m_recentBtn->setProperty("section", Recent);
    m_marketBtn->setProperty("section", Market);
    m_settingsBtn->setProperty("section", Settings);

    m_homeBtn->setChecked(true);

    connect(m_group, &QButtonGroup::idToggled, this, [this](int id, bool checked) {
        Q_UNUSED(id)
        if (checked) {
            onSectionButtonToggled();
        }
    });

    connect(m_settingsBtn, &QToolButton::toggled, this, [this](bool checked) {
        if (checked) {
            m_currentSection = Settings;
            emit sectionChanged(m_currentSection);
            emit settingsToggled(true);
        } else {
            if (!m_group->checkedButton()) {
                m_homeBtn->setChecked(true);
                m_currentSection = Home;
                emit sectionChanged(m_currentSection);
            }
            emit settingsToggled(false);
        }
    });

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 16, 10, 16);
    layout->setSpacing(12);
    layout->addWidget(m_homeBtn);
    layout->addWidget(m_favBtn);
    layout->addWidget(m_recentBtn);
    layout->addWidget(m_marketBtn);
    layout->addStretch();
    layout->addWidget(m_settingsBtn);
}

NavSidebar::Section NavSidebar::currentSection() const {
    return m_currentSection;
}

bool NavSidebar::settingsActive() const {
    return m_settingsBtn->isChecked();
}

void NavSidebar::setSection(NavSidebar::Section section) {
    m_settingsBtn->setChecked(false);
    switch (section) {
        case Home: m_homeBtn->setChecked(true); break;
        case Favorites: m_favBtn->setChecked(true); break;
        case Recent: m_recentBtn->setChecked(true); break;
        case Market: m_marketBtn->setChecked(true); break;
        case Settings:
            m_settingsBtn->setChecked(true);
            break;
    }
}

void NavSidebar::onSectionButtonToggled() {
    auto *btn = qobject_cast<QToolButton *>(m_group->checkedButton());
    if (!btn) return;

    m_settingsBtn->setChecked(false);
    Section section = btn->property("section").value<Section>();
    m_currentSection = section;
    emit sectionChanged(section);
    emit settingsToggled(false);
}
