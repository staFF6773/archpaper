/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "mainwindow.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTextStream>
#include <QVBoxLayout>

#include "components/folderspanel.h"
#include "components/marketpanel.h"
#include "components/navsidebar.h"
#include "components/previewpanel.h"
#include "components/settingspanel.h"
#include "components/wallpapergrid.h"

extern "C" {
#include "archpaper/backend.h"
#include "archpaper/config.h"
#include "archpaper/daemon.h"
#include "archpaper/utils.h"
#include "archpaper/wallust.h"
}

#include <cstdlib>
#include <ctime>
#include <signal.h>
#include <unistd.h>

namespace {

QString configDir() {
    QString path = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    path += "/archpaper";
    return path;
}

QString favoritesFile() {
    return configDir() + "/favorites";
}

QString recentFile() {
    return configDir() + "/recent";
}

QString defaultWallpaperDir() {
    return QDir::homePath() + "/Pictures/Wallpapers";
}

QString ext(const QString &path) {
    return QFileInfo(path).suffix().toLower();
}

bool isAnimatedImage(const QString &path) {
    QString e = ext(path);
    return e == "gif" || e == "webp";
}

bool isVideo(const QString &path) {
    QString e = ext(path);
    return e == "mp4" || e == "webm" || e == "mkv" || e == "mov" || e == "avi" || e == "ogv";
}

bool isAnimatedFile(const QString &path) {
    return isAnimatedImage(path) || isVideo(path);
}

}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUi();
    loadConfig();
}

MainWindow::~MainWindow() = default;

void MainWindow::applyStyleSheet() {
    QFile styleFile(":/theme/style.qss");
    if (styleFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qApp->setStyleSheet(QString::fromUtf8(styleFile.readAll()));
    } else {
        qApp->setStyleSheet(QString());
    }
}

