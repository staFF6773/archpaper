/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "wallhavenprovider.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QUrlQuery>

namespace {

constexpr char USER_AGENT[] = "archpaper/1.0 (Qt6; Linux; Market)";

} // namespace

WallhavenProvider::WallhavenProvider(QNetworkAccessManager *nam, QObject *parent)
    : MarketProvider(nam, parent) {}

void WallhavenProvider::setApiKey(const QString &apiKey) {
    m_apiKey = apiKey;
}

void WallhavenProvider::setPurity(const QString &purity) {
    m_purity = purity.trimmed().toLower();
    if (m_purity.isEmpty()) m_purity = QStringLiteral("sfw");
}

void WallhavenProvider::setSorting(const QString &sorting) {
    m_sorting = sorting.trimmed().toLower();
}

void WallhavenProvider::setTopRange(const QString &topRange) {
    m_topRange = topRange;
}

void WallhavenProvider::setResolution(const QString &resolution) {
    m_resolution = resolution.trimmed();
}

void WallhavenProvider::setRatio(const QString &ratio) {
    m_ratio = ratio.trimmed();
}

QNetworkRequest WallhavenProvider::createRequest(const QUrl &url) {
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader, USER_AGENT);
    return req;
}

QString WallhavenProvider::purityCode() const {
    if (m_purity == QLatin1String("sketchy")) return QStringLiteral("010");
    if (m_purity == QLatin1String("nsfw")) return QStringLiteral("001");
    if (m_purity == QLatin1String("sfw,sketchy") || m_purity == QLatin1String("sketchy,sfw"))
        return QStringLiteral("110");
    if (m_purity == QLatin1String("all")) return QStringLiteral("111");
    return QStringLiteral("100"); // sfw default
}

QString WallhavenProvider::sortingValue() const {
    if (m_sorting == QLatin1String("relevance")) return QStringLiteral("relevance");
    if (m_sorting == QLatin1String("random")) return QStringLiteral("random");
    if (m_sorting == QLatin1String("views")) return QStringLiteral("views");
    if (m_sorting == QLatin1String("favorites")) return QStringLiteral("favorites");
    if (m_sorting == QLatin1String("toplist")) return QStringLiteral("toplist");
    return QStringLiteral("date_added"); // latest default
}

void WallhavenProvider::search(const QString &query, int page) {
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    QUrl url(QStringLiteral("https://wallhaven.cc/api/v1/search"));
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("q"), query.trimmed());
    q.addQueryItem(QStringLiteral("page"), QString::number(qMax(1, page)));
    q.addQueryItem(QStringLiteral("purity"), purityCode());
    q.addQueryItem(QStringLiteral("categories"), QStringLiteral("111"));
    q.addQueryItem(QStringLiteral("sorting"), sortingValue());
    if (m_sorting == QLatin1String("toplist") && !m_topRange.isEmpty()) {
        q.addQueryItem(QStringLiteral("topRange"), m_topRange);
    }
    if (!m_resolution.isEmpty()) {
        q.addQueryItem(QStringLiteral("resolutions"), m_resolution);
    }
    if (!m_ratio.isEmpty()) {
        q.addQueryItem(QStringLiteral("ratios"), m_ratio);
    }
    if (!m_apiKey.isEmpty()) {
        q.addQueryItem(QStringLiteral("apikey"), m_apiKey);
    }
    url.setQuery(q);

    m_reply = m_nam->get(createRequest(url));
    connect(m_reply, &QNetworkReply::finished, this, &WallhavenProvider::onFinished);
}

void WallhavenProvider::onFinished() {
    auto *reply = qobject_cast<QNetworkReply *>(sender());
    if (!reply || reply != m_reply) return;
    m_reply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        emit error(QStringLiteral("Wallhaven: %1").arg(reply->errorString()));
        reply->deleteLater();
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject()) {
        emit error(QStringLiteral("Wallhaven: invalid response"));
        return;
    }

    QJsonObject root = doc.object();
    QJsonArray arr = root.value(QStringLiteral("data")).toArray();
    QJsonObject meta = root.value(QStringLiteral("meta")).toObject();
    int lastPage = meta.value(QStringLiteral("last_page")).toInt(1);

    QList<MarketItem> items;
    items.reserve(arr.size());

    for (const QJsonValue &val : arr) {
        QJsonObject obj = val.toObject();
        MarketItem item;
        item.id = obj.value(QStringLiteral("id")).toString();
        item.title = item.id;
        item.source = QStringLiteral("wallhaven");
        item.pageUrl = obj.value(QStringLiteral("url")).toString();
        item.fullUrl = obj.value(QStringLiteral("path")).toString();
        item.resolution = obj.value(QStringLiteral("resolution")).toString();
        item.fileType = obj.value(QStringLiteral("file_type")).toString();

        QJsonObject thumbs = obj.value(QStringLiteral("thumbs")).toObject();
        item.thumbnailUrl = thumbs.value(QStringLiteral("large")).toString();
        if (item.thumbnailUrl.isEmpty())
            item.thumbnailUrl = thumbs.value(QStringLiteral("small")).toString();

        if (!item.id.isEmpty() && !item.fullUrl.isEmpty())
            items.append(item);
    }

    emit resultsReady(items, lastPage);
}
