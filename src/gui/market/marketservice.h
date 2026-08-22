/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef MARKETSERVICE_H
#define MARKETSERVICE_H

#include "marketitem.h"
#include "marketsearchoptions.h"

#include <QFile>
#include <QHash>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPixmap>

class WallhavenProvider;
class MoeWallsProvider;

struct DownloadJob {
    QNetworkReply *reply = nullptr;
    QFile *file = nullptr;
    QString id;
    QString filePath;
};

class MarketService : public QObject {
    Q_OBJECT

public:
    explicit MarketService(QObject *parent = nullptr);
    ~MarketService();

    void setWallhavenApiKey(const QString &apiKey);
    void setWallhavenPurity(const QString &purity);

    void search(const MarketSearchOptions &options);
    void loadThumbnail(const QString &url);
    void download(const MarketItem &item, const QString &downloadDir, bool applyAfter = false);
    void cancelDownload(const QString &id);

signals:
    void searchStarted();
    void searchFinished();
    void resultsReady(const QList<MarketItem> &items, int totalPages);
    void searchError(const QString &message);

    void thumbnailLoaded(const QString &url, const QPixmap &pixmap);
    void thumbnailError(const QString &url);

    void downloadStarted(const QString &id);
    void downloadProgress(const QString &id, qint64 received, qint64 total);
    void downloadFinished(const QString &id, const QString &filePath, const MarketItem &item, bool applyAfter);
    void downloadError(const QString &id, const QString &message);

private slots:
    void onThumbnailFinished();
    void onDownloadProgress(qint64 received, qint64 total);
    void onDownloadFinished();
    void onResolvePageFinished();

private:
    void providerFinished();

    QNetworkRequest makeRequest(const QUrl &url) const;
    QString makeUniqueFileName(const MarketItem &item, const QString &downloadDir);
    void resolveMoeWallsDownload(const MarketItem &item, const QString &downloadDir, bool applyAfter);
    void startFileDownload(const MarketItem &item, const QString &directUrl,
                           const QString &downloadDir, bool applyAfter);
    void cleanupJob(DownloadJob *job);
    static QString cacheDirectory();

    QNetworkAccessManager *m_nam;
    WallhavenProvider *m_wallhaven;
    MoeWallsProvider *m_moewalls;

    int m_pendingProviders = 0;
    int m_totalPagesWallhaven = 1;
    int m_totalPagesMoeWalls = 1;
    QList<MarketItem> m_collectedResults;
    QStringList m_errors;
    QHash<QString, QPixmap> m_thumbnailCache;
    QSet<QString> m_pendingThumbnails;
    QHash<QString, DownloadJob *> m_downloadJobs;
};

#endif // MARKETSERVICE_H
