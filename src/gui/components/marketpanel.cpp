/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "marketpanel.h"

#include "marketgrid.h"
#include "marketpreviewpanel.h"
#include "../market/marketservice.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QVBoxLayout>

namespace {

constexpr int SEARCH_DEBOUNCE_MS = 400;

} // namespace

MarketPanel::MarketPanel(QWidget *parent)
    : QFrame(parent) {
    setupUi();
    restoreDefaultFilters();
}

void MarketPanel::setupUi() {
    setObjectName(QStringLiteral("marketPanel"));

    m_service = new MarketService(this);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    /* Controls bar */
    auto *topBar = new QFrame;
    topBar->setObjectName(QStringLiteral("topBar"));
    auto *topBarLayout = new QVBoxLayout(topBar);
    topBarLayout->setContentsMargins(10, 8, 10, 8);
    topBarLayout->setSpacing(10);

    /* Row 1: search + source + quick actions */
    auto *row1 = new QHBoxLayout;
    row1->setSpacing(10);

    m_searchEdit = new QLineEdit;
    m_searchEdit->setObjectName(QStringLiteral("searchEdit"));
    m_searchEdit->setPlaceholderText(QStringLiteral("Type to search wallpapers..."));
    m_searchEdit->setClearButtonEnabled(true);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &MarketPanel::onSearch);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &MarketPanel::onSearchTextChanged);

    auto *sourceLabel = new QLabel(QStringLiteral("Source"));
    sourceLabel->setObjectName(QStringLiteral("mutedLabel"));
    m_sourceCombo = new QComboBox;
    m_sourceCombo->addItem(QStringLiteral("All"), QStringLiteral("all"));
    m_sourceCombo->addItem(QStringLiteral("Wallhaven"), QStringLiteral("wallhaven"));
    m_sourceCombo->addItem(QStringLiteral("MoeWalls"), QStringLiteral("moewalls"));
    connect(m_sourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MarketPanel::onSourceChanged);

    m_searchButton = new QPushButton(QStringLiteral("Search"));
    m_searchButton->setToolTip(QStringLiteral("Search now"));
    connect(m_searchButton, &QPushButton::clicked, this, &MarketPanel::onSearch);

    m_resetButton = new QPushButton(QStringLiteral("Reset"));
    m_resetButton->setObjectName(QStringLiteral("secondaryButton"));
    m_resetButton->setToolTip(QStringLiteral("Reset filters"));
    connect(m_resetButton, &QPushButton::clicked, this, &MarketPanel::onResetFilters);

    m_loadMoreButton = new QPushButton(QStringLiteral("Load more"));
    m_loadMoreButton->setObjectName(QStringLiteral("secondaryButton"));
    m_loadMoreButton->setVisible(false);
    connect(m_loadMoreButton, &QPushButton::clicked, this, &MarketPanel::onLoadMore);

    row1->addWidget(new QLabel(QStringLiteral("Search")), 0);
    row1->addWidget(m_searchEdit, 1);
    row1->addWidget(sourceLabel, 0);
    row1->addWidget(m_sourceCombo);
    row1->addWidget(m_searchButton);
    row1->addWidget(m_resetButton);
    row1->addWidget(m_loadMoreButton);

    /* Row 2: filters */
    auto *row2 = new QHBoxLayout;
    row2->setSpacing(10);

    m_sortCombo = new QComboBox;
    m_sortCombo->addItem(QStringLiteral("Latest"), QStringLiteral("latest"));
    m_sortCombo->addItem(QStringLiteral("Best match"), QStringLiteral("relevance"));
    m_sortCombo->addItem(QStringLiteral("Random"), QStringLiteral("random"));
    m_sortCombo->addItem(QStringLiteral("Most viewed"), QStringLiteral("views"));
    m_sortCombo->addItem(QStringLiteral("Most favorited"), QStringLiteral("favorites"));
    m_sortCombo->addItem(QStringLiteral("Top list"), QStringLiteral("toplist"));
    connect(m_sortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MarketPanel::onFiltersChanged);

    m_topRangeCombo = new QComboBox;
    m_topRangeCombo->addItem(QStringLiteral("1 day"), QStringLiteral("1d"));
    m_topRangeCombo->addItem(QStringLiteral("3 days"), QStringLiteral("3d"));
    m_topRangeCombo->addItem(QStringLiteral("1 week"), QStringLiteral("1w"));
    m_topRangeCombo->addItem(QStringLiteral("1 month"), QStringLiteral("1M"));
    m_topRangeCombo->addItem(QStringLiteral("3 months"), QStringLiteral("3M"));
    m_topRangeCombo->addItem(QStringLiteral("6 months"), QStringLiteral("6M"));
    m_topRangeCombo->addItem(QStringLiteral("1 year"), QStringLiteral("1y"));
    m_topRangeCombo->setEnabled(false);
    connect(m_topRangeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MarketPanel::onFiltersChanged);

    m_purityCombo = new QComboBox;
    m_purityCombo->addItem(QStringLiteral("SFW"), QStringLiteral("sfw"));
    m_purityCombo->addItem(QStringLiteral("Sketchy"), QStringLiteral("sketchy"));
    m_purityCombo->addItem(QStringLiteral("NSFW"), QStringLiteral("nsfw"));
    m_purityCombo->addItem(QStringLiteral("All"), QStringLiteral("all"));
    connect(m_purityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MarketPanel::onFiltersChanged);

    m_resolutionCombo = new QComboBox;
    m_resolutionCombo->setEditable(true);
    m_resolutionCombo->setInsertPolicy(QComboBox::NoInsert);
    m_resolutionCombo->addItem(QStringLiteral("Any resolution"), QString());
    m_resolutionCombo->addItem(QStringLiteral("1920x1080"), QStringLiteral("1920x1080"));
    m_resolutionCombo->addItem(QStringLiteral("2560x1440"), QStringLiteral("2560x1440"));
    m_resolutionCombo->addItem(QStringLiteral("3840x2160"), QStringLiteral("3840x2160"));
    m_resolutionCombo->addItem(QStringLiteral("1280x720"), QStringLiteral("1280x720"));
    m_resolutionCombo->addItem(QStringLiteral("3440x1440"), QStringLiteral("3440x1440"));
    m_resolutionCombo->addItem(QStringLiteral("2560x1080"), QStringLiteral("2560x1080"));
    m_resolutionCombo->setCurrentIndex(0);
    connect(m_resolutionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MarketPanel::onFiltersChanged);
    connect(m_resolutionCombo->lineEdit(), &QLineEdit::editingFinished,
            this, &MarketPanel::onFiltersChanged);

    m_ratioCombo = new QComboBox;
    m_ratioCombo->addItem(QStringLiteral("Any ratio"), QString());
    m_ratioCombo->addItem(QStringLiteral("16:9"), QStringLiteral("16x9"));
    m_ratioCombo->addItem(QStringLiteral("21:9"), QStringLiteral("21x9"));
    m_ratioCombo->addItem(QStringLiteral("4:3"), QStringLiteral("4x3"));
    m_ratioCombo->addItem(QStringLiteral("3:2"), QStringLiteral("3x2"));
    m_ratioCombo->addItem(QStringLiteral("1:1"), QStringLiteral("1x1"));
    m_ratioCombo->addItem(QStringLiteral("9:16"), QStringLiteral("9x16"));
    m_ratioCombo->addItem(QStringLiteral("16:10"), QStringLiteral("16x10"));
    connect(m_ratioCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MarketPanel::onFiltersChanged);

    row2->addWidget(new QLabel(QStringLiteral("Sort")));
    row2->addWidget(m_sortCombo);
    row2->addWidget(new QLabel(QStringLiteral("Top range")));
    row2->addWidget(m_topRangeCombo);
    row2->addWidget(new QLabel(QStringLiteral("Purity")));
    row2->addWidget(m_purityCombo);
    row2->addWidget(new QLabel(QStringLiteral("Resolution")));
    row2->addWidget(m_resolutionCombo);
    row2->addWidget(new QLabel(QStringLiteral("Ratio")));
    row2->addWidget(m_ratioCombo);
    row2->addStretch(1);

    topBarLayout->addLayout(row1);
    topBarLayout->addLayout(row2);

    /* Progress / status */
    auto *statusLayout = new QHBoxLayout;
    statusLayout->setSpacing(10);
    m_statusLabel = new QLabel(QStringLiteral("Ready"));
    m_statusLabel->setObjectName(QStringLiteral("mutedLabel"));
    m_progressBar = new QProgressBar;
    m_progressBar->setRange(0, 0);
    m_progressBar->setVisible(false);
    statusLayout->addWidget(m_statusLabel, 1);
    statusLayout->addWidget(m_progressBar, 1);

    /* Content: grid + preview */
    auto *content = new QHBoxLayout;
    content->setSpacing(12);

    m_grid = new MarketGrid;
    connect(m_grid, &MarketGrid::itemSelected, this, &MarketPanel::onItemSelected);
    connect(m_grid, &MarketGrid::itemDoubleClicked, this, &MarketPanel::onItemDoubleClicked);
    connect(m_grid, &MarketGrid::thumbnailNeeded, this, &MarketPanel::onThumbnailNeeded);

    m_preview = new MarketPreviewPanel;
    connect(m_preview, &MarketPreviewPanel::downloadRequested,
            this, [this]() { onDownloadRequested(false); });
    connect(m_preview, &MarketPreviewPanel::downloadAndApplyRequested,
            this, [this]() { onDownloadRequested(true); });

    content->addWidget(m_grid, 1);
    content->addWidget(m_preview);

    layout->addWidget(topBar);
    layout->addLayout(statusLayout);
    layout->addLayout(content, 1);

    /* Service connections */
    connect(m_service, &MarketService::searchStarted, this, [this]() {
        m_loading = true;
        m_progressBar->setRange(0, 0);
        m_progressBar->setVisible(true);
        m_searchButton->setEnabled(false);
        m_statusLabel->setText(QStringLiteral("Searching..."));
        QApplication::setOverrideCursor(Qt::WaitCursor);
    });
    connect(m_service, &MarketService::searchFinished, this, [this]() {
        m_loading = false;
        m_progressBar->setVisible(false);
        m_searchButton->setEnabled(true);
        QApplication::restoreOverrideCursor();
    });
    connect(m_service, &MarketService::resultsReady,
            this, &MarketPanel::onResultsReady);
    connect(m_service, &MarketService::searchError,
            this, &MarketPanel::onSearchError);
    connect(m_service, &MarketService::thumbnailLoaded,
            this, &MarketPanel::onThumbnailLoaded);
    connect(m_service, &MarketService::thumbnailError,
            this, [](const QString &) { /* ignored */ });
    connect(m_service, &MarketService::downloadStarted,
            this, &MarketPanel::onDownloadStarted);
    connect(m_service, &MarketService::downloadProgress,
            this, &MarketPanel::onDownloadProgress);
    connect(m_service, &MarketService::downloadFinished,
            this, &MarketPanel::onDownloadFinished);
    connect(m_service, &MarketService::downloadError,
            this, &MarketPanel::onDownloadError);

    m_searchDebounceTimer = new QTimer(this);
    m_searchDebounceTimer->setSingleShot(true);
    connect(m_searchDebounceTimer, &QTimer::timeout, this, &MarketPanel::onSearch);
}

