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
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QVBoxLayout>

MarketPanel::MarketPanel(QWidget *parent)
    : QFrame(parent) {
    setupUi();
}

void MarketPanel::setupUi() {
    setObjectName(QStringLiteral("marketPanel"));

    m_service = new MarketService(this);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);

    /* Top controls */
    auto *topBar = new QFrame;
    topBar->setObjectName(QStringLiteral("topBar"));
    auto *topBarLayout = new QHBoxLayout(topBar);
    topBarLayout->setContentsMargins(10, 8, 10, 8);
    topBarLayout->setSpacing(10);

    m_searchEdit = new QLineEdit;
    m_searchEdit->setObjectName(QStringLiteral("searchEdit"));
    m_searchEdit->setPlaceholderText(QStringLiteral("Search wallpapers..."));
    m_searchEdit->setClearButtonEnabled(true);
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &MarketPanel::onSearch);

    topBarLayout->addWidget(new QLabel(QStringLiteral("Search:")), 0);
    topBarLayout->addWidget(m_searchEdit, 1);

    auto *sourceLabel = new QLabel(QStringLiteral("Source:"));
    sourceLabel->setObjectName(QStringLiteral("mutedLabel"));
    m_sourceCombo = new QComboBox;
    m_sourceCombo->addItem(QStringLiteral("All"), QStringLiteral("all"));
    m_sourceCombo->addItem(QStringLiteral("Wallhaven"), QStringLiteral("wallhaven"));
    m_sourceCombo->addItem(QStringLiteral("MoeWalls"), QStringLiteral("moewalls"));
    connect(m_sourceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MarketPanel::onSourceChanged);

    auto *purityLabel = new QLabel(QStringLiteral("Purity:"));
    purityLabel->setObjectName(QStringLiteral("mutedLabel"));
    m_purityCombo = new QComboBox;
    m_purityCombo->addItem(QStringLiteral("SFW"), QStringLiteral("sfw"));
    m_purityCombo->addItem(QStringLiteral("Sketchy"), QStringLiteral("sketchy"));
    m_purityCombo->addItem(QStringLiteral("NSFW"), QStringLiteral("nsfw"));
    m_purityCombo->addItem(QStringLiteral("All"), QStringLiteral("all"));

    m_searchButton = new QPushButton(QStringLiteral("Search"));
    connect(m_searchButton, &QPushButton::clicked, this, &MarketPanel::onSearch);

    topBarLayout->addWidget(sourceLabel);
    topBarLayout->addWidget(m_sourceCombo);
    topBarLayout->addWidget(purityLabel);
    topBarLayout->addWidget(m_purityCombo);
    topBarLayout->addWidget(m_searchButton);

    m_loadMoreButton = new QPushButton(QStringLiteral("Load more"));
    m_loadMoreButton->setObjectName(QStringLiteral("secondaryButton"));
    m_loadMoreButton->setVisible(false);
    connect(m_loadMoreButton, &QPushButton::clicked, this, &MarketPanel::onLoadMore);
    topBarLayout->addWidget(m_loadMoreButton);

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
}

void MarketPanel::onSearch() {
    performSearch(1);
}

void MarketPanel::onLoadMore() {
    if (m_currentPage < m_totalPages)
        performSearch(m_currentPage + 1);
}

void MarketPanel::performSearch(int page) {
    QString query = m_searchEdit->text();
    QString source = m_sourceCombo->currentData().toString();
    QString purity = m_purityCombo->currentData().toString();

    m_service->setWallhavenPurity(purity);
    m_currentQuery = query;
    m_currentSource = source;
    m_currentPage = page;

    if (page == 1)
        m_grid->clear();

    m_service->search(query, source, page);
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

    m_statusLabel->setText(QStringLiteral("%1 results").arg(m_grid->count()));
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
