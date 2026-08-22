/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef MOEWALLSPROVIDER_H
#define MOEWALLSPROVIDER_H

#include "marketprovider.h"

#include <QNetworkReply>

class MoeWallsProvider : public MarketProvider {
    Q_OBJECT

public:
    explicit MoeWallsProvider(QNetworkAccessManager *nam, QObject *parent = nullptr);

    QString name() const override { return QStringLiteral("moewalls"); }

    static QString baseDownloadUrl(const QString &encodedDataUrl);

public slots:
    void search(const QString &query, int page) override;

private slots:
    void onFinished();

private:
    QNetworkReply *m_reply = nullptr;
    static QNetworkRequest createRequest(const QUrl &url);
};

#endif // MOEWALLSPROVIDER_H
