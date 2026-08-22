/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "marketservice.h"

#include "moewallsprovider.h"
#include "wallhavenprovider.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPixmap>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QUrlQuery>

namespace {

constexpr char USER_AGENT[] = "archpaper/1.0 (Qt6; Linux; Market)";

QString hashForUrl(const QString &url) {
    return QString::fromLatin1(
        QCryptographicHash::hash(url.toUtf8(), QCryptographicHash::Sha1).toHex());
}

QString parseContentDispositionFileName(const QByteArray &value) {
    static const QRegularExpression re(QStringLiteral("filename\\*=UTF-8''([^;]+)|filename=\"([^\"]+)\""));
    QRegularExpressionMatch m = re.match(QString::fromUtf8(value));
    if (m.captured(1).isEmpty()) return m.captured(2);
    return QUrl::fromPercentEncoding(m.captured(1).toUtf8());
}

QString sanitizeFileName(const QString &name) {
    QString s = name;
    s.replace(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*]")), QStringLiteral("_"));
    return s.trimmed();
}

QString uniqueFilePath(const QString &dir, const QString &fileName) {
    QString base = fileName;
    QString ext;
    int dot = base.lastIndexOf('.');
    if (dot > 0) {
        ext = base.mid(dot);
        base = base.left(dot);
    }
    QString path = dir + QDir::separator() + base + ext;
    if (!QFile::exists(path)) return path;

    for (int i = 1; i < 10000; ++i) {
        path = dir + QDir::separator() + base + QStringLiteral("_%1").arg(i) + ext;
        if (!QFile::exists(path)) return path;
    }
    return path;
}

} // namespace

MarketService::MarketService(QObject *parent)
    : QObject(parent), m_nam(new QNetworkAccessManager(this)),
      m_wallhaven(new WallhavenProvider(m_nam, this)),
      m_moewalls(new MoeWallsProvider(m_nam, this)) {

    qRegisterMetaType<MarketItem>();


    connect(m_wallhaven, &MarketProvider::resultsReady,
            this, [this](const QList<MarketItem> &items, int totalPages) {
                m_collectedResults.append(items);
                m_totalPagesWallhaven = totalPages;
                providerFinished();
            });
    connect(m_wallhaven, &MarketProvider::error,
            this, [this](const QString &msg) {
                m_errors << msg;
                providerFinished();
            });
    connect(m_moewalls, &MarketProvider::resultsReady,
            this, [this](const QList<MarketItem> &items, int totalPages) {
                m_collectedResults.append(items);
                m_totalPagesMoeWalls = totalPages;
                providerFinished();
            });
    connect(m_moewalls, &MarketProvider::error,
            this, [this](const QString &msg) {
                m_errors << msg;
                providerFinished();
            });
}

void MarketService::providerFinished() {
    if (--m_pendingProviders > 0) return;

    emit searchFinished();

    int totalPages = qMax(1, qMax(m_totalPagesWallhaven, m_totalPagesMoeWalls));
    emit resultsReady(m_collectedResults, totalPages);

    if (m_collectedResults.isEmpty() && !m_errors.isEmpty())
        emit searchError(m_errors.join(QStringLiteral("\n")));
}

MarketService::~MarketService() {
    for (auto it = m_downloadJobs.begin(); it != m_downloadJobs.end(); ++it) {
        DownloadJob *job = it.value();
        if (job->reply) job->reply->abort();
        cleanupJob(job);
    }
}

void MarketService::setWallhavenApiKey(const QString &apiKey) {
    m_wallhaven->setApiKey(apiKey);
}

void MarketService::setWallhavenPurity(const QString &purity) {
    m_wallhaven->setPurity(purity);
}

QNetworkRequest MarketService::makeRequest(const QUrl &url) const {
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, USER_AGENT);
    return req;
}

void MarketService::search(const MarketSearchOptions &options) {
    m_collectedResults.clear();
    m_errors.clear();
    m_totalPagesWallhaven = 1;
    m_totalPagesMoeWalls = 1;
    m_pendingProviders = 0;

    QString s = options.source.toLower();
    bool all = (s == QLatin1String("all"));

    m_wallhaven->setSorting(options.sorting);
    m_wallhaven->setTopRange(options.topRange);
    m_wallhaven->setResolution(options.resolution);
    m_wallhaven->setRatio(options.ratio);

    if (all || s == QLatin1String("wallhaven")) {
        m_pendingProviders++;
        m_wallhaven->search(options.query, options.page);
    }
    if (all || s == QLatin1String("moewalls")) {
        m_pendingProviders++;
        m_moewalls->search(options.query, options.page);
    }

    if (m_pendingProviders > 0)
        emit searchStarted();
}

