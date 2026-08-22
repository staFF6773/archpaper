/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef MARKETPROVIDER_H
#define MARKETPROVIDER_H

#include "marketitem.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>

class MarketProvider : public QObject {
    Q_OBJECT

public:
    explicit MarketProvider(QNetworkAccessManager *nam, QObject *parent = nullptr)
        : QObject(parent), m_nam(nam) {}
    virtual ~MarketProvider() = default;

    virtual QString name() const = 0;

public slots:
    virtual void search(const QString &query, int page) = 0;

signals:
    void resultsReady(const QList<MarketItem> &items, int totalPages);
    void error(const QString &message);

protected:
    QNetworkAccessManager *m_nam;
};

#endif // MARKETPROVIDER_H
