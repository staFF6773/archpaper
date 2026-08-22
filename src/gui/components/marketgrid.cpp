/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "marketgrid.h"

#include <QHash>
#include <QIcon>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QVBoxLayout>

namespace {

constexpr int THUMB_WIDTH = 160;
constexpr int THUMB_HEIGHT = 90;

QPixmap genericThumb(bool video) {
    QIcon icon = QIcon::fromTheme(video ? QStringLiteral("video-x-generic")
                                        : QStringLiteral("image-x-generic"));
    if (!icon.isNull()) {
        QPixmap pix = icon.pixmap(QSize(THUMB_WIDTH, THUMB_HEIGHT));
        if (!pix.isNull()) return pix;
    }

    QPixmap fallback(THUMB_WIDTH, THUMB_HEIGHT);
    fallback.fill(video ? QColor(40, 45, 52) : QColor(35, 39, 46));
    QPainter p(&fallback);
    p.setPen(QColor(120, 128, 140));
    p.drawText(fallback.rect(), Qt::AlignCenter, video ? QStringLiteral("VIDEO") : QStringLiteral("IMG"));
    p.end();
    return fallback;
}

} // namespace

MarketGrid::MarketGrid(QWidget *parent)
    : QFrame(parent) {
    setupUi();
}

void MarketGrid::setupUi() {
    setObjectName(QStringLiteral("marketGrid"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    m_list = new QListWidget(this);
    m_list->setObjectName(QStringLiteral("imagesList"));
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(THUMB_WIDTH, THUMB_HEIGHT));
    m_list->setSpacing(10);
    m_list->setWrapping(true);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setGridSize(QSize(THUMB_WIDTH + 24, THUMB_HEIGHT + 54));
    m_list->setWordWrap(true);
    m_list->setUniformItemSizes(true);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setFrameShape(QFrame::NoFrame);

    connect(m_list, &QListWidget::itemSelectionChanged,
            this, &MarketGrid::onSelectionChanged);
    connect(m_list, &QListWidget::itemDoubleClicked,
            this, &MarketGrid::onItemDoubleClicked);

    layout->addWidget(m_list);
}

void MarketGrid::clear() {
    m_list->clear();
    m_urlToRows.clear();
    emit countChanged(0);
}

void MarketGrid::setItems(const QList<MarketItem> &items) {
    clear();
    appendItems(items);
}

void MarketGrid::appendItems(const QList<MarketItem> &items) {
    for (const MarketItem &item : items) {
        bool video = (item.source == QLatin1String("moewalls") || item.fileType.startsWith(QLatin1String("video")));
        addItem(item, genericThumb(video));
        if (!item.thumbnailUrl.isEmpty())
            emit thumbnailNeeded(item.thumbnailUrl);
    }
    emit countChanged(m_list->count());
}

void MarketGrid::addItem(const MarketItem &item, const QPixmap &thumb) {
    QString label = item.title;
    if (!item.resolution.isEmpty())
        label += QStringLiteral("\n%1 | %2").arg(item.resolution, item.source);

    auto *listItem = new QListWidgetItem(QIcon(roundedThumb(thumb, QSize(THUMB_WIDTH, THUMB_HEIGHT))), label);
    listItem->setData(Qt::UserRole, QVariant::fromValue(item));
    listItem->setToolTip(item.pageUrl.isEmpty() ? item.title : item.pageUrl);
    listItem->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    int row = m_list->count();
    m_list->addItem(listItem);

    if (!item.thumbnailUrl.isEmpty())
        m_urlToRows[item.thumbnailUrl].append(row);
}

MarketItem MarketGrid::selectedItem() const {
    auto *listItem = m_list->currentItem();
    if (!listItem) return MarketItem();
    return listItem->data(Qt::UserRole).value<MarketItem>();
}

QList<MarketItem> MarketGrid::items() const {
    QList<MarketItem> out;
    out.reserve(m_list->count());
    for (int i = 0; i < m_list->count(); ++i) {
        auto *listItem = m_list->item(i);
        if (!listItem) continue;
        out.append(listItem->data(Qt::UserRole).value<MarketItem>());
    }
    return out;
}

int MarketGrid::count() const {
    return m_list->count();
}

void MarketGrid::setThumbnail(const QString &url, const QPixmap &pixmap) {
    auto it = m_urlToRows.find(url);
    if (it == m_urlToRows.end()) return;

    QPixmap rounded = roundedThumb(pixmap, QSize(THUMB_WIDTH, THUMB_HEIGHT));
    for (int row : it.value()) {
        auto *listItem = m_list->item(row);
        if (!listItem) continue;
        listItem->setIcon(QIcon(rounded));
    }
}

void MarketGrid::onSelectionChanged() {
    MarketItem item = selectedItem();
    if (!item.id.isEmpty()) emit itemSelected(item);
}

void MarketGrid::onItemDoubleClicked(QListWidgetItem *item) {
    if (!item) return;
    MarketItem mi = item->data(Qt::UserRole).value<MarketItem>();
    if (!mi.id.isEmpty()) emit itemDoubleClicked(mi);
}

QPixmap MarketGrid::roundedThumb(const QPixmap &source, const QSize &targetSize) {
    if (source.isNull()) return QPixmap();

    QSize scaledSize = source.size().scaled(targetSize, Qt::KeepAspectRatio);
    QPixmap scaled = source.scaled(scaledSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QPixmap canvas(targetSize.width() + 4, targetSize.height() + 4);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    QRect outerRect(2, 2, targetSize.width(), targetSize.height());
    QPainterPath clip;
    clip.addRoundedRect(outerRect, 10, 10);

    painter.setPen(Qt::NoPen);
    painter.setClipPath(clip);

    QRect imageRect(QPoint(0, 0), scaled.size());
    imageRect.moveCenter(outerRect.center());
    painter.drawPixmap(imageRect, scaled);
    painter.setClipping(false);

    QPen border(QColor(48, 54, 61), 1);
    painter.setPen(border);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(outerRect, 10, 10);
    painter.end();

    return canvas;
}