void MarketPanel::setDownloadDir(const QString &dir) {
    m_downloadDir = dir;
}

void MarketPanel::setWallhavenApiKey(const QString &apiKey) {
    m_service->setWallhavenApiKey(apiKey);
}

void MarketPanel::setWallhavenPurity(const QString &purity) {
    int idx = m_purityCombo->findData(purity);
    if (idx >= 0) m_purityCombo->setCurrentIndex(idx);
    m_service->setWallhavenPurity(purity);
}

QString MarketPanel::currentPurity() const {
    return m_purityCombo->currentData().toString();
}

void MarketPanel::onSourceChanged(int index) {
    Q_UNUSED(index)
    QString source = m_sourceCombo->currentData().toString();
    bool wallhavenEnabled = (source == QLatin1String("all") || source == QLatin1String("wallhaven"));
    m_purityCombo->setEnabled(wallhavenEnabled);
    m_sortCombo->setEnabled(wallhavenEnabled);
    m_topRangeCombo->setEnabled(wallhavenEnabled && m_sortCombo->currentData().toString() == QLatin1String("toplist"));
    m_resolutionCombo->setEnabled(wallhavenEnabled);
    m_ratioCombo->setEnabled(wallhavenEnabled);

    if (!m_searchEdit->text().isEmpty())
        performSearch(1);
}