void MarketService::loadThumbnail(const QString &url) {
    if (url.isEmpty()) return;

    auto it = m_thumbnailCache.find(url);
    if (it != m_thumbnailCache.end()) {
        emit thumbnailLoaded(url, *it);
        return;
    }

    if (m_pendingThumbnails.contains(url)) return;

    QString dir = cacheDirectory();
    if (!dir.isEmpty()) {
        QString fileName = dir + QDir::separator() + hashForUrl(url) + QStringLiteral(".jpg");
        if (QFile::exists(fileName)) {
            QPixmap pix(fileName);
            if (!pix.isNull()) {
                m_thumbnailCache.insert(url, pix);
                emit thumbnailLoaded(url, pix);
                return;
            }
        }
    }

    m_pendingThumbnails.insert(url);
    QNetworkReply *reply = m_nam->get(makeRequest(QUrl(url)));
    reply->setProperty("thumbUrl", url);
    connect(reply, &QNetworkReply::finished, this, &MarketService::onThumbnailFinished);
}

void MarketService::onThumbnailFinished() {
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) return;

    QString url = reply->property("thumbUrl").toString();
    m_pendingThumbnails.remove(url);

    if (reply->error() == QNetworkReply::NoError) {
        QByteArray data = reply->readAll();
        QImage img = QImage::fromData(data);
        if (!img.isNull()) {
            QPixmap pix = QPixmap::fromImage(img);
            m_thumbnailCache.insert(url, pix);

            QString dir = cacheDirectory();
            if (!dir.isEmpty()) {
                QString fileName = dir + QDir::separator() + hashForUrl(url) + QStringLiteral(".jpg");
                img.save(fileName, "JPEG", 90);
            }

            emit thumbnailLoaded(url, pix);
            reply->deleteLater();
            return;
        }
    }

    emit thumbnailError(url);
    reply->deleteLater();
}

QString MarketService::cacheDirectory() {
    QString loc = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (loc.isEmpty()) return QString();
    QString dir = loc + QDir::separator() + QStringLiteral("archpaper") + QDir::separator() + QStringLiteral("market") + QDir::separator() + QStringLiteral("thumbs");
    QDir().mkpath(dir);
    return dir;
}

void MarketService::download(const MarketItem &item, const QString &downloadDir, bool applyAfter) {
    if (m_downloadJobs.contains(item.id)) {
        emit downloadError(item.id, QStringLiteral("Download already in progress"));
        return;
    }

    QDir().mkpath(downloadDir);

    if (item.source == QLatin1String("moewalls")) {
        if (!item.fullUrl.isEmpty()) {
            startFileDownload(item, item.fullUrl, downloadDir, applyAfter);
        } else {
            resolveMoeWallsDownload(item, downloadDir, applyAfter);
        }
    } else {
        if (item.fullUrl.isEmpty()) {
            emit downloadError(item.id, QStringLiteral("No download URL available"));
            return;
        }
        startFileDownload(item, item.fullUrl, downloadDir, applyAfter);
    }
}

void MarketService::resolveMoeWallsDownload(const MarketItem &item, const QString &downloadDir, bool applyAfter) {
    emit downloadStarted(item.id);
    QNetworkReply *reply = m_nam->get(makeRequest(QUrl(item.pageUrl)));
    reply->setProperty("marketItem", QVariant::fromValue(item));
    reply->setProperty("downloadDir", downloadDir);
    reply->setProperty("applyAfter", applyAfter);
    connect(reply, &QNetworkReply::finished, this, &MarketService::onResolvePageFinished);
}

void MarketService::onResolvePageFinished() {
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) return;

    MarketItem item = reply->property("marketItem").value<MarketItem>();
    QString downloadDir = reply->property("downloadDir").toString();
    bool applyAfter = reply->property("applyAfter").toBool();

    if (reply->error() != QNetworkReply::NoError) {
        emit downloadError(item.id, QStringLiteral("MoeWalls page error: %1").arg(reply->errorString()));
        reply->deleteLater();
        return;
    }

    QString html = QString::fromUtf8(reply->readAll());
    reply->deleteLater();

    static const QRegularExpression re(QStringLiteral("id=\"moe-download\"[^>]*data-url=\"([^\"]+)\""));
    QRegularExpressionMatch m = re.match(html);
    if (!m.hasMatch()) {
        emit downloadError(item.id, QStringLiteral("Could not find MoeWalls download link"));
        return;
    }

    QString dataUrl = m.captured(1);
    QString directUrl = QLatin1String("https://go.moewalls.com/download.php?video=") + dataUrl;
    startFileDownload(item, directUrl, downloadDir, applyAfter);
}

