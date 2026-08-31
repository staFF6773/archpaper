/*
 * archpaper - Wallpaper manager for Wayland
 * Copyright (C) 2024  archpaper contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

#include "wallpapergrid.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRandomGenerator>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QResizeEvent>
#include <QVBoxLayout>

namespace {

constexpr int THUMB_WIDTH = 240;
constexpr int THUMB_HEIGHT = 135;

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

bool isAnimatedFile(const QString &path) {
    return isAnimatedImage(path) || isVideo(path);
}

QString mediaBadgeText(const QString &path) {
    if (isVideo(path)) return "\u25b6  VIDEO";
    if (isAnimatedImage(path)) return "\u25b6  GIF";
    return QString();
}

}

WallpaperGrid::WallpaperGrid(QWidget *parent)
    : QFrame(parent)
{
    setupUi();
}

void WallpaperGrid::setupUi() {
    setObjectName("wallpaperGrid");

    m_filter = new QLineEdit(this);
    m_filter->setObjectName("internalFilter");
    m_filter->hide();
    connect(m_filter, &QLineEdit::textChanged, this, &WallpaperGrid::onFilterTextChanged);

    m_list = new QListWidget(this);
    m_list->setObjectName("imagesList");
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(THUMB_WIDTH, THUMB_HEIGHT));
    m_list->setSpacing(14);
    m_list->setWrapping(true);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setGridSize(QSize(THUMB_WIDTH + 32, THUMB_HEIGHT + 56));
    m_list->setWordWrap(false);
    m_list->setUniformItemSizes(true);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setFrameShape(QFrame::NoFrame);

    connect(m_list, &QListWidget::itemSelectionChanged, this, &WallpaperGrid::onSelectionChanged);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &WallpaperGrid::onItemDoubleClicked);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_list, 1);
}


void WallpaperGrid::clear() {
    m_list->clear();
}

void WallpaperGrid::addWallpaper(const QString &path) {
    QFileInfo info(path);
    QIcon icon;
    QPixmap thumb = createThumbnail(path, QSize(THUMB_WIDTH, THUMB_HEIGHT));
    if (!thumb.isNull()) {
        icon = QIcon(thumb);
    } else {
        icon = QIcon::fromTheme(isVideo(path) ? "video-x-generic" : "image-x-generic");
    }

    QString display = info.fileName();
    QString badge = mediaBadgeText(path);
    if (!badge.isEmpty()) {
        display += "\n" + badge;
    }

    auto *item = new QListWidgetItem(icon, display);
    item->setData(Qt::UserRole, path);
    item->setToolTip(path);
    item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
    m_list->addItem(item);
}

void WallpaperGrid::loadFromFolder(const QString &folder) {
    clear();

    QDir dir(folder);
    QStringList filters;
    filters << "*.png" << "*.jpg" << "*.jpeg" << "*.webp" << "*.bmp" << "*.gif" << "*.tif" << "*.tiff"
            << "*.mp4" << "*.webm" << "*.mkv" << "*.mov" << "*.avi" << "*.ogv";
    QFileInfoList files = dir.entryInfoList(filters, QDir::Files, QDir::Name);

    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (const QFileInfo &info : files) {
        addWallpaper(info.absoluteFilePath());
    }
    QApplication::restoreOverrideCursor();

    refreshFilter();
}

void WallpaperGrid::setWallpapers(const QStringList &paths) {
    clear();
    for (const QString &path : paths) {
        addWallpaper(path);
    }
    refreshFilter();
}

QString WallpaperGrid::selectedPath() const {
    auto *item = m_list->currentItem();
    return item ? item->data(Qt::UserRole).toString() : QString();
}

QString WallpaperGrid::pathAt(int row) const {
    auto *item = m_list->item(row);
    return item ? item->data(Qt::UserRole).toString() : QString();
}

int WallpaperGrid::count() const {
    return m_list->count();
}

int WallpaperGrid::visibleCount() const {
    int n = 0;
    for (int i = 0; i < m_list->count(); ++i) {
        if (!m_list->item(i)->isHidden()) ++n;
    }
    return n;
}

QStringList WallpaperGrid::currentPaths() const {
    QStringList paths;
    for (int i = 0; i < m_list->count(); ++i) {
        paths.append(m_list->item(i)->data(Qt::UserRole).toString());
    }
    return paths;
}

void WallpaperGrid::selectRandom() {
    int visible = visibleCount();
    if (visible == 0) return;

    int idx;
    do {
        idx = QRandomGenerator::global()->bounded(m_list->count());
    } while (m_list->item(idx)->isHidden());

    m_list->setCurrentRow(idx);
    onSelectionChanged();
}

void WallpaperGrid::setFilter(const QString &text) {
    m_filter->setText(text);
}

void WallpaperGrid::refreshFilter() {
    onFilterTextChanged(m_filter->text());
}

void WallpaperGrid::onSelectionChanged() {
    QString path = selectedPath();
    if (!path.isEmpty()) {
        emit imageSelected(path);
    }
}

void WallpaperGrid::onItemDoubleClicked(QListWidgetItem *item) {
    if (!item) return;
    QString path = item->data(Qt::UserRole).toString();
    emit imageDoubleClicked(path);
}

void WallpaperGrid::onFilterTextChanged(const QString &text) {
    int visible = 0;
    for (int i = 0; i < m_list->count(); ++i) {
        auto *item = m_list->item(i);
        QFileInfo info(item->data(Qt::UserRole).toString());
        bool match = text.isEmpty() || info.fileName().contains(text, Qt::CaseInsensitive);
        item->setHidden(!match);
        if (match) ++visible;
    }
    emit countChanged(visible, m_list->count());
}

static QPixmap roundedThumbnail(const QImage &sourceImage, const QSize &targetSize) {
    QPixmap source = QPixmap::fromImage(sourceImage);
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

    /* Center the scaled image inside the rounded rect without stretching. */
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