void MainWindow::setupUi() {
    setWindowTitle("archpaper");
    resize(1600, 900);
    setMinimumSize(1280, 720);

    applyStyleSheet();

    auto *central = new QWidget;
    setCentralWidget(central);

    auto *rootLayout = new QHBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    /* Sidebar */
    m_sidebar = new NavSidebar(this);
    connect(m_sidebar, &NavSidebar::sectionChanged, this, &MainWindow::onSectionChanged);
    connect(m_sidebar, &NavSidebar::settingsToggled, this, &MainWindow::onSettingsToggled);

    /* Right column */
    auto *rightColumn = new QWidget;
    auto *rightLayout = new QVBoxLayout(rightColumn);
    rightLayout->setContentsMargins(12, 12, 12, 12);
    rightLayout->setSpacing(12);

    /* Top bar */
    auto *topBar = new QFrame;
    topBar->setObjectName("topBar");
    auto *topBarLayout = new QHBoxLayout(topBar);
    topBarLayout->setContentsMargins(14, 10, 14, 10);
    topBarLayout->setSpacing(12);

    m_sectionTitle = new QLabel("<b style='font-size:18px; color:#f0f6fc;'>Home</b>");

    auto *backendLabel = new QLabel("Backend:");
    backendLabel->setObjectName("mutedLabel");
    m_backendCombo = new QComboBox;
    m_backendCombo->addItem("swaybg");
    m_backendCombo->addItem("hyprpaper");
    m_backendCombo->addItem("mpvpaper");
    m_backendCombo->addItem("swww");
    m_backendCombo->setToolTip("Backend used to apply the wallpaper");
    connect(m_backendCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onBackendChanged);

    auto *modeLabel = new QLabel("Mode:");
    modeLabel->setObjectName("mutedLabel");
    m_modeCombo = new QComboBox;
    m_modeCombo->addItems({"fill", "fit", "stretch", "center", "tile"});
    m_modeCombo->setToolTip("Image scaling mode");
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onModeChanged);

    topBarLayout->addWidget(m_sectionTitle);
    topBarLayout->addSpacing(20);
    topBarLayout->addStretch();
    topBarLayout->addWidget(backendLabel);
    topBarLayout->addWidget(m_backendCombo);
    topBarLayout->addSpacing(12);
    topBarLayout->addWidget(modeLabel);
    topBarLayout->addWidget(m_modeCombo);

    /* Pages */
    m_pages = new QStackedWidget(this);

    /* Gallery page */
    m_galleryPage = new QWidget;
    auto *galleryLayout = new QHBoxLayout(m_galleryPage);
    galleryLayout->setContentsMargins(0, 0, 0, 0);
    galleryLayout->setSpacing(0);

    m_gallerySplitter = new QSplitter(Qt::Horizontal);

    m_foldersPanel = new FoldersPanel;
    connect(m_foldersPanel, &FoldersPanel::folderSelected, this, &MainWindow::onFolderSelected);
    connect(m_foldersPanel, &FoldersPanel::folderAdded, this, &MainWindow::onFolderAdded);
    connect(m_foldersPanel, &FoldersPanel::folderRemoved, this, &MainWindow::onFolderRemoved);

    m_grid = new WallpaperGrid;
    connect(m_grid, &WallpaperGrid::imageSelected, this, &MainWindow::onImageSelected);
    connect(m_grid, &WallpaperGrid::imageDoubleClicked, this, &MainWindow::onImageDoubleClicked);
    connect(m_grid, &WallpaperGrid::countChanged, this, &MainWindow::onGridCountChanged);

    m_preview = new PreviewPanel;
    connect(m_preview, &PreviewPanel::favoriteClicked, this, &MainWindow::onToggleFavorite);
    connect(m_preview, &PreviewPanel::applyClicked, this, &MainWindow::onApply);
    connect(m_preview, &PreviewPanel::randomClicked, this, &MainWindow::onRandom);
    connect(m_preview, &PreviewPanel::clearClicked, this, &MainWindow::onClear);

    m_gallerySplitter->addWidget(m_foldersPanel);
    m_gallerySplitter->addWidget(m_grid);
    m_gallerySplitter->addWidget(m_preview);
    m_gallerySplitter->setStretchFactor(1, 1);
    m_gallerySplitter->setHandleWidth(4);

    galleryLayout->addWidget(m_gallerySplitter);

    /* Settings page */
    m_settingsPanel = new SettingsPanel;
    connect(m_settingsPanel, &SettingsPanel::backendChanged, this, &MainWindow::onBackendChanged);
    connect(m_settingsPanel, &SettingsPanel::modeChanged, this, &MainWindow::onModeChanged);
    connect(m_settingsPanel, &SettingsPanel::settingsChanged, this, &MainWindow::onSettingsChanged);
    connect(m_settingsPanel, &SettingsPanel::daemonRequested, this, &MainWindow::onDaemonRequested);
    connect(m_settingsPanel, &SettingsPanel::marketSettingsChanged,
            this, &MainWindow::onMarketSettingsChanged);

    /* Market page */
    m_marketPanel = new MarketPanel;
    connect(m_marketPanel, &MarketPanel::statusMessage, this, &MainWindow::updateStatus);
    connect(m_marketPanel, &MarketPanel::wallpaperDownloaded,
            this, &MainWindow::onMarketDownloaded);

    m_pages->addWidget(m_galleryPage);
    m_pages->addWidget(m_settingsPanel);
    m_pages->addWidget(m_marketPanel);

    /* Status footer */
    auto *statusBar = new QFrame;
    statusBar->setObjectName("statusBar");
    auto *statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(14, 8, 14, 8);
    m_statusLabel = new QLabel("Ready");
    m_statusLabel->setObjectName("statusLabel");
    statusLayout->addWidget(m_statusLabel);
    statusLayout->addStretch();

    rightLayout->addWidget(topBar);
    rightLayout->addWidget(m_pages, 1);
    rightLayout->addWidget(statusBar);

    rootLayout->addWidget(m_sidebar);
    rootLayout->addWidget(rightColumn, 1);
}

