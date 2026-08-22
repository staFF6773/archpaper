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

QT_BEGIN_NAMESPACE
class QLabel;
class QPushButton;
class QStackedWidget;
class QMovie;
class QMediaPlayer;
class QVideoWidget;
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

signals:
    void favoriteClicked();
    void applyClicked();
    void randomClicked();
    void clearClicked();

protected:
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void onFavoriteClicked();

private:
    void setupUi();
    void stopMedia();
    void showImage(const QString &path);
    void showAnimatedImage(const QString &path);
    void showVideo(const QString &path);
    void showEmpty();
    void updateInfo(const QFileInfo &info, const QString &badge);

    QStackedWidget *m_stack;
    QLabel *m_imageLabel;
    QVideoWidget *m_videoWidget;
    QLabel *m_infoLabel;
    QPushButton *m_favoriteButton;
    QPushButton *m_applyButton;
    QPushButton *m_randomButton;
    QPushButton *m_clearButton;

    QMovie *m_movie = nullptr;
    QMediaPlayer *m_player = nullptr;

    QString m_currentPath;
    bool m_isFavorite = false;
    bool m_scalingDirty = false;
};

#endif // PREVIEWPANEL_H