void MarketPanel::onSearchTextChanged() {
    m_searchDebounceTimer->stop();
    m_searchDebounceTimer->start(SEARCH_DEBOUNCE_MS);
}

void MarketPanel::onFiltersChanged() {
    QString sort = m_sortCombo->currentData().toString();
    bool isToplist = (sort == QLatin1String("toplist"));
    QString source = m_sourceCombo->currentData().toString();
    bool wallhavenEnabled = (source == QLatin1String("all") || source == QLatin1String("wallhaven"));
    m_topRangeCombo->setEnabled(wallhavenEnabled && isToplist);

    m_searchDebounceTimer->stop();
    m_searchDebounceTimer->start(SEARCH_DEBOUNCE_MS);
}

void MarketPanel::onSearch() {
    performSearch(1);
}

void MarketPanel::onLoadMore() {
    if (m_currentPage < m_totalPages)
        performSearch(m_currentPage + 1);
}

void MarketPanel::onResetFilters() {
    restoreDefaultFilters();
    performSearch(1);
}

void MarketPanel::restoreDefaultFilters() {
    m_searchEdit->clear();
    m_sourceCombo->setCurrentIndex(0);
    m_sortCombo->setCurrentIndex(0);
    m_topRangeCombo->setCurrentIndex(3); // 1 month
    m_purityCombo->setCurrentIndex(0);
    m_resolutionCombo->setCurrentIndex(0);
    m_ratioCombo->setCurrentIndex(0);
    onSourceChanged(0);
}