void MainWindow::loadConfig() {
    config_t cfg;
    config_load(&cfg);

    int backendIndex;
    switch (cfg.backend) {
        case BACKEND_HYPRPAPER: backendIndex = 1; break;
        case BACKEND_MPVPPAPER: backendIndex = 2; break;
        case BACKEND_SWWW: backendIndex = 3; break;
        default: backendIndex = 0; break;
    }
    m_backendCombo->setCurrentIndex(backendIndex);
    m_settingsPanel->setBackend(backendIndex);

    QString mode = QString::fromUtf8(cfg.mode);
    int modeIndex = m_modeCombo->findText(mode, Qt::MatchFixedString);
    if (modeIndex < 0) modeIndex = 0;
    m_modeCombo->setCurrentIndex(modeIndex);
    m_settingsPanel->setMode(mode);

    m_settingsPanel->setWallustEnabled(cfg.wallust_enabled != 0);
    if (wallust_available()) {
        m_settingsPanel->setToolTip("Run 'wallust run' after applying the wallpaper");
    } else {
        m_settingsPanel->setToolTip("wallust is not installed; it will activate once installed");
    }
    m_settingsPanel->setWallustHook(QString::fromUtf8(cfg.wallust_hook));

    m_settingsPanel->setInterval(cfg.daemon_interval > 0 ? cfg.daemon_interval : 300);

    QString marketDir = QString::fromUtf8(cfg.market_download_dir);
    if (marketDir.isEmpty()) marketDir = defaultWallpaperDir();

    m_settingsPanel->setMarketDownloadDir(marketDir);
    m_settingsPanel->setWallhavenApiKey(QString::fromUtf8(cfg.wallhaven_api_key));
    m_settingsPanel->setWallhavenPurity(QString::fromUtf8(cfg.wallhaven_purity));

    m_marketPanel->setDownloadDir(marketDir);
    m_marketPanel->setWallhavenApiKey(QString::fromUtf8(cfg.wallhaven_api_key));
    m_marketPanel->setWallhavenPurity(QString::fromUtf8(cfg.wallhaven_purity));

    loadFavorites();
    loadRecent();
    loadFolders();

    if (cfg.last_wallpaper[0] != '\0') {
        m_preview->setWallpaper(QString::fromUtf8(cfg.last_wallpaper));
    }

    QString status = QString("Detected backend: %1").arg(backend_to_string(detect_backend()));
    if (!wallust_available()) {
        status += " | wallust not available";
    }
    updateStatus(status);

    /* loadFolders() already selected the first folder and loaded the grid. */
    updateSectionTitle(NavSidebar::Home);
}

void MainWindow::saveCurrentConfig(const char *path) {
    config_t cfg;
    config_load(&cfg);

    cfg.backend = selectedBackend();
    cfg.wallust_enabled = m_settingsPanel->wallustEnabled() ? 1 : 0;

    QByteArray hook = m_settingsPanel->wallustHook().toUtf8();
    strncpy(cfg.wallust_hook, hook.constData(), sizeof(cfg.wallust_hook) - 1);
    cfg.wallust_hook[sizeof(cfg.wallust_hook) - 1] = '\0';

    QByteArray mode = m_modeCombo->currentText().toUtf8();
    strncpy(cfg.mode, mode.constData(), sizeof(cfg.mode) - 1);
    cfg.mode[sizeof(cfg.mode) - 1] = '\0';

    if (path) {
        strncpy(cfg.last_wallpaper, path, sizeof(cfg.last_wallpaper) - 1);
        cfg.last_wallpaper[sizeof(cfg.last_wallpaper) - 1] = '\0';
    }

    cfg.daemon_interval = m_settingsPanel->interval();

    config_save(&cfg);
    saveFolders();
    saveFavorites();
    saveRecent();
}

