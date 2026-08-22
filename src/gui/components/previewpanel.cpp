/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "previewpanel.h"

#include <QBrush>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QMediaPlayer>
#include <QMovie>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QStackedWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QVideoWidget>

namespace {

QString ext(const QString &path) {
    return QFileInfo(path).suffix().toLower();
}

bool isAnimatedImage(const QString &path) {
    QString e = ext(path);
    return e == "gif" || e == "webp";
}

bool isVideo(const QString &path) {
    QString e = ext(path);
    return e == "mp4" || e == "webm" || e == "mkv" || e == "mov" || e == "avi" || e == "ogv";
}

QString mediaBadgeText(const QString &path) {
    if (isVideo(path)) return "\u25b6  VIDEO";
    if (isAnimatedImage(path)) return "\u25b6  GIF";
    return QString();
}

QPixmap roundPixmap(const QPixmap &source, int radius) {
    QPixmap rounded(source.size());
    rounded.fill(Qt::transparent);
    QPainter painter(&rounded);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QBrush(source));
    painter.drawRoundedRect(source.rect(), radius, radius);

    QPen border(QColor(48, 54, 61), 1);
    painter.setPen(border);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(source.rect(), radius, radius);
    painter.end();
    return rounded;
}

constexpr int PREVIEW_MARGIN = 24;
}

PreviewPanel::PreviewPanel(QWidget *parent)
    : QFrame(parent)
{
    setupUi();
}

PreviewPanel::~PreviewPanel() {
    stopMedia();
}

void PreviewPanel::setupUi() {
    setObjectName("previewPanel");
    setMinimumWidth(280);
    setMaximumWidth(380);

    auto *header = new QLabel("<b>Preview</b>");
    header->setObjectName("panelHeader");

    m_stack = new QStackedWidget(this);
    m_stack->setObjectName("previewStack");

    m_imageLabel = new QLabel("Select a wallpaper");
    m_imageLabel->setObjectName("previewImage");
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setScaledContents(false);
    m_imageLabel->setText("Select a wallpaper");

    m_videoWidget = new QVideoWidget(this);
    m_videoWidget->setObjectName("previewVideo");

    m_stack->addWidget(m_imageLabel); // index 0
    m_stack->addWidget(m_videoWidget); // index 1

    m_infoLabel = new QLabel(this);
    m_infoLabel->setObjectName("infoLabel");
    m_infoLabel->setWordWrap(true);

    m_favoriteButton = new QPushButton("\u2606  Favorite", this);
    m_favoriteButton->setObjectName("secondaryButton");
    m_favoriteButton->setToolTip("Add or remove from favorites");
    connect(m_favoriteButton, &QPushButton::clicked, this, &PreviewPanel::onFavoriteClicked);

    m_applyButton = new QPushButton("Apply", this);
    m_applyButton->setToolTip("Apply selected wallpaper");
    connect(m_applyButton, &QPushButton::clicked, this, &PreviewPanel::applyClicked);

    m_randomButton = new QPushButton("\u21c4", this);
    m_randomButton->setObjectName("secondaryButton");
    m_randomButton->setToolTip("Pick a random wallpaper");
    connect(m_randomButton, &QPushButton::clicked, this, &PreviewPanel::randomClicked);

    m_clearButton = new QPushButton("Clear", this);
    m_clearButton->setObjectName("dangerButton");
    m_clearButton->setToolTip("Remove current wallpaper");
    connect(m_clearButton, &QPushButton::clicked, this, &PreviewPanel::clearClicked);

    auto *actionLayout = new QHBoxLayout;
    actionLayout->setSpacing(10);
    actionLayout->addWidget(m_applyButton, 2);
    actionLayout->addWidget(m_randomButton, 1);
    actionLayout->addWidget(m_clearButton, 1);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(12);
    layout->addWidget(header);
    layout->addWidget(m_stack, 1);
    layout->addWidget(m_infoLabel);
    layout->addWidget(m_favoriteButton);
    layout->addLayout(actionLayout);
}

void PreviewPanel::setWallpaper(const QString &path) {
    if (m_currentPath == path && !m_scalingDirty) {
        return;
    }
    m_currentPath = path;
    m_scalingDirty = false;

    stopMedia();

    if (path.isEmpty() || !QFile::exists(path)) {
        showEmpty();
        return;
    }

    QFileInfo info(path);
    QString badge = mediaBadgeText(path);

    if (isAnimatedImage(path)) {
        showAnimatedImage(path);
    } else if (isVideo(path)) {
        showVideo(path);
    } else {
        showImage(path);
    }

    updateInfo(info, badge);
}

