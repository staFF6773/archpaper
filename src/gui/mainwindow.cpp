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
#include <QCheckBox>
#include <QSpinBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QMenu>
#include <QPushButton>
#include <QPalette>
#include <QScrollArea>
#include <QShortcut>
#include <QSplitter>
#include <QStackedWidget>
#include <QStandardPaths>
#include <QThread>
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
#include "archpaper/history.h"
#include "archpaper/process.h"
#include "archpaper/storage.h"
#include "archpaper/wallpaper.h"
#include "archpaper/engine.h"
}

#include <cstdlib>
#include <memory>

namespace {

QString defaultWallpaperDir() {
    return QDir::homePath() + "/Pictures/Wallpapers";
}

QStringList historyPaths(ap_history_kind kind, ap_result *result) {
    ap_path_list list = {};
    *result = ap_history_load(kind, &list);
    QStringList paths;
    for (size_t i = 0; i < list.count; ++i) paths.append(QString::fromUtf8(list.paths[i]));
    ap_path_list_free(&list);
    return paths;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUi();
    loadConfig();
    m_loadingConfig = false;
}

MainWindow::~MainWindow() {
    if (m_applyThread) {
        m_applyThread->disconnect(this);
        m_applyThread->requestInterruption();
        m_applyThread->wait();
        delete m_applyThread;
    }
}

void MainWindow::applyStyleSheet() {
    qApp->setStyle("Fusion");
    QPalette palette;
    palette.setColor(QPalette::Window, QColor("#121212"));
    palette.setColor(QPalette::WindowText, QColor("#eeeeee"));
    palette.setColor(QPalette::Base, QColor("#191919"));
    palette.setColor(QPalette::AlternateBase, QColor("#222222"));
    palette.setColor(QPalette::Text, QColor("#eeeeee"));
    palette.setColor(QPalette::Button, QColor("#202020"));
    palette.setColor(QPalette::ButtonText, QColor("#eeeeee"));
    palette.setColor(QPalette::Highlight, QColor("#555555"));
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::PlaceholderText, QColor("#888888"));
    palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#707070"));
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#707070"));
    qApp->setPalette(palette);
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
    btn->setFixedSize(32, 32);
    btn->setAccessibleName(tooltip);
    btn->setCursor(Qt::PointingHandCursor);
    return btn;
}