void MainWindow::loadFolders() {
    config_t cfg;
    config_load(&cfg);

    QStringList folders;
    if (cfg.folder_count == 0) {
        QString defaultDir = defaultWallpaperDir();
        if (!QDir(defaultDir).exists()) {
            QDir().mkpath(defaultDir);
        }
        folders.append(defaultDir);
    } else {
        for (int i = 0; i < cfg.folder_count; i++) {
            folders.append(QString::fromUtf8(cfg.folders[i]));
        }
    }

    m_foldersPanel->setFolders(folders);
    onFolderSelected(m_foldersPanel->selectedFolder());
}

void MainWindow::saveFolders() {
    config_t cfg;
    config_load(&cfg);

    /* Clear existing folders. */
    for (int i = cfg.folder_count - 1; i >= 0; --i) {
        config_remove_folder(&cfg, i);
    }

    for (int i = 0; i < m_foldersPanel->count(); i++) {
        QByteArray folder = m_foldersPanel->folderAt(i).toUtf8();
        config_add_folder(&cfg, folder.constData());
    }

    config_save(&cfg);
}

void MainWindow::loadFavorites() {
    m_favoritePaths.clear();
    QFile f(favoritesFile());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QTextStream in(&f);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (!line.isEmpty() && QFile::exists(line)) {
            m_favoritePaths.append(line);
        }
    }
}

void MainWindow::saveFavorites() {
    QDir().mkpath(configDir());
    QFile f(favoritesFile());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;

    QTextStream out(&f);
    for (const QString &path : m_favoritePaths) {
        out << path << "\n";
    }
}

void MainWindow::loadRecent() {
    m_recentPaths.clear();
    QFile f(recentFile());
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;

    QTextStream in(&f);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (!line.isEmpty() && QFile::exists(line)) {
            m_recentPaths.append(line);
        }
    }
}

void MainWindow::saveRecent() {
    QDir().mkpath(configDir());
    QFile f(recentFile());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;

    QTextStream out(&f);
    for (const QString &path : m_recentPaths) {
        out << path << "\n";
    }
}

void MainWindow::addToRecent(const QString &path) {
    m_recentPaths.removeAll(path);
    m_recentPaths.prepend(path);
    while (m_recentPaths.size() > 50) {
        m_recentPaths.removeLast();
    }
    saveRecent();
}

bool MainWindow::isFavorite(const QString &path) const {
    return m_favoritePaths.contains(path);
}

void MainWindow::refreshFavoriteButton() {
    QString path = m_grid->selectedPath();
    if (path.isEmpty()) {
        m_preview->setIsFavorite(false);
        return;
    }
    m_preview->setIsFavorite(isFavorite(path));
}

void MainWindow::setGallerySection(NavSidebar::Section section) {
    m_currentSection = section;
    m_pages->setCurrentIndex(GalleryPage);

    switch (section) {
        case NavSidebar::Home:
            m_foldersPanel->show();
            if (!m_currentFolder.isEmpty() && QDir(m_currentFolder).exists()) {
                m_grid->loadFromFolder(m_currentFolder);
            } else if (!m_foldersPanel->selectedFolder().isEmpty()) {
                m_grid->loadFromFolder(m_foldersPanel->selectedFolder());
            }
            break;
        case NavSidebar::Favorites:
            m_foldersPanel->hide();
            m_grid->setWallpapers(m_favoritePaths);
            break;
        case NavSidebar::Recent:
            m_foldersPanel->hide();
            m_grid->setWallpapers(m_recentPaths);
            break;
        case NavSidebar::Market:
            m_foldersPanel->hide();
            m_pages->setCurrentIndex(MarketPage);
            break;
        case NavSidebar::Settings:
            break;
    }

    updateSectionTitle(section);
    refreshFavoriteButton();
}

void MainWindow::onSectionChanged(NavSidebar::Section section) {
    setGallerySection(section);
}

