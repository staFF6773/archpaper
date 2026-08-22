/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef WALLHAVENPROVIDER_H
#define WALLHAVENPROVIDER_H

#include "marketprovider.h"

class WallhavenProvider : public MarketProvider {
    Q_OBJECT

public:
    explicit WallhavenProvider(QNetworkAccessManager *nam, QObject *parent = nullptr);

    QString name() const override { return QStringLiteral("wallhaven"); }

    void setApiKey(const QString &apiKey);
    void setPurity(const QString &purity);
    void setSorting(const QString &sorting);
    void setTopRange(const QString &topRange);
    void setResolution(const QString &resolution);
    void setRatio(const QString &ratio);

public slots:
    void search(const QString &query, int page) override;

private slots:
    void onFinished();

private:
    QString m_apiKey;
    QString m_purity = QStringLiteral("sfw");
    QString m_sorting;
    QString m_topRange;
    QString m_resolution;
    QString m_ratio;
    QNetworkReply *m_reply = nullptr;

    static QNetworkRequest createRequest(const QUrl &url);
    QString purityCode() const;
    QString sortingValue() const;
};

#endif // WALLHAVENPROVIDER_H