void MainWindow::setupUi() {
    setWindowTitle("archpaper");
    resize(1100, 720);
    setMinimumSize(820, 520);

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
    centralLayout->setContentsMargins(16, 14, 16, 8);
    centralLayout->setSpacing(10);

    /* Toolbar */
    auto *toolbar = new QFrame;
    toolbar->setObjectName("toolbar");
    auto *tbLayout = new QHBoxLayout(toolbar);
    tbLayout->setContentsMargins(0, 0, 0, 8);
    tbLayout->setSpacing(8);

    m_sectionTitle = new QLabel("Library");
    m_sectionTitle->setObjectName("panelTitle");
    m_countLabel = new QLabel("0 wallpapers");
    m_countLabel->setObjectName("mutedLabel");
    auto *heading = new QVBoxLayout;
    heading->setSpacing(2);
    heading->addWidget(m_sectionTitle);
    heading->addWidget(m_countLabel);

    m_applyBtn = new QPushButton("Apply wallpaper");
    m_applyBtn->setObjectName("primaryButton");
    m_applyBtn->setToolTip("Apply selected wallpaper (Enter in the grid)");
    m_favoriteBtn = createToolButton("\u2606", "Toggle favorite", "favoriteToolButton", true);

    connect(m_applyBtn, &QPushButton::clicked, this, &MainWindow::onApply);
    connect(m_favoriteBtn, &QPushButton::clicked, this, &MainWindow::onToggleFavorite);

    m_filterEdit = new QLineEdit;
    m_filterEdit->setObjectName("searchEdit");
    m_filterEdit->setPlaceholderText("Search wallpapers  ·  Ctrl+F");
    m_filterEdit->setAccessibleName("Search wallpapers");
    m_filterEdit->setClearButtonEnabled(true);
    m_filterEdit->setMinimumWidth(200);
    m_filterEdit->setMaximumWidth(300);
    connect(m_filterEdit, &QLineEdit::textChanged, this, &MainWindow::onFilterTextChanged);

    tbLayout->addLayout(heading);
    tbLayout->addStretch();
    tbLayout->addWidget(m_filterEdit, 1);

    m_backendCombo = new QComboBox;
    m_backendCombo->addItems({"swaybg", "hyprpaper", "mpvpaper", "awww", "linux-wallpaperengine"});
    connect(m_backendCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onBackendChanged);

    m_modeCombo = new QComboBox;
    m_modeCombo->addItems({"fill", "fit", "stretch", "center", "tile"});
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onModeChanged);

    m_previewToggleBtn = new QPushButton("Preview");
    m_previewToggleBtn->setCheckable(true);
    m_previewToggleBtn->setToolTip("Show or hide preview (Ctrl+P)");
    m_previewToggleBtn->setChecked(true);
    connect(m_previewToggleBtn, &QPushButton::clicked, this, &MainWindow::onTogglePreview);

    /* Content pages */
    m_pages = new QStackedWidget(this);

    /* Library page: grid + preview */
    m_libraryPage = new QWidget;
    m_libraryPage->setObjectName("libraryPage");
    auto *libraryLayout = new QVBoxLayout(m_libraryPage);
    libraryLayout->setContentsMargins(0, 0, 0, 0);
    libraryLayout->setSpacing(10);

    auto *actions = new QHBoxLayout;
    actions->setSpacing(6);
    actions->addWidget(m_applyBtn);
    actions->addWidget(m_favoriteBtn);
    auto *moreButton = new QToolButton;
    moreButton->setText("More");
    moreButton->setAccessibleName("More wallpaper actions");
    moreButton->setPopupMode(QToolButton::InstantPopup);
    auto *menu = new QMenu(moreButton);
    menu->addAction("Apply random wallpaper", this, &MainWindow::onRandom);
    menu->addAction("Import Wallpaper Engine from Steam", this, [this]() {
        ap_path_list found = {};
        ap_result rc = ap_engine_discover(&found);
        if (rc != AP_OK) { updateStatus(ap_error_string(rc)); return; }
        config_t cfg;
        if (!readUiConfig(&cfg)) { ap_path_list_free(&found); return; }
        int added = 0;
        for (size_t i = 0; i < found.count; ++i) {
            const int before = cfg.folder_count;
            rc = static_cast<ap_result>(config_add_folder(&cfg, found.paths[i]));
            if (rc != AP_OK) break;
            added += cfg.folder_count - before;
        }
        if (rc == AP_OK) rc = static_cast<ap_result>(config_save(&cfg));
        if (rc == AP_OK) {
            loadFolders();
            updateStatus(found.count ? QString("Steam: %1 new Wallpaper Engine folders imported").arg(added)
                                     : "No downloaded Wallpaper Engine Workshop folders found");
        } else updateStatus(ap_error_string(rc));
        ap_path_list_free(&found);
    });
    menu->addSeparator();
    menu->addAction("Clear current wallpaper", this, &MainWindow::onClear);
    moreButton->setMenu(menu);
    actions->addWidget(moreButton);
    actions->addStretch();
    actions->addWidget(m_previewToggleBtn);
    libraryLayout->addLayout(actions);

    m_grid = new WallpaperGrid;
    connect(m_grid, &WallpaperGrid::imageSelected, this, &MainWindow::onImageSelected);
    connect(m_grid, &WallpaperGrid::imageDoubleClicked, this, &MainWindow::onImageDoubleClicked);
    connect(m_grid, &WallpaperGrid::countChanged, this, &MainWindow::onGridCountChanged);
    connect(m_grid, &WallpaperGrid::errorOccurred, this, &MainWindow::updateStatus);

    m_preview = new PreviewPanel;
    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->setChildrenCollapsible(false);
    splitter->setHandleWidth(6);
    splitter->addWidget(m_grid);
    splitter->addWidget(m_preview);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setSizes({620, 250});
    libraryLayout->addWidget(splitter, 1);

    /* Settings page */
    m_settingsPanel = new SettingsPanel;
    connect(m_settingsPanel, &SettingsPanel::settingsChanged, this, &MainWindow::onSettingsChanged);
    connect(m_settingsPanel, &SettingsPanel::daemonRequested, this, &MainWindow::onDaemonRequested);

    auto *playbackGroup = new QGroupBox("Wallpaper");
    playbackGroup->setObjectName("settingsGroup");
    auto *playbackLayout = new QFormLayout(playbackGroup);
    playbackLayout->addRow("Backend", m_backendCombo);
    playbackLayout->addRow("Display mode", m_modeCombo);

    auto *engineGroup = new QGroupBox("Wallpaper Engine scenes");
    m_engineSettings = engineGroup;
    engineGroup->setObjectName("settingsGroup");
    auto *engineLayout = new QFormLayout(engineGroup);
    m_engineOutput = new QLineEdit;
    m_engineOutput->setPlaceholderText("Automatic: all Hyprland monitors");
    m_engineOutput->setToolTip("A monitor name such as DP-1 or eDP-1; leave empty for automatic detection");
    m_engineAssets = new QLineEdit;
    m_engineAssets->setPlaceholderText("Automatic: official assets from Steam");
    m_engineFps = new QSpinBox;
    m_engineFps->setRange(1, 240);
    m_engineFps->setSuffix(" FPS");
    m_engineAudio = new QCheckBox("Enable wallpaper audio");
    engineLayout->addRow("Monitor", m_engineOutput);
    engineLayout->addRow("Assets directory", m_engineAssets);
    engineLayout->addRow("Frame limit", m_engineFps);
    engineLayout->addRow(m_engineAudio);
    auto *engineHint = new QLabel("Requires linux-wallpaperengine-git and official Wallpaper Engine assets. "
        "Scene compatibility depends on the engine. Videos use mpvpaper. Center/tile use fill for scenes.");
    engineHint->setWordWrap(true);
    engineHint->setObjectName("mutedLabel");
    engineLayout->addRow(engineHint);
    connect(m_engineOutput, &QLineEdit::editingFinished, this, &MainWindow::onSettingsChanged);
    connect(m_engineAssets, &QLineEdit::editingFinished, this, &MainWindow::onSettingsChanged);
    connect(m_engineFps, &QSpinBox::valueChanged, this, &MainWindow::onSettingsChanged);
    connect(m_engineAudio, &QCheckBox::toggled, this, &MainWindow::onSettingsChanged);

    auto *settingsContent = new QWidget;
    auto *settingsLayout = new QVBoxLayout(settingsContent);
    settingsLayout->setContentsMargins(0, 0, 8, 0);
    settingsLayout->setSpacing(10);
    settingsLayout->addWidget(playbackGroup);
    settingsLayout->addWidget(engineGroup);
    settingsLayout->addWidget(m_settingsPanel);
    settingsLayout->addStretch();
    auto *settingsScroll = new QScrollArea;
    settingsScroll->setWidgetResizable(true);
    settingsScroll->setFrameShape(QFrame::NoFrame);
    settingsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    settingsScroll->setWidget(settingsContent);

    m_pages->addWidget(m_libraryPage);
    m_pages->addWidget(settingsScroll);

    /* Status footer */
    auto *statusBar = new QFrame;
    statusBar->setObjectName("statusBar");
    auto *statusLayout = new QHBoxLayout(statusBar);
    statusLayout->setContentsMargins(0, 5, 0, 0);
    m_statusLabel = new QLabel("Ready");
    m_statusLabel->setObjectName("statusLabel");
    m_statusLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusLayout->addWidget(m_statusLabel, 1);

    centralLayout->addWidget(toolbar);
    centralLayout->addWidget(m_pages, 1);
    centralLayout->addWidget(statusBar);

    rootLayout->addWidget(m_sidebar);
    rootLayout->addWidget(centralColumn, 1);

    auto *searchShortcut = new QShortcut(QKeySequence::Find, this);
    connect(searchShortcut, &QShortcut::activated, this, [this]() {
        if (m_pages->currentIndex() == SettingsPage)
            m_sidebar->setSection(m_currentSection);
        m_filterEdit->setFocus();
        m_filterEdit->selectAll();
    });
    auto *previewShortcut = new QShortcut(QKeySequence("Ctrl+P"), this);
    connect(previewShortcut, &QShortcut::activated, this, [this]() {
        if (m_pages->currentIndex() == LibraryPage)
            m_previewToggleBtn->click();
    });
    refreshFavoriteButton();
}

