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
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QTextStream>
#include <QToolButton>
#include <QVBoxLayout>

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

} // namespace

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

static QPushButton *createToolButton(const QString &text, const QString &tooltip,
                                     const QString &objName, bool checkable = false) {
    auto *btn = new QPushButton(text);
    btn->setObjectName(objName);
    btn->setToolTip(tooltip);
    btn->setCheckable(checkable);
    btn->setFixedSize(34, 34);
    btn->setCursor(Qt::PointingHandCursor);
    return btn;
}

static QFrame *createVSeparator(QWidget *parent) {
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::VLine);
    line->setObjectName("toolbarSeparator");
    line->setFixedSize(1, 24);
    return line;
}

void MainWindow::setupUi() {
    setWindowTitle("archpaper");
    resize(1400, 860);
    setMinimumSize(1100, 680);

    applyStyleSheet();

    auto *central = new QWidget;
    setCentralWidget(central);

    auto *rootLayout = new QHBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    /* Sidebar */
    m_sidebar = new NavSidebar(this);
    connect(m_sidebar, &NavSidebar::sectionChanged, this, &MainWindow::onSectionChanged);
    connect(m_sidebar, &NavSidebar::folderSelected, this, &MainWindow::onFolderSelected);
    connect(m_sidebar, &NavSidebar::folderAdded, this, &MainWindow::onFolderAdded);
    connect(m_sidebar, &NavSidebar::folderRemoved, this, &MainWindow::onFolderRemoved);

    /* Central area */
    auto *centralColumn = new QWidget;
    auto *centralLayout = new QVBoxLayout(centralColumn);
    centralLayout->setContentsMargins(12, 12, 12, 12);
    centralLayout->setSpacing(10);

    /* Toolbar */
    auto *toolbar = new QFrame;
    toolbar->setObjectName("toolbar");
    auto *tbLayout = new QHBoxLayout(toolbar);
    tbLayout->setContentsMargins(14, 8, 14, 8);
    tbLayout->setSpacing(8);

    m_applyBtn = createToolButton("\u25b6", "Apply selected wallpaper", "primaryToolButton", false);
    m_randomBtn = createToolButton("\u21c4", "Pick a random wallpaper", "secondaryToolButton", false);
    m_favoriteBtn = createToolButton("\u2606", "Toggle favorite", "favoriteToolButton", true);
    m_clearBtn = createToolButton("\u2715", "Remove current wallpaper", "dangerToolButton", false);

    connect(m_applyBtn, &QPushButton::clicked, this, &MainWindow::onApply);
    connect(m_randomBtn, &QPushButton::clicked, this, &MainWindow::onRandom);
    connect(m_favoriteBtn, &QPushButton::clicked, this, &MainWindow::onToggleFavorite);
    connect(m_clearBtn, &QPushButton::clicked, this, &MainWindow::onClear);

    m_filterEdit = new QLineEdit;
    m_filterEdit->setObjectName("searchEdit");
    m_filterEdit->setPlaceholderText("Filter...");
    m_filterEdit->setClearButtonEnabled(true);
    m_filterEdit->setMinimumWidth(180);
    m_filterEdit->setMaximumWidth(260);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &MainWindow::onFilterTextChanged);

    tbLayout->addWidget(m_applyBtn);
    tbLayout->addWidget(m_randomBtn);
    tbLayout->addWidget(m_favoriteBtn);
    tbLayout->addWidget(m_clearBtn);
    tbLayout->addWidget(createVSeparator(toolbar));
    tbLayout->addWidget(m_filterEdit);
    tbLayout->addStretch();

    auto *backendLabel = new QLabel("Backend:");
    backendLabel->setObjectName("mutedLabel");
    m_backendCombo = new QComboBox;
    m_backendCombo->addItems({"swaybg", "hyprpaper", "mpvpaper", "awww"});
    connect(m_backendCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onBackendChanged);

    auto *modeLabel = new QLabel("Mode:");
    modeLabel->setObjectName("mutedLabel");
    m_modeCombo = new QComboBox;
    m_modeCombo->addItems({"fill", "fit", "stretch", "center", "tile"});
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onModeChanged);

    m_previewToggleBtn = createToolButton("\u29c9", "Toggle preview panel", "secondaryToolButton", true);
    m_previewToggleBtn->setChecked(true);
    connect(m_previewToggleBtn, &QPushButton::clicked, this, &MainWindow::onTogglePreview);

    m_daemonBtn = createToolButton("\u26a1", "Start/stop automatic wallpaper changes", "daemonToolButton", true);
    connect(m_daemonBtn, &QPushButton::clicked, this, &MainWindow::onDaemonRequested);

    tbLayout->addWidget(backendLabel);
    tbLayout->addWidget(m_backendCombo);
    tbLayout->addSpacing(10);
    tbLayout->addWidget(modeLabel);
    tbLayout->addWidget(m_modeCombo);
    tbLayout->addWidget(createVSeparator(toolbar));
    tbLayout->addWidget(m_previewToggleBtn);
    tbLayout->addWidget(m_daemonBtn);

    /* Content pages */
    m_pages = new QStackedWidget(this);

    /* Library page: grid + preview */
    m_libraryPage = new QWidget;
    m_libraryPage->setObjectName("libraryPage");
    auto *libraryLayout = new QHBoxLayout(m_libraryPage);
    libraryLayout->setContentsMargins(0, 0, 0, 0);
    libraryLayout->setSpacing(12);

    m_grid = new WallpaperGrid;
    connect(m_grid, &WallpaperGrid::imageSelected, this, &MainWindow::onImageSelected);
    connect(m_grid, &WallpaperGrid::imageDoubleClicked, this, &MainWindow::onImageDoubleClicked);
    connect(m_grid, &WallpaperGrid::countChanged, this, &MainWindow::onGridCountChanged);

    m_preview = new PreviewPanel;
    connect(m_preview, &PreviewPanel::favoriteClicked, this, &MainWindow::onToggleFavorite);
    connect(m_preview, &PreviewPanel::applyClicked, this, &MainWindow::onApply);
    connect(m_preview, &PreviewPanel::randomClicked, this, &MainWindow::onRandom);
    connect(m_preview, &PreviewPanel::clearClicked, this, &MainWindow::onClear);

    libraryLayout->addWidget(m_grid, 1);
    libraryLayout->addWidget(m_preview);

    /* Settings page */
    m_settingsPanel = new SettingsPanel;
    connect(m_settingsPanel, &SettingsPanel::settingsChanged, this, &MainWindow::onSettingsChanged);
    connect(m_settingsPanel, &SettingsPanel::daemonRequested, this, &MainWindow::onDaemonRequested);

    m_pages->addWidget(m_libraryPage);
    m_pages->addWidget(m_settingsPanel);

    /* Status footer */
    auto *statusBar = new QFrame;
    statusBar->setObjectName("statusBar");
    auto *statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(14, 8, 14, 8);
    m_statusLabel = new QLabel("Ready");
    m_statusLabel->setObjectName("statusLabel");
    statusLayout->addWidget(m_statusLabel);
    statusLayout->addStretch();

    centralLayout->addWidget(toolbar);
    centralLayout->addWidget(m_pages, 1);
    centralLayout->addWidget(statusBar);

    rootLayout->addWidget(m_sidebar);
    rootLayout->addWidget(centralColumn, 1);
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

    QString mode = QString::fromUtf8(cfg.mode);
    int modeIndex = m_modeCombo->findText(mode, Qt::MatchFixedString);
    if (modeIndex < 0) modeIndex = 0;
    m_modeCombo->setCurrentIndex(modeIndex);

    m_settingsPanel->setWallustEnabled(cfg.wallust_enabled != 0);
    m_settingsPanel->setWallustHook(QString::fromUtf8(cfg.wallust_hook));
    m_settingsPanel->setCacheQuality(QString::fromUtf8(cfg.cache_quality));
    m_settingsPanel->setMpvpaperProfile(QString::fromUtf8(cfg.mpvpaper_profile));
    m_settingsPanel->setMpvpaperHwdec(cfg.mpvpaper_hwdec != 0);
    m_settingsPanel->setInterval(cfg.daemon_interval > 0 ? cfg.daemon_interval : 300);

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

    /* loadFolders() already selects the first folder and loads the grid. */
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

    QByteArray cacheQuality = m_settingsPanel->cacheQuality().toUtf8();
    strncpy(cfg.cache_quality, cacheQuality.constData(), sizeof(cfg.cache_quality) - 1);
    cfg.cache_quality[sizeof(cfg.cache_quality) - 1] = '\0';

    QByteArray mpvpaperProfile = m_settingsPanel->mpvpaperProfile().toUtf8();
    strncpy(cfg.mpvpaper_profile, mpvpaperProfile.constData(), sizeof(cfg.mpvpaper_profile) - 1);
    cfg.mpvpaper_profile[sizeof(cfg.mpvpaper_profile) - 1] = '\0';

    cfg.mpvpaper_hwdec = m_settingsPanel->mpvpaperHwdec() ? 1 : 0;

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

    m_sidebar->setFolders(folders);
    onFolderSelected(m_sidebar->selectedFolder());
}