void MainWindow::onSettingsToggled(bool active) {
    if (active) {
        m_pages->setCurrentIndex(SettingsPage);
        updateSectionTitle(NavSidebar::Settings);
    } else if (m_pages->currentIndex() != GalleryPage) {
        setGallerySection(m_currentSection);
    }
}

void MainWindow::onFolderSelected(const QString &folder) {
    if (folder.isEmpty()) return;
    m_currentFolder = folder;
    if (m_currentSection != NavSidebar::Home) {
        m_sidebar->setSection(NavSidebar::Home);
        m_currentSection = NavSidebar::Home;
    }
    m_foldersPanel->show();
    m_grid->loadFromFolder(folder);
    updateSectionTitle(NavSidebar::Home);
}

void MainWindow::onFolderAdded(const QString &folder) {
    config_t cfg;
    config_load(&cfg);
    config_add_folder(&cfg, folder.toUtf8().constData());
    config_save(&cfg);
}

void MainWindow::onFolderRemoved(int row) {
    config_t cfg;
    config_load(&cfg);
    config_remove_folder(&cfg, row);
    config_save(&cfg);
}

void MainWindow::onImageSelected(const QString &path) {
    m_preview->setWallpaper(path);
    refreshFavoriteButton();
}

void MainWindow::onImageDoubleClicked(const QString &path) {
    m_preview->setWallpaper(path);
    applySelectedImage(path);
}

void MainWindow::onGridCountChanged(int visible, int total) {
    if (visible == total) {
        updateStatus(QString("%1 wallpapers").arg(total));
    } else {
        updateStatus(QString("%1 of %2 match").arg(visible).arg(total));
    }
}

void MainWindow::onToggleFavorite() {
    QString path = m_grid->selectedPath();
    if (path.isEmpty()) return;

    if (m_favoritePaths.contains(path)) {
        m_favoritePaths.removeAll(path);
    } else {
        m_favoritePaths.append(path);
    }
    saveFavorites();
    refreshFavoriteButton();

    if (m_currentSection == NavSidebar::Favorites) {
        setGallerySection(NavSidebar::Favorites);
    }
}

void MainWindow::onApply() {
    QString path = m_grid->selectedPath();
    if (path.isEmpty()) {
        updateStatus("Select a wallpaper first");
        return;
    }
    applySelectedImage(path);
}

void MainWindow::onRandom() {
    if (m_grid->count() == 0 || m_grid->visibleCount() == 0) {
        updateStatus("No wallpapers available");
        return;
    }
    m_grid->selectRandom();
    QString path = m_grid->selectedPath();
    if (!path.isEmpty()) {
        m_preview->setWallpaper(path);
        applySelectedImage(path);
        updateStatus(QString("Random: %1").arg(QFileInfo(path).fileName()));
    }
}

void MainWindow::onClear() {
    clear_wallpaper();
    updateStatus("Wallpaper cleared");
}

void MainWindow::onBackendChanged(int index) {
    (void)index;
    int comboIndex = m_backendCombo->currentIndex();
    m_settingsPanel->setBackend(comboIndex);

    backend_t b = selectedBackend();
    if (!backend_available(b)) {
        updateStatus(QString("Backend '%1' not available").arg(backend_to_string(b)));
    }
    saveCurrentConfig(nullptr);
}

void MainWindow::onModeChanged(int index) {
    (void)index;
    m_settingsPanel->setMode(m_modeCombo->currentText());
    saveCurrentConfig(nullptr);
}

void MainWindow::onSettingsChanged() {
    m_backendCombo->setCurrentIndex(m_settingsPanel->backend());
    m_modeCombo->setCurrentText(m_settingsPanel->mode());
    saveCurrentConfig(nullptr);
}

