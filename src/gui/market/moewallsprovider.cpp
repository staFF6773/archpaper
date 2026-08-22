/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "moewallsprovider.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QUrlQuery>

namespace {

constexpr char USER_AGENT[] = "archpaper/1.0 (Qt6; Linux; Market)";

QString resolutionFromClasses(const QJsonArray &classes) {
    static const QRegularExpression re(QStringLiteral("resolutions-(\\d+x\\d+)"));
    for (const QJsonValue &v : classes) {
        const QString s = v.toString();
        QRegularExpressionMatch m = re.match(s);
        if (m.hasMatch()) return m.captured(1);
    }
    return QString();
}

} // namespace

MoeWallsProvider::MoeWallsProvider(QNetworkAccessManager *nam, QObject *parent)
    : MarketProvider(nam, parent) {}

QString MoeWallsProvider::baseDownloadUrl(const QString &encodedDataUrl) {
    return QStringLiteral("https://go.moewalls.com/download.php?video=%1")
        .arg(QString::fromUtf8(QUrl::toPercentEncoding(encodedDataUrl)));
}

QNetworkRequest MoeWallsProvider::createRequest(const QUrl &url) {
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, USER_AGENT);
    req.setRawHeader("Referer", "https://moewalls.com/");
    return req;
}

void MoeWallsProvider::search(const QString &query, int page) {
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    QUrl url(QStringLiteral("https://moewalls.com/wp-json/wp/v2/posts"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("per_page"), QStringLiteral("24"));
    q.addQueryItem(QStringLiteral("page"), QString::number(qMax(1, page)));
    q.addQueryItem(QStringLiteral("search"), query.trimmed());
    q.addQueryItem(QStringLiteral("_embed"), QStringLiteral("wp:featuredmedia"));
    url.setQuery(q);

    m_reply = m_nam->get(createRequest(url));
    connect(m_reply, &QNetworkReply::finished, this, &MoeWallsProvider::onFinished);
}

void MoeWallsProvider::onFinished() {
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply || reply != m_reply) return;
    m_reply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        emit error(QStringLiteral("MoeWalls: %1").arg(reply->errorString()));
        reply->deleteLater();
        return;
    }

    QByteArray data = reply->readAll();
    int totalPages = 1;
    QByteArray totalPagesHeader = reply->rawHeader("X-WP-TotalPages");
    if (!totalPagesHeader.isEmpty())
        totalPages = totalPagesHeader.toInt();

    reply->deleteLater();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isArray()) {
        emit error(QStringLiteral("MoeWalls: invalid response"));
        return;
    }

    QJsonArray posts = doc.array();
    QList<MarketItem> items;
    items.reserve(posts.size());

    for (const QJsonValue &val : posts) {
        QJsonObject post = val.toObject();
        MarketItem item;
        item.id = QString::number(post.value(QStringLiteral("id")).toInt());
        item.title = post.value(QStringLiteral("title")).toObject()
                        .value(QStringLiteral("rendered")).toString();
        item.source = QStringLiteral("moewalls");
        item.pageUrl = post.value(QStringLiteral("link")).toString();
        item.fileType = QStringLiteral("mp4");
        item.resolution = resolutionFromClasses(post.value(QStringLiteral("class_list")).toArray());

        QJsonObject embedded = post.value(QStringLiteral("_embedded")).toObject();
        QJsonArray media = embedded.value(QStringLiteral("wp:featuredmedia")).toArray();
        if (!media.isEmpty()) {
            QJsonObject mediaObj = media.first().toObject();
            item.thumbnailUrl = mediaObj.value(QStringLiteral("source_url")).toString();
        }

        // Strip HTML entities in title
        item.title.replace(QRegularExpression(QStringLiteral("<[^>]+>")), QString());

        if (!item.id.isEmpty() && !item.pageUrl.isEmpty())
            items.append(item);
    }

    emit resultsReady(items, qMax(1, totalPages));
}
