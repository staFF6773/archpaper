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
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QTemporaryFile>
#include <QTimer>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>
#include "archpaper/cache.h"
#include "archpaper/process.h"
extern "C" {
#include "archpaper/utils.h"
}

namespace {

bool isAnimatedImage(const QString &path) {
    return is_animated_image(path.toUtf8().constData());
}

bool isVideo(const QString &path) {
    return is_video(path.toUtf8().constData());
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

    QPen border(QColor(42, 42, 42), 1);
    painter.setPen(border);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(source.rect(), radius, radius);
    painter.end();
    return rounded;
}

constexpr int PREVIEW_MARGIN = 12;

} // namespace

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
    setMinimumWidth(220);
    setMaximumWidth(360);

    m_header = new QLabel("Preview");
    m_header->setObjectName("panelHeader");

    m_imageLabel = new QLabel("Select a wallpaper");
    m_imageLabel->setObjectName("previewImage");
    m_imageLabel->setAlignment(Qt::AlignCenter);
    m_imageLabel->setScaledContents(false);
    m_imageLabel->setMinimumSize(180, 112);
    m_imageLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_imageLabel->setFixedHeight(140);
    m_imageLabel->setText("Select a wallpaper");

    m_infoLabel = new QLabel(this);
    m_infoLabel->setObjectName("infoLabel");
    m_infoLabel->setWordWrap(true);
    m_infoLabel->setMinimumWidth(0);
    m_infoLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    layout->addWidget(m_header);
    layout->addWidget(m_imageLabel);
    layout->addWidget(m_infoLabel);
    auto *hint = new QLabel("Double-click a wallpaper to apply");
    hint->setObjectName("mutedLabel");
    hint->setWordWrap(true);
    layout->addWidget(hint);
    layout->addStretch();
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

    if (isVideo(path)) {
        setVideoFallback();
        startVideoFrameExtraction(path);
    } else {
        /* QImageReader displays the first frame of animated images. */
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
    m_header->setText(favorite && !m_currentPath.isEmpty() ? "Preview  ·  \u2605 Favorite" : "Preview");
}

bool PreviewPanel::isFavorite() const {
    return m_isFavorite;
}

void PreviewPanel::resizeEvent(QResizeEvent *event) {
    QFrame::resizeEvent(event);
    m_imageLabel->setFixedHeight(qMax(120, (width() - 24) * 9 / 16));
    if (m_currentPath.isEmpty())
        return;

    m_scalingDirty = true;
    // Re-scale the cached static pixmap on the next event loop tick.
    QTimer::singleShot(100, this, [this]() {
        if (m_scalingDirty && !m_originalPixmap.isNull()) {
            scaleAndShowPixmap();
        }
    });
}

void PreviewPanel::stopMedia() {
    ++m_generation;
    if (m_extractor) {
        m_extractor->disconnect(this);
        m_extractor->requestInterruption();
        m_extractor->wait();
        delete m_extractor;
        m_extractor = nullptr;
    }
    if (m_extractorTemp) {
        delete m_extractorTemp;
        m_extractorTemp = nullptr;
    }
    m_originalPixmap = QPixmap();
}

bool PreviewPanel::showImage(const QString &path) {
    QImageReader reader(path);
    if (!reader.canRead()) {
        m_imageLabel->setPixmap(QPixmap());
        m_imageLabel->setText("Could not load preview");
        m_originalPixmap = QPixmap();
        return false;
    }

    QImage img = reader.read();
    if (img.isNull()) {
        m_imageLabel->setText("Could not load preview");
        m_originalPixmap = QPixmap();
        return false;
    }

    /* Keep the original at a reasonable resolution so we can re-scale it on
     * resize without re-reading the file from disk. */
    m_originalPixmap = QPixmap::fromImage(img);
    scaleAndShowPixmap();
    return true;
}

void PreviewPanel::scaleAndShowPixmap() {
    if (m_originalPixmap.isNull()) {
        return;
    }
    m_scalingDirty = false;

    QSize maxSize = m_imageLabel->size() - QSize(PREVIEW_MARGIN, PREVIEW_MARGIN);
    maxSize = maxSize.boundedTo(QSize(2560, 1600));
    if (maxSize.width() <= 0 || maxSize.height() <= 0)
        return;

    QPixmap scaled = m_originalPixmap.scaled(maxSize, Qt::KeepAspectRatio,
                                           Qt::SmoothTransformation);
    m_imageLabel->setPixmap(roundPixmap(scaled, 6));
}

void PreviewPanel::startVideoFrameExtraction(const QString &path) {
    m_extractorTemp = new QTemporaryFile(
        QDir::tempPath() + QStringLiteral("/archpaper_vthumb_XXXXXX.png"), this);
    m_extractorTemp->setAutoRemove(true);
    if (!m_extractorTemp->open()) {
        delete m_extractorTemp;
        m_extractorTemp = nullptr;
        return;
    }
    QString tmpPath = m_extractorTemp->fileName();
    m_extractorTemp->close();

    m_extractor = QThread::create([path, tmpPath]() {
        ap_process_set_cancel_check([](void *) -> int {
            return QThread::currentThread()->isInterruptionRequested();
        }, nullptr);
        ap_thumbnail_extract(path.toUtf8().constData(), tmpPath.toUtf8().constData());
        ap_process_set_cancel_check(nullptr, nullptr);
    });
    m_extractor->setProperty("outputPath", tmpPath);
    const quint64 generation = m_generation;
    connect(m_extractor, &QThread::finished, this, [this, generation]() {
        if (generation == m_generation) onVideoFrameExtracted();
    });
    m_extractor->start();
}

void PreviewPanel::onVideoFrameExtracted() {
    if (!m_extractor) return;

    QString tmpPath = m_extractor->property("outputPath").toString();
    m_extractor->deleteLater();
    m_extractor = nullptr;

    if (!QFile::exists(tmpPath) || m_currentPath.isEmpty()) {
        setVideoFallback();
        return;
    }

    QPixmap pix(tmpPath);
    if (pix.isNull()) {
        setVideoFallback();
        return;
    }

    m_originalPixmap = pix;
    scaleAndShowPixmap();
}

void PreviewPanel::setVideoFallback() {
    if (m_currentPath.isEmpty()) return;
    m_imageLabel->setPixmap(QPixmap());
    m_imageLabel->setText(QStringLiteral("\u25b6  VIDEO"));
    m_originalPixmap = QPixmap();
}

void PreviewPanel::showEmpty() {
    m_imageLabel->setPixmap(QPixmap());
    m_imageLabel->setText("Select a wallpaper");
    m_infoLabel->clear();
    setIsFavorite(false);
    m_originalPixmap = QPixmap();
}

void PreviewPanel::updateInfo(const QFileInfo &info, const QString &badge) {
    QImageReader reader(info.absoluteFilePath());
    QSize origSize = reader.size();
    QString resolution = origSize.isValid()
                             ? QString("%1 x %2").arg(origSize.width()).arg(origSize.height())
                             : QString("unknown");

    QString badgeHtml = badge.isEmpty() ? QString()
                                        : QString(" <span style='color:#b5b5b5;'>%1</span>").arg(badge);
    m_infoLabel->setText(
        QString("<b style='color:#eeeeee;'>%1</b>%4<br><span style='color:#999999;'>%2 | %3</span>")
            .arg(info.fileName().toHtmlEscaped())
            .arg(resolution)
            .arg(info.suffix().toUpper())
            .arg(badgeHtml));
}