void PreviewPanel::clear() {
    m_currentPath.clear();
    stopMedia();
    showEmpty();
}

void PreviewPanel::setIsFavorite(bool favorite) {
    m_isFavorite = favorite;
    if (m_currentPath.isEmpty()) {
        m_favoriteButton->setEnabled(false);
        m_favoriteButton->setText("\u2606  Favorite");
        return;
    }
    m_favoriteButton->setEnabled(true);
    m_favoriteButton->setText(m_isFavorite ? "\u2605  Unfavorite" : "\u2606  Favorite");
}

bool PreviewPanel::isFavorite() const {
    return m_isFavorite;
}

void PreviewPanel::onFavoriteClicked() {
    if (m_currentPath.isEmpty()) return;
    m_isFavorite = !m_isFavorite;
    setIsFavorite(m_isFavorite);
    emit favoriteClicked();
}

void PreviewPanel::resizeEvent(QResizeEvent *event) {
    QFrame::resizeEvent(event);
    if (!m_currentPath.isEmpty()) {
        m_scalingDirty = true;
        // Reload on next event loop to avoid recursive resize floods.
        QTimer::singleShot(100, this, [this]() {
            if (m_scalingDirty) {
                setWallpaper(m_currentPath);
            }
        });
    }
}

void PreviewPanel::stopMedia() {
    if (m_movie) {
        m_movie->stop();
        delete m_movie;
        m_movie = nullptr;
    }
    if (m_player) {
        m_player->stop();
        m_player->setVideoOutput(nullptr);
        delete m_player;
        m_player = nullptr;
    }
}

void PreviewPanel::showImage(const QString &path) {
    m_stack->setCurrentIndex(0);
    QImageReader reader(path);
    if (!reader.canRead()) {
        m_imageLabel->setText("Could not load preview");
        m_imageLabel->setPixmap(QPixmap());
        return;
    }

    QSize maxSize = m_imageLabel->size() - QSize(PREVIEW_MARGIN, PREVIEW_MARGIN);
    maxSize = maxSize.boundedTo(QSize(2560, 1600));

    QSize orig = reader.size();
    if (orig.isValid() && !orig.isNull()) {
        reader.setScaledSize(orig.scaled(maxSize, Qt::KeepAspectRatio));
    }

    QImage img = reader.read();
    if (img.isNull()) {
        m_imageLabel->setText("Could not load preview");
        return;
    }

    QPixmap pix = QPixmap::fromImage(img);
    m_imageLabel->setPixmap(roundPixmap(pix, 16));
    m_imageLabel->setText("");
}

void PreviewPanel::showAnimatedImage(const QString &path) {
    m_stack->setCurrentIndex(0);
    m_movie = new QMovie(path, QByteArray(), this);
    if (m_movie->isValid()) {
        QSize size = m_imageLabel->size() - QSize(PREVIEW_MARGIN, PREVIEW_MARGIN);
        m_movie->setScaledSize(size);
        m_imageLabel->setMovie(m_movie);
        m_imageLabel->setText("");
        m_movie->start();
    } else {
        delete m_movie;
        m_movie = nullptr;
        showImage(path);
    }
}

void PreviewPanel::showVideo(const QString &path) {
    m_stack->setCurrentIndex(1);
    m_player = new QMediaPlayer(this);
    m_player->setVideoOutput(m_videoWidget);
    m_player->setSource(QUrl::fromLocalFile(path));
    m_player->setLoops(QMediaPlayer::Infinite);
    m_player->play();
}

void PreviewPanel::showEmpty() {
    m_stack->setCurrentIndex(0);
    m_imageLabel->setText("Select a wallpaper");
    m_imageLabel->setPixmap(QPixmap());
    m_infoLabel->clear();
    m_favoriteButton->setEnabled(false);
    m_favoriteButton->setText("\u2606  Favorite");
}

void PreviewPanel::updateInfo(const QFileInfo &info, const QString &badge) {
    QImageReader reader(info.absoluteFilePath());
    QSize origSize = reader.size();
    QString resolution = origSize.isValid()
                             ? QString("%1 x %2").arg(origSize.width()).arg(origSize.height())
                             : QString("unknown");

    QString badgeHtml = badge.isEmpty() ? QString()
                                        : QString(" <span style='color:#58a6ff;'>%1</span>").arg(badge);
    m_infoLabel->setText(
        QString("<b style='color:#f0f6fc;'>%1</b>%4<br><span style='color:#8b949e;'>%2 | %3</span>")
            .arg(info.fileName().toHtmlEscaped())
            .arg(resolution)
            .arg(info.suffix().toUpper())
            .arg(badgeHtml));
}