QString MarketService::makeUniqueFileName(const MarketItem &item, const QString &downloadDir) {
    QString baseName;
    if (item.source == QLatin1String("wallhaven")) {
        QUrl u(item.fullUrl);
        baseName = u.fileName();
        if (baseName.isEmpty()) baseName = item.id + QStringLiteral(".jpg");
    } else {
        QUrl u(item.pageUrl);
        QString slug = u.path().section('/', -1, -1);
        if (slug.isEmpty()) slug = item.id;
        baseName = slug + QStringLiteral(".mp4");
    }

    baseName = sanitizeFileName(baseName);
    if (baseName.isEmpty()) baseName = item.id;
    return uniqueFilePath(downloadDir, baseName);
}

void MarketService::startFileDownload(const MarketItem &item, const QString &directUrl,
                                      const QString &downloadDir, bool applyAfter) {
    QString filePath = makeUniqueFileName(item, downloadDir);
    QFile *file = new QFile(filePath);
    if (!file->open(QIODevice::WriteOnly)) {
        emit downloadError(item.id, QStringLiteral("Cannot write file: %1").arg(file->errorString()));
        delete file;
        return;
    }

    emit downloadStarted(item.id);

    QNetworkReply *reply = m_nam->get(makeRequest(QUrl(directUrl)));
    DownloadJob *job = new DownloadJob;
    job->reply = reply;
    job->file = file;
    job->id = item.id;
    job->filePath = filePath;
    m_downloadJobs.insert(item.id, job);

    reply->setProperty("marketItem", QVariant::fromValue(item));
    reply->setProperty("applyAfter", applyAfter);
    reply->setProperty("jobId", item.id);

    connect(reply, &QNetworkReply::downloadProgress,
            this, &MarketService::onDownloadProgress);
    connect(reply, &QNetworkReply::finished,
            this, &MarketService::onDownloadFinished);
}

void MarketService::onDownloadProgress(qint64 received, qint64 total) {
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) return;
    QString id = reply->property("jobId").toString();
    emit downloadProgress(id, received, total);
}

void MarketService::onDownloadFinished() {
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply) return;

    QString id = reply->property("jobId").toString();
    MarketItem item = reply->property("marketItem").value<MarketItem>();
    bool applyAfter = reply->property("applyAfter").toBool();

    DownloadJob *job = m_downloadJobs.value(id);
    if (!job) {
        reply->deleteLater();
        return;
    }

    bool ok = (reply->error() == QNetworkReply::NoError);
    if (ok) {
        QByteArray data = reply->readAll();
        if (job->file->write(data) != data.size()) ok = false;
    }

    // Try to use the Content-Disposition filename if available.
    if (ok) {
        QByteArray cd = reply->rawHeader("Content-Disposition");
        if (!cd.isEmpty()) {
            QString realName = parseContentDispositionFileName(cd);
            if (!realName.isEmpty() && realName != QFileInfo(job->filePath).fileName()) {
                QString newPath = uniqueFilePath(QFileInfo(job->filePath).path(), realName);
                if (QFile::rename(job->filePath, newPath))
                    job->filePath = newPath;
            }
        }
    }

    cleanupJob(job);
    m_downloadJobs.remove(id);
    reply->deleteLater();

    if (ok) {
        emit downloadFinished(id, job->filePath, item, applyAfter);
    } else {
        QFile::remove(job->filePath);
        emit downloadError(id, reply->errorString().isEmpty()
                                    ? QStringLiteral("Write error")
                                    : reply->errorString());
    }
    delete job;
}

void MarketService::cleanupJob(DownloadJob *job) {
    if (!job) return;
    if (job->file) {
        job->file->close();
        delete job->file;
        job->file = nullptr;
    }
    if (job->reply) {
        job->reply->deleteLater();
        job->reply = nullptr;
    }
}

void MarketService::cancelDownload(const QString &id) {
    DownloadJob *job = m_downloadJobs.take(id);
    if (!job) return;
    if (job->reply) job->reply->abort();
    cleanupJob(job);
    QFile::remove(job->filePath);
    delete job;
}
