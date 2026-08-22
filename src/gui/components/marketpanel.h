/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef MARKETPANEL_H
#define MARKETPANEL_H

#include "../market/marketitem.h"

#include <QFrame>

QT_BEGIN_NAMESPACE
class QComboBox;
class QLineEdit;
class QPushButton;
class QLabel;
class QProgressBar;
QT_END_NAMESPACE

class MarketGrid;
class MarketPreviewPanel;
class MarketService;

class MarketPanel : public QFrame {
    Q_OBJECT

public:
    explicit MarketPanel(QWidget *parent = nullptr);

    void setDownloadDir(const QString &dir);
    void setWallhavenApiKey(const QString &apiKey);
    void setWallhavenPurity(const QString &purity);

    QString currentPurity() const;

signals:
    void statusMessage(const QString &msg);
    void wallpaperDownloaded(const QString &filePath, const MarketItem &item, bool applyAfter);

private slots:
    void onSearch();
    void onLoadMore();
    void onSourceChanged(int index);
    void onItemSelected(const MarketItem &item);
    void onItemDoubleClicked(const MarketItem &item);
    void onThumbnailNeeded(const QString &url);
    void onThumbnailLoaded(const QString &url, const QPixmap &pixmap);
    void onResultsReady(const QList<MarketItem> &items, int totalPages);
    void onSearchError(const QString &message);
    void onDownloadRequested(bool applyAfter);
    void onDownloadStarted(const QString &id);
    void onDownloadProgress(const QString &id, qint64 received, qint64 total);
    void onDownloadFinished(const QString &id, const QString &filePath, const MarketItem &item, bool applyAfter);
    void onDownloadError(const QString &id, const QString &message);

private:
    void setupUi();
    void performSearch(int page);
    void updateLoadMore();

    MarketService *m_service;
    MarketGrid *m_grid;
    MarketPreviewPanel *m_preview;

    QLineEdit *m_searchEdit;
    QComboBox *m_sourceCombo;
    QComboBox *m_purityCombo;
    QPushButton *m_searchButton;
    QPushButton *m_loadMoreButton;
    QLabel *m_statusLabel;
    QProgressBar *m_progressBar;

    QString m_downloadDir;
    QString m_currentQuery;
    QString m_currentSource;
    int m_currentPage = 1;
    int m_totalPages = 1;
    bool m_loading = false;
};

#endif // MARKETPANEL_H
