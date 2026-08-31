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

signals:
    void backendChanged(int index);
    void modeChanged(int index);
    void settingsChanged();
    void daemonRequested(bool start);

private slots:
    void onWallustHookBrowse();
    void onDaemonToggled(bool checked);

private:
    void setupUi();

    QComboBox *m_backendCombo;
    QComboBox *m_modeCombo;
    QCheckBox *m_wallustCheck;
    QLineEdit *m_wallustHookEdit;
    QPushButton *m_wallustHookBrowseButton;
    QSpinBox *m_intervalSpin;
    QPushButton *m_daemonButton;
};

#endif // SETTINGSPANEL_H
