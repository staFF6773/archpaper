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

    /* Backend & mode */
    auto *backendModeGroup = new QGroupBox("Backend & Mode");
    backendModeGroup->setObjectName("settingsGroup");

    auto *backendLabel = new QLabel("Backend:");
    backendLabel->setObjectName("mutedLabel");
    m_backendCombo = new QComboBox;
    m_backendCombo->addItem("swaybg");
    m_backendCombo->addItem("hyprpaper");
    m_backendCombo->addItem("mpvpaper");
    m_backendCombo->addItem("awww");
    m_backendCombo->setToolTip("Backend used to apply wallpapers");
    connect(m_backendCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPanel::backendChanged);

    auto *modeLabel = new QLabel("Mode:");
    modeLabel->setObjectName("mutedLabel");
    m_modeCombo = new QComboBox;
    m_modeCombo->addItems({"fill", "fit", "stretch", "center", "tile"});
    m_modeCombo->setToolTip("Image scaling mode");
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPanel::modeChanged);

    auto *backendModeLayout = new QGridLayout(backendModeGroup);
    backendModeLayout->setSpacing(10);
    backendModeLayout->addWidget(backendLabel, 0, 0);
    backendModeLayout->addWidget(m_backendCombo, 0, 1);
    backendModeLayout->addWidget(modeLabel, 1, 0);
    backendModeLayout->addWidget(m_modeCombo, 1, 1);
    backendModeLayout->setColumnStretch(2, 1);

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

    /* Market */
    auto *marketGroup = new QGroupBox("Market");
    marketGroup->setObjectName("settingsGroup");

    auto *marketDirLabel = new QLabel("Download folder:");
    marketDirLabel->setObjectName("mutedLabel");
    m_marketDownloadDirEdit = new QLineEdit;
    m_marketDownloadDirEdit->setPlaceholderText("~/Pictures/Wallpapers");
    connect(m_marketDownloadDirEdit, &QLineEdit::editingFinished,
            this, &SettingsPanel::marketSettingsChanged);

    m_marketDownloadBrowseButton = new QPushButton("Browse");
    m_marketDownloadBrowseButton->setObjectName("secondaryButton");
    connect(m_marketDownloadBrowseButton, &QPushButton::clicked,
            this, &SettingsPanel::onMarketDownloadBrowse);

    auto *marketDirLayout = new QHBoxLayout;
    marketDirLayout->setSpacing(10);
    marketDirLayout->addWidget(m_marketDownloadDirEdit, 1);
    marketDirLayout->addWidget(m_marketDownloadBrowseButton);

    auto *apiKeyLabel = new QLabel("Wallhaven API key:");
    apiKeyLabel->setObjectName("mutedLabel");
    m_wallhavenApiKeyEdit = new QLineEdit;
    m_wallhavenApiKeyEdit->setPlaceholderText("Optional (required for NSFW)");
    m_wallhavenApiKeyEdit->setEchoMode(QLineEdit::Password);
    connect(m_wallhavenApiKeyEdit, &QLineEdit::editingFinished,
            this, &SettingsPanel::marketSettingsChanged);

    auto *purityLabel = new QLabel("Wallhaven purity:");
    purityLabel->setObjectName("mutedLabel");
    m_wallhavenPurityCombo = new QComboBox;
    m_wallhavenPurityCombo->addItem("SFW", "sfw");
    m_wallhavenPurityCombo->addItem("Sketchy", "sketchy");
    m_wallhavenPurityCombo->addItem("NSFW", "nsfw");
    m_wallhavenPurityCombo->addItem("All", "all");
    connect(m_wallhavenPurityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsPanel::marketSettingsChanged);

    auto *marketLayout = new QGridLayout(marketGroup);
    marketLayout->setSpacing(10);
    marketLayout->addWidget(marketDirLabel, 0, 0);
    marketLayout->addLayout(marketDirLayout, 0, 1);
    marketLayout->addWidget(apiKeyLabel, 1, 0);
    marketLayout->addWidget(m_wallhavenApiKeyEdit, 1, 1);
    marketLayout->addWidget(purityLabel, 2, 0);
    marketLayout->addWidget(m_wallhavenPurityCombo, 2, 1);
    marketLayout->setColumnStretch(1, 1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(16);
    layout->addWidget(title);
    layout->addWidget(backendModeGroup);
    layout->addWidget(wallustGroup);
    layout->addWidget(daemonGroup);
    layout->addWidget(marketGroup);
    layout->addStretch(1);
}

void SettingsPanel::setBackend(int index) {
    m_backendCombo->setCurrentIndex(index);
}

int SettingsPanel::backend() const {
    return m_backendCombo->currentIndex();
}

void SettingsPanel::setMode(const QString &mode) {
    int idx = m_modeCombo->findText(mode, Qt::MatchFixedString);
    if (idx < 0) idx = 0;
    m_modeCombo->setCurrentIndex(idx);
}

QString SettingsPanel::mode() const {
    return m_modeCombo->currentText();
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

void SettingsPanel::setMarketDownloadDir(const QString &dir) {
    m_marketDownloadDirEdit->setText(dir);
}

QString SettingsPanel::marketDownloadDir() const {
    return m_marketDownloadDirEdit->text();
}

void SettingsPanel::setWallhavenApiKey(const QString &key) {
    m_wallhavenApiKeyEdit->setText(key);
}

QString SettingsPanel::wallhavenApiKey() const {
    return m_wallhavenApiKeyEdit->text();
}

void SettingsPanel::setWallhavenPurity(const QString &purity) {
    int idx = m_wallhavenPurityCombo->findData(purity);
    if (idx < 0) idx = 0;
    m_wallhavenPurityCombo->setCurrentIndex(idx);
}

QString SettingsPanel::wallhavenPurity() const {
    return m_wallhavenPurityCombo->currentData().toString();
}

void SettingsPanel::onMarketDownloadBrowse() {
    QString dir = QFileDialog::getExistingDirectory(this,
                                                    QStringLiteral("Select download folder"),
                                                    QDir::homePath());
    if (!dir.isEmpty()) {
        m_marketDownloadDirEdit->setText(dir);
        emit marketSettingsChanged();
    }
}