void MainWindow::loadConfig() {
    config_t cfg;
    const int configResult = config_load(&cfg);

    int backendIndex;
    switch (cfg.backend) {
        case BACKEND_HYPRPAPER: backendIndex = 1; break;
        case BACKEND_MPVPPAPER: backendIndex = 2; break;
        case BACKEND_SWWW: backendIndex = 3; break;
        case BACKEND_WALLPAPER_ENGINE: backendIndex = 4; break;
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
    m_engineOutput->setText(QString::fromUtf8(cfg.engine_output));
    m_engineAssets->setText(QString::fromUtf8(cfg.engine_assets));
    m_engineFps->setValue(cfg.engine_fps);
    m_engineAudio->setChecked(cfg.engine_audio);
    int daemonPid = 0;
    if (daemon_status(&daemonPid) == AP_OK)
        m_settingsPanel->setDaemonRunning(daemonPid > 0);

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
    if (configResult != AP_OK)
        status = QString("Configuration: %1").arg(ap_error_string(static_cast<ap_result>(configResult)));
    updateStatus(status);

    /* loadFolders() already selects the first folder and loads the grid. */
}

bool MainWindow::readUiConfig(config_t *cfg) {
    int rc = config_load(cfg);
    if (rc != AP_OK) {
        updateStatus(QString("Configuration: %1").arg(ap_error_string(static_cast<ap_result>(rc))));
        return false;
    }
    cfg->backend = selectedBackend();
    cfg->wallust_enabled = m_settingsPanel->wallustEnabled();
    cfg->mpvpaper_hwdec = m_settingsPanel->mpvpaperHwdec();
    cfg->daemon_interval = m_settingsPanel->interval();
    cfg->engine_fps = m_engineFps->value();
    cfg->engine_audio = m_engineAudio->isChecked();
    rc |= ap_copy_string(cfg->engine_output, sizeof(cfg->engine_output), m_engineOutput->text().trimmed().toUtf8().constData());
    rc |= ap_copy_string(cfg->engine_assets, sizeof(cfg->engine_assets), m_engineAssets->text().toUtf8().constData());
    rc |= ap_copy_string(cfg->mode, sizeof(cfg->mode), m_modeCombo->currentText().toUtf8().constData());
    rc |= ap_copy_string(cfg->wallust_hook, sizeof(cfg->wallust_hook), m_settingsPanel->wallustHook().toUtf8().constData());
    rc |= ap_copy_string(cfg->cache_quality, sizeof(cfg->cache_quality), m_settingsPanel->cacheQuality().toUtf8().constData());
    rc |= ap_copy_string(cfg->mpvpaper_profile, sizeof(cfg->mpvpaper_profile), m_settingsPanel->mpvpaperProfile().toUtf8().constData());
    cfg->folder_count = 0;
    for (int i = 0; i < m_sidebar->folderCount(); ++i)
        rc |= config_add_folder(cfg, m_sidebar->folderAt(i).toUtf8().constData());
    if (rc || config_validate(cfg) != AP_OK) {
        updateStatus("Invalid settings or a path is too long");
        return false;
    }
    return true;
}

void MainWindow::saveCurrentConfig() {
    if (m_loadingConfig) return;
    config_t cfg;
    if (!readUiConfig(&cfg)) return;
    const int rc = config_save(&cfg);
    if (rc != AP_OK) updateStatus(ap_error_string(static_cast<ap_result>(rc)));
}

void MainWindow::loadFolders() {
    config_t cfg;
    config_load(&cfg);

    QStringList folders;
    if (cfg.folder_count == 0) {
        QString defaultDir = defaultWallpaperDir();
        if (!QDir(defaultDir).exists()) {
            ap_mkdirs(defaultDir.toUtf8().constData());
        }
        folders.append(defaultDir);
    } else {
        for (int i = 0; i < cfg.folder_count; i++) {
            folders.append(QString::fromUtf8(cfg.folders[i]));
        }
    }

    m_sidebar->setFolders(folders);
}

void MainWindow::loadFavorites() {
    ap_result rc;
    const QStringList paths = historyPaths(AP_FAVORITES, &rc);
    if (rc == AP_OK) m_favoritePaths = paths;
    else updateStatus(ap_error_string(rc));
}

void MainWindow::loadRecent() {
    ap_result rc;
    const QStringList paths = historyPaths(AP_RECENT, &rc);
    if (rc == AP_OK) m_recentPaths = paths;
    else updateStatus(ap_error_string(rc));
}

bool MainWindow::isFavorite(const QString &path) const {
    return m_favoritePaths.contains(path);
}

void MainWindow::refreshFavoriteButton() {
    QString path = m_grid->selectedPath();
    bool hasSelection = !path.isEmpty();
    m_applyBtn->setEnabled(hasSelection && !m_applyThread);
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
    const bool settings = section == NavSidebar::Settings;
    m_filterEdit->setVisible(!settings);
    m_countLabel->setVisible(!settings);
    if (section == NavSidebar::Settings) {
        m_sectionTitle->setText("Settings");
        m_pages->setCurrentIndex(SettingsPage);
    } else {
        m_pages->setCurrentIndex(LibraryPage);
        setLibrarySection(section);
    }
}

void MainWindow::setLibrarySection(NavSidebar::Section section) {
    m_currentSection = section;

    switch (section) {
        case NavSidebar::Home:
            m_sectionTitle->setText("Library");
            if (!m_currentFolder.isEmpty() && QDir(m_currentFolder).exists()) {
                m_grid->loadFromFolder(m_currentFolder);
            } else if (!m_sidebar->selectedFolder().isEmpty()) {
                onFolderSelected(m_sidebar->selectedFolder());
            } else {
                m_grid->setWallpapers({});
            }
            break;
        case NavSidebar::Favorites:
            m_sectionTitle->setText("Favorites");
            loadFavorites();
            m_grid->setWallpapers(m_favoritePaths);
            break;
        case NavSidebar::Recent:
            m_sectionTitle->setText("Recent");
            loadRecent();
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
    Q_UNUSED(folder);
    saveCurrentConfig();
}

void MainWindow::onFolderRemoved(int row) {
    Q_UNUSED(row);
    if (m_sidebar->selectedFolder().isEmpty()) {
        m_currentFolder.clear();
        if (m_currentSection == NavSidebar::Home) m_grid->setWallpapers({});
    }
    saveCurrentConfig();
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
        m_countLabel->setText(QString("%1 wallpapers").arg(total));
    } else {
        m_countLabel->setText(QString("%1 of %2 wallpapers").arg(visible).arg(total));
    }
    if (m_grid->selectedPath().isEmpty())
        m_preview->clear();
    refreshFavoriteButton();
}

void MainWindow::onFilterTextChanged(const QString &text) {
    m_grid->setFilter(text);
}

void MainWindow::onToggleFavorite() {
    QString path = m_grid->selectedPath();
    if (path.isEmpty()) return;

    const ap_result rc = ap_favorite_toggle(path.toUtf8().constData(), nullptr);
    if (rc != AP_OK) { updateStatus(ap_error_string(rc)); return; }
    loadFavorites();
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
    }
}

