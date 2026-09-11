/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#ifndef PREVIEWPANEL_H
#define PREVIEWPANEL_H

#include <QFileInfo>
#include <QFrame>
#include <QPixmap>

QT_BEGIN_NAMESPACE
class QLabel;
class QThread;
class QTemporaryFile;
QT_END_NAMESPACE

class PreviewPanel : public QFrame {
    Q_OBJECT

public:
    explicit PreviewPanel(QWidget *parent = nullptr);
    ~PreviewPanel();

    void setWallpaper(const QString &path);
    void clear();

    void setIsFavorite(bool favorite);
    bool isFavorite() const;

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onVideoFrameExtracted();

private:
    void setupUi();
    void stopMedia();
    bool showImage(const QString &path);
    void scaleAndShowPixmap();
    void startVideoFrameExtraction(const QString &path);
    void setVideoFallback();

    void showEmpty();
    void updateInfo(const QFileInfo &info, const QString &badge);

    QLabel *m_imageLabel;
    QLabel *m_infoLabel;
    QLabel *m_header;


    QString m_currentPath;
    bool m_isFavorite = false;
    bool m_scalingDirty = false;
    QPixmap m_originalPixmap;

    /* Async video thumbnail extraction */
    QThread *m_extractor = nullptr;
    QTemporaryFile *m_extractorTemp = nullptr;
    quint64 m_generation = 0;
};

#endif // PREVIEWPANEL_H
