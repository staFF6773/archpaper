/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef MARKETPREVIEWPANEL_H
#define MARKETPREVIEWPANEL_H

#include "../market/marketitem.h"

#include <QFrame>

QT_BEGIN_NAMESPACE
class QLabel;
class QProgressBar;
class QPushButton;
QT_END_NAMESPACE

class MarketPreviewPanel : public QFrame {
    Q_OBJECT

public:
    explicit MarketPreviewPanel(QWidget *parent = nullptr);

    void setItem(const MarketItem &item);
    void setThumbnail(const QPixmap &pixmap);
    void setProgress(qint64 received, qint64 total);
    void setDownloadActive(bool active);

signals:
    void downloadRequested();
    void downloadAndApplyRequested();

private:
    void setupUi();
    static QPixmap roundPixmap(const QPixmap &source, const QSize &maxSize);
    void updateInfo();

    MarketItem m_item;

    QLabel *m_imageLabel;
    QLabel *m_infoLabel;
    QProgressBar *m_progressBar;
    QPushButton *m_downloadButton;
    QPushButton *m_downloadApplyButton;
};

#endif // MARKETPREVIEWPANEL_H