void MainWindow::saveFolders() {
    config_t cfg;
    config_load(&cfg);

    for (int i = cfg.folder_count - 1; i >= 0; --i) {
        config_remove_folder(&cfg, i);
    }

    for (int i = 0; i < m_sidebar->folderCount(); i++) {
        QByteArray folder = m_sidebar->folderAt(i).toUtf8();
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
    bool hasSelection = !path.isEmpty();
    m_applyBtn->setEnabled(hasSelection);
    m_favoriteBtn->setEnabled(hasSelection);

    if (path.isEmpty()) {
        m_preview->setIsFavorite(false);
        m_favoriteBtn->setChecked(false);
        m_favoriteBtn->setText("\u2606");
        return;
    }

    bool favorite = isFavorite(path);
    m_preview->setIsFavorite(favorite);
    m_favoriteBtn->setChecked(favorite);
    m_favoriteBtn->setText(favorite ? "\u2605" : "\u2606");
}

void MainWindow::onSectionChanged(NavSidebar::Section section) {
    if (section == NavSidebar::Settings) {
        m_pages->setCurrentIndex(SettingsPage);
        m_daemonBtn->setChecked(m_daemonRunning);
    } else {
        m_pages->setCurrentIndex(LibraryPage);
        setLibrarySection(section);
    }
}

void MainWindow::setLibrarySection(NavSidebar::Section section) {
    m_currentSection = section;

    switch (section) {
        case NavSidebar::Home:
            if (!m_currentFolder.isEmpty() && QDir(m_currentFolder).exists()) {
                m_grid->loadFromFolder(m_currentFolder);
            } else if (!m_sidebar->selectedFolder().isEmpty()) {
                onFolderSelected(m_sidebar->selectedFolder());
            }
            break;
        case NavSidebar::Favorites:
            m_grid->setWallpapers(m_favoritePaths);
            break;
        case NavSidebar::Recent:
            m_grid->setWallpapers(m_recentPaths);
            break;
        case NavSidebar::Settings:
            break;
    }

    refreshFavoriteButton();
}

void MainWindow::onFolderSelected(const QString &folder) {
    if (folder.isEmpty()) return;
    m_currentFolder = folder;
    m_sidebar->setSection(NavSidebar::Home);
    m_currentSection = NavSidebar::Home;
    m_grid->loadFromFolder(folder);
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

void MainWindow::onFilterTextChanged(const QString &text) {
    m_grid->setFilter(text);
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
        setLibrarySection(NavSidebar::Favorites);
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

void MainWindow::onTogglePreview() {
    if (m_previewToggleBtn->isChecked()) {
        m_preview->show();
    } else {
        m_preview->hide();
    }
}

void MainWindow::onBackendChanged(int index) {
    (void)index;
    backend_t b = selectedBackend();
    if (!backend_available(b)) {
        updateStatus(QString("Backend '%1' not available").arg(backend_to_string(b)));
    }
    saveCurrentConfig(nullptr);
}

void MainWindow::onModeChanged(int index) {
    (void)index;
    saveCurrentConfig(nullptr);
}

void MainWindow::onSettingsChanged() {
    saveCurrentConfig(nullptr);
}

void MainWindow::onDaemonRequested(bool start) {
    if (start) {
        if (m_currentFolder.isEmpty() || !QDir(m_currentFolder).exists()) {
            QMessageBox::warning(this, "Daemon", "Select a valid folder first.");
            m_daemonBtn->setChecked(false);
            m_settingsPanel->setDaemonRunning(false);
            return;
        }

        backend_t b = selectedBackend();
        if (!backend_available(b)) {
            QMessageBox::warning(this, "Daemon",
                                 QString("Selected backend '%1' is not available.").arg(backend_to_string(b)));
            m_daemonBtn->setChecked(false);
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

        strncpy(cfg.cache_quality, m_settingsPanel->cacheQuality().toUtf8().constData(),
                sizeof(cfg.cache_quality) - 1);
        cfg.cache_quality[sizeof(cfg.cache_quality) - 1] = '\0';

        if (daemonize_random(m_currentFolder.toUtf8().constData(), interval, b, mode.constData(),
                             enable_wallust, cfg.wallust_hook, cfg.cache_quality) != 0) {
            QMessageBox::critical(this, "Daemon", "Could not start daemon.");
            m_daemonBtn->setChecked(false);
            m_settingsPanel->setDaemonRunning(false);
            return;
        }

        m_daemonBtn->setChecked(true);
        m_daemonBtn->setText("Stop Daemon");
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
        m_daemonBtn->setChecked(false);
        m_daemonBtn->setText("Start Daemon");
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
    return select_backend_for_path(path.toUtf8().constData(), selectedBackend());
}

void MainWindow::applySelectedImage(const QString &path) {
    if (!QFile::exists(path)) {
        updateStatus("The wallpaper does not exist");
        return;
    }

    config_t cfg;
    config_load(&cfg);

    backend_t b = preferredBackendFor(path);
    if (!backend_available(b)) {
        updateStatus(QString("Backend not available: %1").arg(backend_to_string(b)));
        return;
    }

    QByteArray mode = m_modeCombo->currentText().toUtf8();
    if (set_wallpaper(b, path.toUtf8().constData(), mode.constData(),
                      cfg.cache_quality) != 0) {
        updateStatus("Error applying wallpaper");
        return;
    }

    addToRecent(path);
    saveCurrentConfig(path.toUtf8().constData());

    if (m_settingsPanel->wallustEnabled()) {
        if (wallust_available()) {
            wallust_run(path.toUtf8().constData());
        }
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

void MainWindow::updateStatus(const QString &msg) {
    m_statusLabel->setText(msg);
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