void MainWindow::onDaemonRequested(bool start) {
    if (start) {
        if (m_currentFolder.isEmpty() || !QDir(m_currentFolder).exists()) {
            QMessageBox::warning(this, "Daemon", "Select a valid folder first.");
            m_settingsPanel->setDaemonRunning(false);
            return;
        }

        backend_t b = selectedBackend();
        if (!backend_available(b)) {
            QMessageBox::warning(this, "Daemon",
                                 QString("Selected backend '%1' is not available.").arg(backend_to_string(b)));
            m_settingsPanel->setDaemonRunning(false);
            return;
        }

        QByteArray mode = m_modeCombo->currentText().toUtf8();
        int interval = m_settingsPanel->interval();

        int enable_wallust = m_settingsPanel->wallustEnabled() ? 1 : 0;
        config_t cfg;
        config_load(&cfg);
        strncpy(cfg.wallust_hook, m_settingsPanel->wallustHook().toUtf8().constData(),
                sizeof(cfg.wallust_hook) - 1);
        cfg.wallust_hook[sizeof(cfg.wallust_hook) - 1] = '\0';

        if (daemonize_random(m_currentFolder.toUtf8().constData(), interval, b, mode.constData(),
                             enable_wallust, cfg.wallust_hook) != 0) {
            QMessageBox::critical(this, "Daemon", "Could not start daemon.");
            m_settingsPanel->setDaemonRunning(false);
            return;
        }

        m_settingsPanel->setDaemonRunning(true);
        updateStatus(QString("Daemon started (%1s) with %2").arg(interval).arg(backend_to_string(b)));
        m_daemonRunning = true;
    } else {
        int pid = 0;
        if (readDaemonPid(&pid)) {
            if (kill(pid, SIGTERM) == 0) {
                updateStatus("Daemon stopped");
            } else {
                updateStatus("Could not stop daemon");
            }
        } else {
            updateStatus("No active daemon");
        }
        m_settingsPanel->setDaemonRunning(false);
        m_daemonRunning = false;
    }
}

backend_t MainWindow::selectedBackend() const {
    int idx = m_backendCombo->currentIndex();
    if (idx == 1) return BACKEND_HYPRPAPER;
    if (idx == 2) return BACKEND_MPVPPAPER;
    if (idx == 3) return BACKEND_SWWW;
    return BACKEND_SWAYBG;
}

backend_t MainWindow::preferredBackendFor(const QString &path) const {
    backend_t chosen = selectedBackend();

    if (chosen == BACKEND_MPVPPAPER || chosen == BACKEND_SWWW) return chosen;

    if (isVideo(path) && backend_available(BACKEND_MPVPPAPER))
        return BACKEND_MPVPPAPER;
    if (isAnimatedFile(path) && backend_available(BACKEND_SWWW))
        return BACKEND_SWWW;
    if (isAnimatedFile(path) && backend_available(BACKEND_MPVPPAPER))
        return BACKEND_MPVPPAPER;

    return chosen;
}

void MainWindow::applySelectedImage(const QString &path) {
    if (!QFile::exists(path)) {
        updateStatus("The wallpaper does not exist");
        return;
    }

    backend_t b = preferredBackendFor(path);
    if (!backend_available(b)) {
        updateStatus(QString("Backend not available: %1").arg(backend_to_string(b)));
        return;
    }

    QByteArray mode = m_modeCombo->currentText().toUtf8();
    if (set_wallpaper(b, path.toUtf8().constData(), mode.constData()) != 0) {
        updateStatus("Error applying wallpaper");
        return;
    }

    addToRecent(path);
    saveCurrentConfig(path.toUtf8().constData());

    if (m_settingsPanel->wallustEnabled()) {
        if (wallust_available()) {
            wallust_run(path.toUtf8().constData());
        }
        config_t cfg;
        config_load(&cfg);
        wallust_hook_run(cfg.wallust_hook, path.toUtf8().constData());
        if (wallust_available()) {
            updateStatus(QString("Applied with wallust: %1").arg(QFileInfo(path).fileName()));
        } else {
            updateStatus(QString("Applied; wallust not available: %1").arg(QFileInfo(path).fileName()));
        }
    } else {
        updateStatus(QString("Applied: %1").arg(QFileInfo(path).fileName()));
    }
}