void MainWindow::onClear() {
    if (m_applyThread) { updateStatus("A wallpaper is still being applied"); return; }
    const ap_result rc = ap_wallpaper_clear();
    updateStatus(rc == AP_OK ? "Wallpaper cleared" : ap_error_string(rc));
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
    saveCurrentConfig();
}

void MainWindow::onModeChanged(int index) {
    (void)index;
    saveCurrentConfig();
}

void MainWindow::onSettingsChanged() {
    saveCurrentConfig();
}

void MainWindow::onDaemonRequested(bool start) {
    int rc;
    if (start) {
        config_t cfg;
        if (!readUiConfig(&cfg)) {
            m_settingsPanel->setDaemonRunning(false);
            return;
        }
        rc = daemon_start(m_currentFolder.toUtf8().constData(), &cfg);
    } else {
        rc = daemon_stop();
    }
    int pid = 0;
    daemon_status(&pid);
    m_settingsPanel->setDaemonRunning(pid > 0);
    updateStatus(rc == AP_OK ? (start ? "Daemon started" : "Daemon stopped")
                            : ap_error_string(static_cast<ap_result>(rc)));
}

backend_t MainWindow::selectedBackend() const {
    int idx = m_backendCombo->currentIndex();
    if (idx == 1) return BACKEND_HYPRPAPER;
    if (idx == 2) return BACKEND_MPVPPAPER;
    if (idx == 3) return BACKEND_SWWW;
    if (idx == 4) return BACKEND_WALLPAPER_ENGINE;
    return BACKEND_SWAYBG;
}

