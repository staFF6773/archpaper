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

constexpr char USER_AGENT[] =
    "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) "
    "Chrome/126.0.0.0 Safari/537.36";

QString resolutionFromClasses(const QString &classes) {
    static const QRegularExpression re(QStringLiteral("resolutions-(\\d+x\\d+)"),
                                         QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch m = re.match(classes);
    if (m.hasMatch()) return m.captured(1);
    return QString();
}

QString cleanTitle(const QString &title) {
    QString out = title;
    out.remove(QRegularExpression(QStringLiteral("<[^>]+>")));
    out.replace(QStringLiteral("&#8211;"), QStringLiteral("-"));
    out.replace(QStringLiteral("&#8212;"), QStringLiteral("—"));
    out.replace(QStringLiteral("&#8216;"), QStringLiteral("'"));
    out.replace(QStringLiteral("&#8217;"), QStringLiteral("'"));
    out.replace(QStringLiteral("&#8220;"), QStringLiteral("\""));
    out.replace(QStringLiteral("&#8221;"), QStringLiteral("\""));
    out.replace(QStringLiteral("&#8230;"), QStringLiteral("..."));
    return out.simplified();
}

QList<MarketItem> parseSearchResults(const QString &html) {
    QList<MarketItem> items;

    static const QRegularExpression articleRe(
        QStringLiteral("<article[^>]*>(.*?)</article>"),
        QRegularExpression::CaseInsensitiveOption |
            QRegularExpression::DotMatchesEverythingOption);

    static const QRegularExpression postIdRe(
        QStringLiteral("class=\"[^\"]*post-(\\d+)[^\"]*\""),
        QRegularExpression::CaseInsensitiveOption);

    static const QRegularExpression titleRe(
        QStringLiteral("<h3[^>]*class=\"[^\"]*entry-title[^\"]*\"[^>]*>"
                       ".*?<a[^>]*href=\"([^\"]+)\"[^>]*>([^<]+)</a>"),
        QRegularExpression::CaseInsensitiveOption |
            QRegularExpression::DotMatchesEverythingOption);

    static const QRegularExpression thumbRe(
        QStringLiteral("<img[^>]*class=\"[^\"]*wp-post-image[^\"]*\"[^>]*"
                       "src=\"([^\"]+)\""),
        QRegularExpression::CaseInsensitiveOption);

    static const QRegularExpression resolutionRe(
        QStringLiteral("resolutions-(\\d+x\\d+)"),
        QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatchIterator it = articleRe.globalMatch(html);
    while (it.hasNext()) {
        QRegularExpressionMatch articleMatch = it.next();
        QString article = articleMatch.captured(1);

        QRegularExpressionMatch idMatch = postIdRe.match(article);
        if (!idMatch.hasMatch()) continue;

        QRegularExpressionMatch titleMatch = titleRe.match(article);
        if (!titleMatch.hasMatch()) continue;

        MarketItem item;
        item.id = idMatch.captured(1);
        item.pageUrl = titleMatch.captured(1);
        item.title = cleanTitle(titleMatch.captured(2));
        item.source = QLatin1String("moewalls");
        item.fileType = QLatin1String("mp4");

        QRegularExpressionMatch thumbMatch = thumbRe.match(article);
        if (thumbMatch.hasMatch()) item.thumbnailUrl = thumbMatch.captured(1);

        QRegularExpressionMatch resMatch = resolutionRe.match(article);
        if (resMatch.hasMatch()) item.resolution = resMatch.captured(1);

        if (!item.id.isEmpty() && !item.pageUrl.isEmpty())
            items.append(item);
    }

    return items;
}

int parseTotalPages(const QString &html) {
    int total = 1;
    static const QRegularExpression pageRe(
        QStringLiteral("<a[^>]*class=\"[^\"]*page-numbers[^\"]*\"[^>]*>([^<]+)</a>"),
        QRegularExpression::CaseInsensitiveOption);

    QRegularExpressionMatchIterator it = pageRe.globalMatch(html);
    while (it.hasNext()) {
        QRegularExpressionMatch m = it.next();
        bool ok = false;
        int n = m.captured(1).toInt(&ok);
        if (ok && n > total) total = n;
    }
    return total;
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
    req.setRawHeader("User-Agent", USER_AGENT);
    req.setRawHeader("Accept",
                     "text/html,application/xhtml+xml,application/xml;q=0.9,image/avif,"
                     "image/webp,image/apng,*/*;q=0.8");
    req.setRawHeader("Accept-Language", "en-US,en;q=0.9");
    req.setRawHeader("Referer", "https://moewalls.com/");
    req.setRawHeader("Connection", "keep-alive");
    return req;
}

void MoeWallsProvider::search(const QString &query, int page) {
    if (m_reply) {
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }

    QUrl url;
    if (page > 1) {
        url = QUrl(QStringLiteral("https://moewalls.com/page/%1/").arg(page));
    } else {
        url = QUrl(QStringLiteral("https://moewalls.com/"));
    }

    QUrlQuery q;
    q.addQueryItem(QStringLiteral("s"), query.trimmed());
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

    QString html = QString::fromUtf8(reply->readAll());
    reply->deleteLater();

    QList<MarketItem> items = parseSearchResults(html);
    int totalPages = parseTotalPages(html);

    if (items.isEmpty()) {
        emit error(QStringLiteral("MoeWalls: no results found"));
        return;
    }

    emit resultsReady(items, qMax(1, totalPages));
}