MarketSearchOptions MarketPanel::buildOptions() {
    MarketSearchOptions options;
    options.query = m_searchEdit->text().trimmed();
    options.source = m_sourceCombo->currentData().toString();
    options.page = m_currentPage;
    options.sorting = m_sortCombo->currentData().toString();
    options.topRange = m_topRangeCombo->currentData().toString();

    if (m_resolutionCombo->currentData().isValid())
        options.resolution = m_resolutionCombo->currentData().toString();
    if (options.resolution.isEmpty() && m_resolutionCombo->isEditable())
        options.resolution = m_resolutionCombo->currentText().trimmed();
    // Keep only valid-looking resolution text
    static const QRegularExpression resolutionRe(QStringLiteral("^\\d+x\\d+$"));
    if (!resolutionRe.match(options.resolution).hasMatch())
        options.resolution.clear();

    options.ratio = m_ratioCombo->currentData().toString();
    return options;
}

void MarketPanel::performSearch(int page) {
    MarketSearchOptions options = buildOptions();
    m_currentQuery = options.query;
    m_currentSource = options.source;
    m_currentPage = page;
    options.page = page;

    m_service->setWallhavenPurity(m_purityCombo->currentData().toString());

    if (page == 1)
        m_grid->clear();

    m_service->search(options);
}

void MarketPanel::onItemSelected(const MarketItem &item) {
    m_preview->setItem(item);
    if (!item.thumbnailUrl.isEmpty())
        m_service->loadThumbnail(item.thumbnailUrl);
}

void MarketPanel::onItemDoubleClicked(const MarketItem &item) {
    onItemSelected(item);
    onDownloadRequested(true);
}

void MarketPanel::onThumbnailNeeded(const QString &url) {
    m_service->loadThumbnail(url);
}

void MarketPanel::onThumbnailLoaded(const QString &url, const QPixmap &pixmap) {
    m_grid->setThumbnail(url, pixmap);
    MarketItem selected = m_grid->selectedItem();
    if (selected.thumbnailUrl == url)
        m_preview->setThumbnail(pixmap);
}

void MarketPanel::onResultsReady(const QList<MarketItem> &items, int totalPages) {
    m_totalPages = totalPages;
    if (m_currentPage == 1)
        m_grid->setItems(items);
    else
        m_grid->appendItems(items);

    m_statusLabel->setText(QStringLiteral("%1 results, page %2/%3")
                               .arg(m_grid->count())
                               .arg(m_currentPage)
                               .arg(m_totalPages));
    updateLoadMore();
}

void MarketPanel::onSearchError(const QString &message) {
    m_statusLabel->setText(message);
    emit statusMessage(message);
}

void MarketPanel::updateLoadMore() {
    m_loadMoreButton->setVisible(m_totalPages > 1);
    m_loadMoreButton->setEnabled(m_currentPage < m_totalPages);
}

void MarketPanel::onDownloadRequested(bool applyAfter) {
    MarketItem item = m_grid->selectedItem();
    if (item.id.isEmpty()) {
        m_statusLabel->setText(QStringLiteral("Select a wallpaper first"));
        return;
    }
    if (m_downloadDir.isEmpty()) {
        m_downloadDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    }
    QDir().mkpath(m_downloadDir);
    m_service->download(item, m_downloadDir, applyAfter);
}

void MarketPanel::onDownloadStarted(const QString &id) {
    Q_UNUSED(id)
    m_preview->setDownloadActive(true);
    m_statusLabel->setText(QStringLiteral("Downloading..."));
}

void MarketPanel::onDownloadProgress(const QString &id, qint64 received, qint64 total) {
    Q_UNUSED(id)
    m_preview->setProgress(received, total);
    if (total > 0) {
        int pct = static_cast<int>((received * 100) / total);
        m_statusLabel->setText(QStringLiteral("Downloading %1%").arg(pct));
    }
}

void MarketPanel::onDownloadFinished(const QString &id, const QString &filePath,
                                     const MarketItem &item, bool applyAfter) {
    Q_UNUSED(id)
    m_preview->setDownloadActive(false);
    m_statusLabel->setText(QStringLiteral("Saved: %1").arg(filePath));
    emit wallpaperDownloaded(filePath, item, applyAfter);
}

void MarketPanel::onDownloadError(const QString &id, const QString &message) {
    Q_UNUSED(id)
    m_preview->setDownloadActive(false);
    m_statusLabel->setText(message);
    QMessageBox::warning(this, QStringLiteral("Download error"), message);
}