void MainWindow::onMarketSettingsChanged() {
    QString marketDir = m_settingsPanel->marketDownloadDir();
    if (marketDir.isEmpty()) {
        marketDir = defaultWallpaperDir();
    } else if (marketDir.startsWith("~/")) {
        marketDir = QDir::homePath() + marketDir.mid(1);
    }

    config_t cfg;
    config_load(&cfg);

    QByteArray dir = marketDir.toUtf8();
    strncpy(cfg.market_download_dir, dir.constData(), sizeof(cfg.market_download_dir) - 1);
    cfg.market_download_dir[sizeof(cfg.market_download_dir) - 1] = '\0';

    QByteArray apiKey = m_settingsPanel->wallhavenApiKey().toUtf8();
    strncpy(cfg.wallhaven_api_key, apiKey.constData(), sizeof(cfg.wallhaven_api_key) - 1);
    cfg.wallhaven_api_key[sizeof(cfg.wallhaven_api_key) - 1] = '\0';

    QByteArray purity = m_settingsPanel->wallhavenPurity().toUtf8();
    strncpy(cfg.wallhaven_purity, purity.constData(), sizeof(cfg.wallhaven_purity) - 1);
    cfg.wallhaven_purity[sizeof(cfg.wallhaven_purity) - 1] = '\0';

    config_save(&cfg);

    m_marketPanel->setDownloadDir(marketDir);
    m_marketPanel->setWallhavenApiKey(m_settingsPanel->wallhavenApiKey());
    m_marketPanel->setWallhavenPurity(m_settingsPanel->wallhavenPurity());
}

void MainWindow::onMarketDownloaded(const QString &path, const MarketItem &/*item*/, bool applyAfter) {
    if (path.isEmpty() || !QFile::exists(path)) return;

    addToRecent(path);

    QString dir = QFileInfo(path).absolutePath();
    bool inFolders = false;
    for (int i = 0; i < m_foldersPanel->count(); ++i) {
        if (m_foldersPanel->folderAt(i) == dir) {
            inFolders = true;
            break;
        }
    }
    if (!inFolders) {
        m_foldersPanel->addFolder(dir);
        onFolderAdded(dir);
    }

    if (m_currentSection == NavSidebar::Home && m_currentFolder == dir) {
        m_grid->loadFromFolder(dir);
    }

    updateStatus(QString("Downloaded: %1").arg(QFileInfo(path).fileName()));

    if (applyAfter) {
        applySelectedImage(path);
    }
}

void MainWindow::updateStatus(const QString &msg) {
    m_statusLabel->setText(msg);
}

void MainWindow::updateSectionTitle(NavSidebar::Section section) {
    switch (section) {
        case NavSidebar::Home: m_sectionTitle->setText("<b style='font-size:18px; color:#f0f6fc;'>Home</b>"); break;
        case NavSidebar::Favorites: m_sectionTitle->setText("<b style='font-size:18px; color:#f0f6fc;'>Favorites</b>"); break;
        case NavSidebar::Recent: m_sectionTitle->setText("<b style='font-size:18px; color:#f0f6fc;'>Recent</b>"); break;
        case NavSidebar::Market: m_sectionTitle->setText("<b style='font-size:18px; color:#f0f6fc;'>Market</b>"); break;
        case NavSidebar::Settings: m_sectionTitle->setText("<b style='font-size:18px; color:#f0f6fc;'>Settings</b>"); break;
    }
}

bool MainWindow::readDaemonPid(int *pid) {
    FILE *f = fopen("/tmp/archpaper.pid", "r");
    if (!f) return false;
    int p;
    bool ok = (fscanf(f, "%d", &p) == 1);
    fclose(f);
    if (ok && pid) *pid = p;
    return ok;
}
