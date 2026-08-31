/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "settingspanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

SettingsPanel::SettingsPanel(QWidget *parent)
    : QFrame(parent)
{
    setupUi();
}

void SettingsPanel::setupUi() {
    setObjectName("settingsPanel");

    auto *title = new QLabel("<b style='font-size:18px; color:#f0f6fc;'>Settings</b>");
    title->setObjectName("panelTitle");

    /* Wallust */
    auto *wallustGroup = new QGroupBox("Wallust");
    wallustGroup->setObjectName("settingsGroup");

    m_wallustCheck = new QCheckBox("Generate scheme with wallust");
    m_wallustCheck->setToolTip("Run 'wallust run' after applying wallpaper");
    connect(m_wallustCheck, &QCheckBox::toggled, this, &SettingsPanel::settingsChanged);

    m_wallustHookEdit = new QLineEdit;
    m_wallustHookEdit->setPlaceholderText("Additional post-wallust hook (optional)");
    m_wallustHookEdit->setToolTip("Extra script after wallust");
    connect(m_wallustHookEdit, &QLineEdit::editingFinished, this, &SettingsPanel::settingsChanged);

    m_wallustHookBrowseButton = new QPushButton("Browse");
    m_wallustHookBrowseButton->setObjectName("secondaryButton");
    m_wallustHookBrowseButton->setToolTip("Select post-wallust script");
    connect(m_wallustHookBrowseButton, &QPushButton::clicked, this, &SettingsPanel::onWallustHookBrowse);

    auto *hookLayout = new QHBoxLayout;
    hookLayout->setSpacing(10);
    hookLayout->addWidget(new QLabel("Hook:"));
    hookLayout->addWidget(m_wallustHookEdit, 1);
    hookLayout->addWidget(m_wallustHookBrowseButton);

    auto *wallustLayout = new QVBoxLayout(wallustGroup);
    wallustLayout->setSpacing(10);
    wallustLayout->addWidget(m_wallustCheck);
    wallustLayout->addLayout(hookLayout);

    /* Cache / quality */
    auto *cacheGroup = new QGroupBox("Cache quality");
    cacheGroup->setObjectName("settingsGroup");

    auto *cacheQualityLabel = new QLabel("Downscale oversized videos/animations to:");
    cacheQualityLabel->setObjectName("mutedLabel");
    m_cacheQualityCombo = new QComboBox;
    m_cacheQualityCombo->setToolTip(
        "Original = never transcode, use full resolution.\n"
        "Monitor size = transcode only if larger than your screen.\n"
        "Low = always downscale to 1080p/720p (old behaviour).");
    m_cacheQualityCombo->addItem("Monitor size", "monitor");
    m_cacheQualityCombo->addItem("Original", "original");
    m_cacheQualityCombo->addItem("Low (save CPU/disk)", "low");
    connect(m_cacheQualityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPanel::settingsChanged);

    auto *cacheLayout = new QHBoxLayout;
    cacheLayout->setSpacing(10);
    cacheLayout->addWidget(cacheQualityLabel);
    cacheLayout->addWidget(m_cacheQualityCombo, 1);

    auto *cacheInner = new QVBoxLayout(cacheGroup);
    cacheInner->setSpacing(10);
    cacheInner->addLayout(cacheLayout);
    cacheInner->addWidget(new QLabel(
        "<span style='color:#8b949e; font-size:12px;'>"
        "Original keeps native resolution; Monitor balances quality and CPU use."
        "</span>"));

    /* mpvpaper profile */
    auto *mpvpaperGroup = new QGroupBox("Video wallpaper (mpvpaper)");
    mpvpaperGroup->setObjectName("settingsGroup");

    auto *mpvpaperProfileLabel = new QLabel("Profile:");
    mpvpaperProfileLabel->setObjectName("mutedLabel");
    m_mpvpaperProfileCombo = new QComboBox;
    m_mpvpaperProfileCombo->setToolTip(
        "Quality = smooth, best scaling.\n"
        "Balanced = lower RAM/CPU, still looks good.\n"
        "Performance = minimum resources, may micro-stutter.");
    m_mpvpaperProfileCombo->addItem("Quality", "quality");
    m_mpvpaperProfileCombo->addItem("Balanced", "balanced");
    m_mpvpaperProfileCombo->addItem("Performance", "performance");
    connect(m_mpvpaperProfileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPanel::settingsChanged);

    auto *mpvpaperProfileLayout = new QHBoxLayout;
    mpvpaperProfileLayout->setSpacing(10);
    mpvpaperProfileLayout->addWidget(mpvpaperProfileLabel);
    mpvpaperProfileLayout->addWidget(m_mpvpaperProfileCombo, 1);

    m_mpvpaperHwdecCheck = new QCheckBox("Try hardware decoding (experimental)");
    m_mpvpaperHwdecCheck->setToolTip(
        "On NVIDIA proprietary drivers this usually fails on Wayland and falls back to software. "
        "Enable only if you want to test vaapi/vdpau support.");
    connect(m_mpvpaperHwdecCheck, &QCheckBox::toggled, this, &SettingsPanel::settingsChanged);

    auto *mpvpaperInner = new QVBoxLayout(mpvpaperGroup);
    mpvpaperInner->setSpacing(10);
    mpvpaperInner->addLayout(mpvpaperProfileLayout);
    mpvpaperInner->addWidget(m_mpvpaperHwdecCheck);

    /* Daemon */
    auto *daemonGroup = new QGroupBox("Automatic wallpaper change");
    daemonGroup->setObjectName("settingsGroup");

    auto *intervalLabel = new QLabel("Interval:");
    intervalLabel->setObjectName("mutedLabel");
    m_intervalSpin = new QSpinBox;
    m_intervalSpin->setRange(10, 86400);
    m_intervalSpin->setValue(300);
    m_intervalSpin->setSuffix(" s");
    m_intervalSpin->setToolTip("Interval between automatic changes");

    m_daemonButton = new QPushButton("Start daemon");
    m_daemonButton->setCheckable(true);
    m_daemonButton->setToolTip("Start/stop automatic wallpaper changes");
    connect(m_daemonButton, &QPushButton::toggled, this, &SettingsPanel::onDaemonToggled);

    auto *daemonLayout = new QHBoxLayout;
    daemonLayout->setSpacing(10);
    daemonLayout->addWidget(intervalLabel);
    daemonLayout->addWidget(m_intervalSpin);
    daemonLayout->addStretch();
    daemonLayout->addWidget(m_daemonButton);

    auto *daemonInner = new QVBoxLayout(daemonGroup);
    daemonInner->setSpacing(10);
    daemonInner->addLayout(daemonLayout);
    daemonInner->addWidget(new QLabel("<span style='color:#8b949e; font-size:12px;'>Daemon uses the currently selected backend and scans the active folder.</span>"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(16);
    layout->addWidget(title);
    layout->addWidget(wallustGroup);
    layout->addWidget(cacheGroup);
    layout->addWidget(mpvpaperGroup);
    layout->addWidget(daemonGroup);
    layout->addStretch(1);
}

void SettingsPanel::setWallustEnabled(bool enabled) {
    m_wallustCheck->setChecked(enabled);
}

bool SettingsPanel::wallustEnabled() const {
    return m_wallustCheck->isChecked();
}

void SettingsPanel::setWallustHook(const QString &hook) {
    m_wallustHookEdit->setText(hook);
}

QString SettingsPanel::wallustHook() const {
    return m_wallustHookEdit->text();
}

void SettingsPanel::setCacheQuality(const QString &quality) {
    int idx = m_cacheQualityCombo->findData(quality, Qt::UserRole, Qt::MatchFixedString);
    if (idx < 0)
        idx = m_cacheQualityCombo->findData("monitor");
    m_cacheQualityCombo->setCurrentIndex(idx >= 0 ? idx : 0);
}

QString SettingsPanel::cacheQuality() const {
    return m_cacheQualityCombo->currentData().toString();
}

void SettingsPanel::setMpvpaperProfile(const QString &profile) {
    int idx = m_mpvpaperProfileCombo->findData(profile, Qt::UserRole, Qt::MatchFixedString);
    if (idx < 0)
        idx = m_mpvpaperProfileCombo->findData("quality");
    m_mpvpaperProfileCombo->setCurrentIndex(idx >= 0 ? idx : 0);
}

QString SettingsPanel::mpvpaperProfile() const {
    return m_mpvpaperProfileCombo->currentData().toString();
}

void SettingsPanel::setMpvpaperHwdec(bool enabled) {
    m_mpvpaperHwdecCheck->setChecked(enabled);
}

bool SettingsPanel::mpvpaperHwdec() const {
    return m_mpvpaperHwdecCheck->isChecked();
}

void SettingsPanel::setInterval(int seconds) {
    m_intervalSpin->setValue(seconds);
}

int SettingsPanel::interval() const {
    return m_intervalSpin->value();
}

void SettingsPanel::setDaemonRunning(bool running) {
    m_daemonButton->setChecked(running);
    m_daemonButton->setText(running ? "Stop daemon" : "Start daemon");
}

void SettingsPanel::onWallustHookBrowse() {
    QString file = QFileDialog::getOpenFileName(this,
                                                "Select wallust hook",
                                                QDir::homePath(),
                                                "Scripts (*.sh);;All files (*)");
    if (!file.isEmpty()) {
        m_wallustHookEdit->setText(file);
        emit settingsChanged();
    }
}

void SettingsPanel::onDaemonToggled(bool checked) {
    m_daemonButton->setText(checked ? "Stop daemon" : "Start daemon");
    emit daemonRequested(checked);
}
