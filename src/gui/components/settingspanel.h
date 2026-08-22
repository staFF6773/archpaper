/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef SETTINGSPANEL_H
#define SETTINGSPANEL_H

#include <QFrame>

QT_BEGIN_NAMESPACE
class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QSpinBox;
QT_END_NAMESPACE

#include "../market/marketitem.h"

class SettingsPanel : public QFrame {
    Q_OBJECT

public:
    explicit SettingsPanel(QWidget *parent = nullptr);

    void setBackend(int index);
    int backend() const;

    void setMode(const QString &mode);
    QString mode() const;

    void setWallustEnabled(bool enabled);
    bool wallustEnabled() const;

    void setWallustHook(const QString &hook);
    QString wallustHook() const;

    void setInterval(int seconds);
    int interval() const;

    void setDaemonRunning(bool running);

    /* Market settings */
    void setMarketDownloadDir(const QString &dir);
    QString marketDownloadDir() const;

    void setWallhavenApiKey(const QString &key);
    QString wallhavenApiKey() const;

    void setWallhavenPurity(const QString &purity);
    QString wallhavenPurity() const;

signals:
    void backendChanged(int index);
    void modeChanged(int index);
    void settingsChanged();
    void daemonRequested(bool start);
    void marketSettingsChanged();

private slots:
    void onWallustHookBrowse();
    void onDaemonToggled(bool checked);
    void onMarketDownloadBrowse();

private:
    void setupUi();

    QComboBox *m_backendCombo;
    QComboBox *m_modeCombo;
    QCheckBox *m_wallustCheck;
    QLineEdit *m_wallustHookEdit;
    QPushButton *m_wallustHookBrowseButton;
    QSpinBox *m_intervalSpin;
    QPushButton *m_daemonButton;

    /* Market */
    QLineEdit *m_marketDownloadDirEdit;
    QPushButton *m_marketDownloadBrowseButton;
    QLineEdit *m_wallhavenApiKeyEdit;
    QComboBox *m_wallhavenPurityCombo;
};

#endif // SETTINGSPANEL_H