void MainWindow::applySelectedImage(const QString &path) {
    if (m_applyThread) { updateStatus("A wallpaper is still being applied"); return; }
    struct ApplyTask {
        config_t options;
        QByteArray path;
        ap_result status = AP_OK;
        ap_apply_result result = {};
    };
    auto task = std::make_shared<ApplyTask>();
    if (!readUiConfig(&task->options)) return;
    task->path = path.toUtf8();
    /* Qt only marshals work/results. All backend, cache, history and theme
     * decisions are made by the same C function used by the CLI and daemon. */
    m_applyThread = QThread::create([task]() {
        ap_process_set_cancel_check([](void *) -> int {
            return QThread::currentThread()->isInterruptionRequested();
        }, nullptr);
        task->status = ap_wallpaper_apply(task->path.constData(), &task->options, 0, &task->result);
        ap_process_set_cancel_check(nullptr, nullptr);
    });
    m_applyBtn->setEnabled(false);
    m_settingsPanel->setEnabled(false);
    m_engineSettings->setEnabled(false);
    m_backendCombo->setEnabled(false);
    m_modeCombo->setEnabled(false);
    updateStatus(QString("Applying: %1…").arg(QFileInfo(path).fileName()));
    connect(m_applyThread, &QThread::finished, this, [this, task, path]() {
        m_applyThread->deleteLater();
        m_applyThread = nullptr;
        m_settingsPanel->setEnabled(true);
        m_engineSettings->setEnabled(true);
        m_backendCombo->setEnabled(true);
        m_modeCombo->setEnabled(true);
        refreshFavoriteButton();
        if (task->status != AP_OK) { updateStatus(ap_error_string(task->status)); return; }
        loadRecent();
        QString message = QString("Applied: %1").arg(QFileInfo(path).fileName());
        if (task->result.persistence != AP_OK)
            message += QString(" | History/config: %1").arg(ap_error_string(task->result.persistence));
        if (task->result.theme != AP_OK)
            message += QString(" | Theme/hook: %1").arg(ap_error_string(task->result.theme));
        updateStatus(message);
    });
    m_applyThread->start();
}

void MainWindow::updateStatus(const QString &msg) {
    m_statusLabel->setText(msg);
    m_statusLabel->setToolTip(msg);
}