static bool extractVideoFrame(const QString &videoPath, const QString &outputImage) {
    /* Try ffmpegthumbnailer first because it is fast and lightweight. */
    if (!QStandardPaths::findExecutable("ffmpegthumbnailer").isEmpty()) {
        QProcess proc;
        proc.start("ffmpegthumbnailer", {"-i", videoPath, "-o", outputImage, "-s", "320"});
        if (proc.waitForFinished(8000) && proc.exitCode() == 0) {
            return QFile::exists(outputImage);
        }
    }

    /* Fallback to ffmpeg. */
    if (!QStandardPaths::findExecutable("ffmpeg").isEmpty()) {
        QProcess proc;
        proc.start("ffmpeg", {"-y", "-ss", "00:00:01", "-i", videoPath,
                              "-vf", "scale=320:-1", "-vframes", "1",
                              "-q:v", "2", outputImage});
        if (proc.waitForFinished(15000) && proc.exitCode() == 0) {
            return QFile::exists(outputImage);
        }
    }

    return false;
}

QPixmap WallpaperGrid::createThumbnail(const QString &path, const QSize &targetSize) {
    if (isVideo(path)) {
        QTemporaryFile tmp(QDir::tempPath() + "/archpaper_video_thumb_XXXXXX.png");
        tmp.setAutoRemove(true);
        if (tmp.open()) {
            QString tmpPath = tmp.fileName();
            tmp.close();
            if (extractVideoFrame(path, tmpPath)) {
                QImage frame(tmpPath);
                if (!frame.isNull()) {
                    return roundedThumbnail(frame, targetSize);
                }
            }
        }
        return QPixmap();
    }

    QImageReader reader(path);
    if (!reader.canRead()) {
        return QPixmap();
    }

    /* Read the first frame of animated images (GIF/WebP). */
    QImage img = reader.read();
    if (img.isNull()) {
        return QPixmap();
    }

    return roundedThumbnail(img, targetSize);
}
