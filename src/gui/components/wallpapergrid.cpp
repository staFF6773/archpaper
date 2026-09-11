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
#include <QShortcut>
#include <QVBoxLayout>

#include "archpaper/cache.h"
#include "archpaper/library.h"
extern "C" {
#include "archpaper/utils.h"
}

namespace {

constexpr int THUMB_WIDTH = 180;
constexpr int THUMB_HEIGHT = 101;

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

}

WallpaperGrid::WallpaperGrid(QWidget *parent)
    : QFrame(parent)
{
    setupUi();
}

void WallpaperGrid::setupUi() {
    setObjectName("wallpaperGrid");
    setMinimumWidth(210);

    m_filter = new QLineEdit(this);
    m_filter->setObjectName("internalFilter");
    m_filter->hide();
    connect(m_filter, &QLineEdit::textChanged, this, &WallpaperGrid::onFilterTextChanged);

    m_list = new QListWidget(this);
    m_list->setObjectName("imagesList");
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(THUMB_WIDTH, THUMB_HEIGHT));
    m_list->setSpacing(4);
    m_list->setMovement(QListView::Static);
    m_list->setTextElideMode(Qt::ElideMiddle);
    m_list->setAccessibleName("Wallpapers");
    m_list->setWrapping(true);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setGridSize(QSize(THUMB_WIDTH + 20, THUMB_HEIGHT + 44));
    m_list->setWordWrap(false);
    m_list->setUniformItemSizes(true);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->viewport()->installEventFilter(this);

    connect(m_list, &QListWidget::itemSelectionChanged, this, &WallpaperGrid::onSelectionChanged);
    connect(m_list, &QListWidget::itemDoubleClicked, this, &WallpaperGrid::onItemDoubleClicked);
    for (const auto key : {Qt::Key_Return, Qt::Key_Enter}) {
        auto *shortcut = new QShortcut(QKeySequence(key), m_list);
        shortcut->setContext(Qt::WidgetShortcut);
        connect(shortcut, &QShortcut::activated, this, [this]() {
            if (!selectedPath().isEmpty())
                emit imageDoubleClicked(selectedPath());
        });
    }

    m_emptyLabel = new QLabel("No wallpapers yet\nAdd a folder using + in the sidebar");
    m_emptyLabel->setObjectName("emptyState");
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_list, 1);
    layout->addWidget(m_emptyLabel, 1);
    m_list->hide();
}

bool WallpaperGrid::eventFilter(QObject *watched, QEvent *event) {
    if (watched == m_list->viewport() && event->type() == QEvent::Resize) {
        const int width = qMax(1, m_list->viewport()->width() - 2 * m_list->spacing());
        const int columns = qMax(1, qMin(qRound(width / 200.0), width / 160));
        const int cellWidth = width / columns;
        const int thumbWidth = qBound(100, cellWidth - 20, THUMB_WIDTH);
        const int thumbHeight = thumbWidth * THUMB_HEIGHT / THUMB_WIDTH;
        m_list->setIconSize(QSize(thumbWidth, thumbHeight));
        m_list->setGridSize(QSize(cellWidth, thumbHeight + 44));
    }
    return QFrame::eventFilter(watched, event);
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

    ap_path_list files = {};
    const ap_result rc = ap_library_scan(folder.toUtf8().constData(), &files);

    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (size_t i = 0; i < files.count; ++i) {
        addWallpaper(QString::fromUtf8(files.paths[i]));
    }
    ap_path_list_free(&files);
    QApplication::restoreOverrideCursor();

    refreshFilter();
    if (rc != AP_OK) emit errorOccurred(QString::fromUtf8(ap_error_string(rc)));
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
    return item && item->isSelected() && !item->isHidden()
               ? item->data(Qt::UserRole).toString() : QString();
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

    size_t selected;
    if (ap_random_index(static_cast<size_t>(visible), &selected) != AP_OK) return;
    for (int i = 0; i < m_list->count(); ++i) {
        if (m_list->item(i)->isHidden()) continue;
        if (selected-- == 0) { m_list->setCurrentRow(i); return; }
    }
}

void WallpaperGrid::setFilter(const QString &text) {
    m_filter->setText(text);
}

void WallpaperGrid::refreshFilter() {
    onFilterTextChanged(m_filter->text());
}

void WallpaperGrid::onSelectionChanged() {
    QString path = selectedPath();
    emit imageSelected(path);
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
        if (!match && item->isSelected())
            m_list->setCurrentItem(nullptr);
        if (match) ++visible;
    }
    m_list->setVisible(visible > 0);
    m_emptyLabel->setVisible(visible == 0);
    m_emptyLabel->setText(text.isEmpty()
        ? "No wallpapers here\nChoose another folder or add one using +"
        : "No results\nTry a different name or clear the search");
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
    clip.addRoundedRect(outerRect, 6, 6);

    painter.setPen(Qt::NoPen);
    painter.setClipPath(clip);

    /* Center the scaled image inside the rounded rect without stretching. */
    QRect imageRect(QPoint(0, 0), scaled.size());
    imageRect.moveCenter(outerRect.center());
    painter.drawPixmap(imageRect, scaled);
    painter.setClipping(false);

    QPen border(QColor(42, 42, 42), 1);
    painter.setPen(border);
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(outerRect, 6, 6);
    painter.end();

    return canvas;
}

QPixmap WallpaperGrid::createThumbnail(const QString &path, const QSize &targetSize) {
    if (isVideo(path)) {
        QTemporaryFile tmp(QDir::tempPath() + "/archpaper_video_thumb_XXXXXX.png");
        tmp.setAutoRemove(true);
        if (tmp.open()) {
            QString tmpPath = tmp.fileName();
            tmp.close();
            if (ap_thumbnail_extract(path.toUtf8().constData(), tmpPath.toUtf8().constData()) == AP_OK) {
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
