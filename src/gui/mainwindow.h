/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "components/navsidebar.h"

extern "C" {
#include "archpaper/backend.h"
}

QT_BEGIN_NAMESPACE
class QLabel;
class QComboBox;
class QStackedWidget;
class QSplitter;
QT_END_NAMESPACE

class FoldersPanel;
class WallpaperGrid;
class PreviewPanel;
class SettingsPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    /* Navigation */
    void onSectionChanged(NavSidebar::Section section);
    void onSettingsToggled(bool active);

    /* Gallery */
    void onFolderSelected(const QString &folder);
    void onFolderAdded(const QString &folder);
    void onFolderRemoved(int row);
    void onImageSelected(const QString &path);
    void onImageDoubleClicked(const QString &path);
    void onGridCountChanged(int visible, int total);

    /* Preview actions */
    void onToggleFavorite();
    void onApply();
    void onRandom();
    void onClear();

    /* Settings */
    void onBackendChanged(int index);
    void onModeChanged(int index);
    void onSettingsChanged();
    void onDaemonRequested(bool start);

private:
    enum ContentPage { GalleryPage, SettingsPage };

    void setupUi();
    void applyStyleSheet();

    void loadConfig();
    void saveCurrentConfig(const char *path);

    void loadFolders();
    void saveFolders();
    void loadFavorites();
    void saveFavorites();
    void loadRecent();
    void saveRecent();
    void addToRecent(const QString &path);

    bool isFavorite(const QString &path) const;
    void refreshFavoriteButton();

    void setGallerySection(NavSidebar::Section section);
    void applySelectedImage(const QString &path);
    void updateStatus(const QString &msg);
    void updateSectionTitle(NavSidebar::Section section);

    backend_t selectedBackend() const;
    backend_t preferredBackendFor(const QString &path) const;
    bool readDaemonPid(int *pid);

    /* UI composition */
    NavSidebar *m_sidebar;
    QLabel *m_sectionTitle;
    QComboBox *m_backendCombo;
    QComboBox *m_modeCombo;
    QStackedWidget *m_pages;
    QLabel *m_statusLabel;

    QWidget *m_galleryPage;
    QSplitter *m_gallerySplitter;
    FoldersPanel *m_foldersPanel;
    WallpaperGrid *m_grid;
    PreviewPanel *m_preview;

    SettingsPanel *m_settingsPanel;

    /* State */
    NavSidebar::Section m_currentSection = NavSidebar::Home;
    QString m_currentFolder;
    bool m_daemonRunning = false;

    QStringList m_favoritePaths;
    QStringList m_recentPaths;
};

#endif // MAINWINDOW_H
