/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "marketpreviewpanel.h"

#include <QBrush>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>

namespace {

constexpr int PREVIEW_MARGIN = 24;

} // namespace

MarketPreviewPanel::MarketPreviewPanel(QWidget *parent)
    : QFrame(parent) {
    setupUi();
}

void MarketPreviewPanel::setupUi() {
    setObjectName(QStringLiteral("previewPanel"));
    setMinimumWidth(280);
    setMaximumWidth(380);

    auto *header = new QLabel(QStringLiteral("<b>Market Preview</b>"));
    header->setObjectName(QStringLiteral("panelHeader"));

    m_imageLabel = new QLabel(QStringLiteral("Select a wallpaper"), this);
    m_imageLabel->setObjectName(QStringLiteral("previewImage"));
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setScaledContents(false);

    m_infoLabel = new QLabel(this);
    m_infoLabel->setObjectName(QStringLiteral("infoLabel"));
    m_infoLabel->setWordWrap(true);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(false);

    m_downloadButton = new QPushButton(QStringLiteral("Download"), this);
    m_downloadButton->setToolTip(QStringLiteral("Download to your wallpapers folder"));
    connect(m_downloadButton, &QPushButton::clicked, this, &MarketPreviewPanel::downloadRequested);

    m_downloadApplyButton = new QPushButton(QStringLiteral("Download & Apply"), this);
    m_downloadApplyButton->setToolTip(QStringLiteral("Download and apply immediately"));
    connect(m_downloadApplyButton, &QPushButton::clicked,
            this, &MarketPreviewPanel::downloadAndApplyRequested);

    auto *buttonLayout = new QHBoxLayout;
    buttonLayout->setSpacing(10);
    buttonLayout->addWidget(m_downloadButton, 1);
    buttonLayout->addWidget(m_downloadApplyButton, 1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);
    layout->addWidget(header);
    layout->addWidget(m_imageLabel, 1);
    layout->addWidget(m_infoLabel);
    layout->addWidget(m_progressBar);
    layout->addLayout(buttonLayout);
}

void MarketPreviewPanel::setItem(const MarketItem &item) {
    m_item = item;
    m_imageLabel->setText(item.title.isEmpty() ? QStringLiteral("Select a wallpaper") : item.title);
    m_imageLabel->setPixmap(QPixmap());
    updateInfo();
    setDownloadActive(false);
}

void MarketPreviewPanel::setThumbnail(const QPixmap &pixmap) {
    if (!pixmap.isNull()) {
        m_imageLabel->setPixmap(roundPixmap(pixmap, m_imageLabel->size() - QSize(PREVIEW_MARGIN, PREVIEW_MARGIN)));
    }
}

void MarketPreviewPanel::setProgress(qint64 received, qint64 total) {
    if (total <= 0) {
        m_progressBar->setRange(0, 0);
    } else {
        m_progressBar->setRange(0, 100);
        m_progressBar->setValue(static_cast<int>((received * 100) / total));
    }
    m_progressBar->setVisible(received < total && total > 0);
}

void MarketPreviewPanel::setDownloadActive(bool active) {
    m_downloadButton->setEnabled(!active);
    m_downloadApplyButton->setEnabled(!active);
    m_progressBar->setVisible(active);
}

void MarketPreviewPanel::updateInfo() {
    if (m_item.id.isEmpty()) {
        m_infoLabel->clear();
        return;
    }

    QString badgeColor = m_item.source == QLatin1String("moewalls") ? QStringLiteral("#f78166") : QStringLiteral("#58a6ff");
    QString html = QStringLiteral(
        "<b style='color:#f0f6fc;'>%1</b><br>"
        "<span style='color:#8b949e;'>%2 | %3 | </span><span style='color:%4;'>%5</span>")
        .arg(m_item.title.toHtmlEscaped())
        .arg(m_item.resolution.isEmpty() ? QStringLiteral("?") : m_item.resolution)
        .arg(m_item.fileType.isEmpty() ? QStringLiteral("?") : m_item.fileType.toUpper())
        .arg(badgeColor)
        .arg(m_item.source);
    m_infoLabel->setText(html);
}

QPixmap MarketPreviewPanel::roundPixmap(const QPixmap &source, const QSize &maxSize) {
    if (source.isNull()) return QPixmap();

    QSize scaledSize = source.size().scaled(maxSize, Qt::KeepAspectRatio);
    QPixmap scaled = source.scaled(scaledSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    QPixmap rounded(scaled.size());
    rounded.fill(Qt::transparent);

    QPainter painter(&rounded);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QBrush(scaled));
    painter.drawRoundedRect(scaled.rect(), 14, 14);

    QPen border(QColor(48, 54, 61), 1);
    painter.setPen(border);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(scaled.rect(), 14, 14);
    painter.end();

    return rounded;
}
