#include "mainwindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QSpinBox>
#include <QSplitter>
#include <QVBoxLayout>
#include <QImageReader>

#include <cstdlib>
#include <ctime>
#include <signal.h>
#include <unistd.h>

extern "C" {
#include "archpaper/backend.h"
#include "archpaper/config.h"
#include "archpaper/daemon.h"
#include "archpaper/utils.h"
#include "archpaper/wallust.h"
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setupUi();
    loadConfig();
}

MainWindow::~MainWindow() = default;

static void applyGlobalStyle() {
    qApp->setStyleSheet(R"(
        QMainWindow {
            background-color: #0f0f12;
        }
        QWidget {
            color: #e4e4e7;
            font-family: 'Inter', 'Segoe UI', 'Noto Sans', 'Cantarell', sans-serif;
            font-size: 13px;
        }
        QListWidget {
            background-color: #18181b;
            border: 1px solid #27272a;
            border-radius: 10px;
            padding: 8px;
            outline: none;
        }
        QListWidget::item {
            background: transparent;
            border-radius: 8px;
            padding: 8px;
            margin: 2px;
            color: #e4e4e7;
        }
        QListWidget::item:selected {
            background: #3b82f6;
            color: #ffffff;
        }
        QListWidget::item:hover {
            background: #27272a;
        }
        QLabel {
            color: #e4e4e7;
        }
        QPushButton {
            background-color: #3b82f6;
            color: #ffffff;
            border: none;
            border-radius: 8px;
            padding: 8px 16px;
            font-weight: 600;
            min-height: 32px;
        }
        QPushButton:hover {
            background-color: #60a5fa;
        }
        QPushButton:pressed {
            background-color: #2563eb;
        }
        QPushButton:checked {
            background-color: #ef4444;
        }
        QPushButton:disabled {
            background-color: #3f3f46;
            color: #a1a1aa;
        }
        QComboBox, QSpinBox, QLineEdit {
            background-color: #18181b;
            border: 1px solid #3f3f46;
            border-radius: 8px;
            padding: 6px 10px;
            color: #e4e4e7;
            min-height: 28px;
        }
        QComboBox::drop-down {
            border: none;
            width: 24px;
        }
        QComboBox QAbstractItemView {
            background-color: #18181b;
            border: 1px solid #3f3f46;
            selection-background-color: #3b82f6;
        }
        QGroupBox {
            border: 1px solid #27272a;
            border-radius: 12px;
            margin-top: 14px;
            padding-top: 14px;
            font-weight: 700;
            color: #e4e4e7;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 12px;
            padding: 0 8px;
        }
        QSplitter::handle {
            background: #27272a;
            border-radius: 2px;
        }
        QSplitter::handle:horizontal {
            width: 4px;
            margin: 4px 2px;
        }
        QSplitter::handle:vertical {
            height: 4px;
            margin: 2px 4px;
        }
    )");
}

void MainWindow::setupUi() {
    setWindowTitle("archpaper");
    resize(1350, 800);

    applyGlobalStyle();

    auto *central = new QWidget;
    setCentralWidget(central);

    auto *mainLayout = new QHBoxLayout(central);
    mainLayout->setContentsMargins(14, 14, 14, 14);
    mainLayout->setSpacing(12);

    /* --- Left panel: folders --- */
    auto *foldersTitle = new QLabel("<b style='font-size:15px;'>Folders</b>");

    foldersList = new QListWidget;
    foldersList->setMinimumWidth(220);
    foldersList->setMaximumWidth(320);
    foldersList->setContextMenuPolicy(Qt::NoContextMenu);
    connect(foldersList, &QListWidget::itemSelectionChanged, this, &MainWindow::onFolderSelected);

    addFolderButton = new QPushButton("+ Add");
    removeFolderButton = new QPushButton("- Remove");
    addFolderButton->setToolTip("Add a wallpaper folder");
    removeFolderButton->setToolTip("Remove the selected folder from favorites");
    connect(addFolderButton, &QPushButton::clicked, this, &MainWindow::onAddFolder);
    connect(removeFolderButton, &QPushButton::clicked, this, &MainWindow::onRemoveFolder);

    auto *folderButtonsLayout = new QHBoxLayout;
    folderButtonsLayout->addWidget(addFolderButton);
    folderButtonsLayout->addWidget(removeFolderButton);

    auto *leftLayout = new QVBoxLayout;
    leftLayout->setSpacing(10);
    leftLayout->addWidget(foldersTitle);
    leftLayout->addWidget(foldersList);
    leftLayout->addLayout(folderButtonsLayout);
    auto *leftPanel = new QWidget;
    leftPanel->setLayout(leftLayout);

    /* --- Center panel: images --- */
    auto *imagesTitle = new QLabel("<b style='font-size:15px;'>Wallpapers</b>");

    filterEdit = new QLineEdit;
    filterEdit->setPlaceholderText("🔍  Filter by name...");
    filterEdit->setClearButtonEnabled(true);
    connect(filterEdit, &QLineEdit::textChanged, this, &MainWindow::onFilterChanged);

    imagesList = new QListWidget;
    imagesList->setViewMode(QListView::IconMode);
    imagesList->setIconSize(QSize(180, 120));
    imagesList->setSpacing(12);
    imagesList->setWrapping(true);
    imagesList->setResizeMode(QListView::Adjust);
    imagesList->setSelectionMode(QAbstractItemView::SingleSelection);
    imagesList->setGridSize(QSize(200, 155));
    imagesList->setWordWrap(true);
    connect(imagesList, &QListWidget::itemSelectionChanged, this, &MainWindow::onImageSelected);
    connect(imagesList, &QListWidget::itemDoubleClicked, this, &MainWindow::onImageDoubleClicked);

    auto *centerLayout = new QVBoxLayout;
    centerLayout->setSpacing(10);
    centerLayout->addWidget(imagesTitle);
    centerLayout->addWidget(filterEdit);
    centerLayout->addWidget(imagesList);
    auto *centerPanel = new QWidget;
    centerPanel->setLayout(centerLayout);

    /* --- Right panel: preview and controls --- */
    auto *previewTitle = new QLabel("<b style='font-size:15px;'>Preview</b>");

    previewLabel = new QLabel("Select an image");
    previewLabel->setAlignment(Qt::AlignCenter);
    previewLabel->setStyleSheet("background-color: #18181b; color: #71717a; border: 1px solid #27272a; border-radius: 12px;");
    previewLabel->setMinimumHeight(320);
    previewLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    previewLabel->setScaledContents(false);

    backendCombo = new QComboBox;
    backendCombo->addItem("swaybg");
    backendCombo->addItem("hyprpaper");
    backendCombo->setToolTip("Backend used to apply the wallpaper");
    connect(backendCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onBackendChanged);

    modeCombo = new QComboBox;
    modeCombo->addItems({"fill", "fit", "stretch", "center", "tile"});
    modeCombo->setToolTip("Image scaling mode");
    connect(modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWindow::onModeChanged);

    auto *optionsLayout = new QHBoxLayout;
    optionsLayout->setSpacing(10);
    optionsLayout->addWidget(new QLabel("Backend:"));
    optionsLayout->addWidget(backendCombo, 1);
    optionsLayout->addSpacing(12);
    optionsLayout->addWidget(new QLabel("Mode:"));
    optionsLayout->addWidget(modeCombo, 1);

    wallustCheck = new QCheckBox("Generate scheme with wallust");
    wallustCheck->setToolTip("Run 'wallust run' after applying the wallpaper");
    connect(wallustCheck, &QCheckBox::toggled, this, &MainWindow::onWallustToggled);

    wallustHookEdit = new QLineEdit;
    wallustHookEdit->setPlaceholderText("Additional post-wallust hook (optional)");
    wallustHookEdit->setToolTip("Extra script after wallust; if empty only wallust.toml is used");
    connect(wallustHookEdit, &QLineEdit::editingFinished, this, &MainWindow::onWallustHookChanged);

    wallustHookBrowseButton = new QPushButton("Browse");
    wallustHookBrowseButton->setToolTip("Select post-wallust script");
    connect(wallustHookBrowseButton, &QPushButton::clicked, this, &MainWindow::onWallustHookBrowse);

    auto *wallustHookLayout = new QHBoxLayout;
    wallustHookLayout->setSpacing(10);
    wallustHookLayout->addWidget(new QLabel("Hook:"));
    wallustHookLayout->addWidget(wallustHookEdit, 1);
    wallustHookLayout->addWidget(wallustHookBrowseButton);

    applyButton = new QPushButton("⬇  Apply");
    randomButton = new QPushButton("🔀  Random");
    clearButton = new QPushButton("🗑  Clear");
    applyButton->setToolTip("Apply selected image as wallpaper");
    randomButton->setToolTip("Pick a random wallpaper from the current folder");
    clearButton->setToolTip("Remove the current wallpaper");
    connect(applyButton, &QPushButton::clicked, this, &MainWindow::onApply);
    connect(randomButton, &QPushButton::clicked, this, &MainWindow::onRandom);
    connect(clearButton, &QPushButton::clicked, this, &MainWindow::onClear);

    auto *buttonsLayout = new QHBoxLayout;
    buttonsLayout->setSpacing(10);
    buttonsLayout->addWidget(applyButton);
    buttonsLayout->addWidget(randomButton);
    buttonsLayout->addWidget(clearButton);

    intervalSpin = new QSpinBox;
    intervalSpin->setRange(10, 86400);
    intervalSpin->setValue(300);
    intervalSpin->setSuffix(" s");
    intervalSpin->setToolTip("Interval between automatic changes");

    daemonButton = new QPushButton("▶  Start daemon");
    daemonButton->setCheckable(true);
    daemonButton->setToolTip("Start/stop automatic wallpaper changes");
    connect(daemonButton, &QPushButton::toggled, this, &MainWindow::onDaemonToggle);

    auto *daemonLayout = new QHBoxLayout;
    daemonLayout->setSpacing(10);
    daemonLayout->addWidget(new QLabel("Interval:"));
    daemonLayout->addWidget(intervalSpin);
    daemonLayout->addStretch();
    daemonLayout->addWidget(daemonButton);

    auto *daemonGroup = new QGroupBox("Automatic change");
    daemonGroup->setLayout(daemonLayout);

    statusLabel = new QLabel("Ready");
    statusLabel->setStyleSheet("color: #a1a1aa; font-size: 12px;");

    auto *rightLayout = new QVBoxLayout;
    rightLayout->setSpacing(10);
    rightLayout->addWidget(previewTitle);
    rightLayout->addWidget(previewLabel, 1);
    rightLayout->addLayout(optionsLayout);
    rightLayout->addWidget(wallustCheck);
    rightLayout->addLayout(wallustHookLayout);
    rightLayout->addLayout(buttonsLayout);
    rightLayout->addWidget(daemonGroup);
    rightLayout->addWidget(statusLabel);
    auto *rightPanel = new QWidget;
    rightPanel->setLayout(rightLayout);

    /* --- Main splitter --- */
    auto *splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(leftPanel);
    splitter->addWidget(centerPanel);
    splitter->addWidget(rightPanel);
    splitter->setSizes({260, 520, 520});
    splitter->setHandleWidth(6);

    mainLayout->addWidget(splitter);
}

void MainWindow::loadConfig() {
    config_t cfg;
    config_load(&cfg);

    int backendIndex = (cfg.backend == BACKEND_HYPRPAPER) ? 1 : 0;
    backendCombo->setCurrentIndex(backendIndex);

    QString mode = QString::fromUtf8(cfg.mode);
    int modeIndex = modeCombo->findText(mode, Qt::MatchFixedString);
    if (modeIndex < 0) modeIndex = 0;
    modeCombo->setCurrentIndex(modeIndex);

    wallustCheck->setChecked(cfg.wallust_enabled != 0);
    if (wallust_available()) {
        wallustCheck->setToolTip("Run 'wallust run' after applying the wallpaper");
    } else {
        wallustCheck->setToolTip("wallust is not installed; it will activate once installed");
    }

    wallustHookEdit->setText(QString::fromUtf8(cfg.wallust_hook));

    loadFolders();

    if (cfg.last_wallpaper[0] != '\0') {
        updatePreview(QString::fromUtf8(cfg.last_wallpaper));
    }

    QString status = QString("Detected backend: %1").arg(backend_to_string(detect_backend()));
    if (!wallust_available()) {
        status += " | wallust not available";
    }
    updateStatus(status);
}

void MainWindow::saveCurrentConfig(const char *path) {
    config_t cfg;
    config_load(&cfg);

    cfg.backend = (currentBackendIndex() == 1) ? BACKEND_HYPRPAPER : BACKEND_SWAYBG;
    cfg.wallust_enabled = wallustCheck->isChecked() ? 1 : 0;

    QByteArray hook = wallustHookEdit->text().toUtf8();
    strncpy(cfg.wallust_hook, hook.constData(), sizeof(cfg.wallust_hook) - 1);
    cfg.wallust_hook[sizeof(cfg.wallust_hook) - 1] = '\0';

    QByteArray mode = modeCombo->currentText().toUtf8();
    strncpy(cfg.mode, mode.constData(), sizeof(cfg.mode) - 1);
    cfg.mode[sizeof(cfg.mode) - 1] = '\0';

    if (path) {
        strncpy(cfg.last_wallpaper, path, sizeof(cfg.last_wallpaper) - 1);
        cfg.last_wallpaper[sizeof(cfg.last_wallpaper) - 1] = '\0';
    }

    /* Keep current favorite folders. */
    for (int i = 0; i < foldersList->count(); i++) {
        QByteArray folder = foldersList->item(i)->text().toUtf8();
        config_add_folder(&cfg, folder.constData());
    }

    config_save(&cfg);
}

void MainWindow::loadFolders() {
    config_t cfg;
    config_load(&cfg);

    foldersList->clear();
    if (cfg.folder_count == 0) {
        QStringList defaults = {
            QDir::homePath() + "/Pictures",
            QDir::homePath() + "/Images",
            QDir::homePath() + "/wallpapers",
            "/usr/share/wallpapers"
        };
        for (const QString &path : defaults) {
            if (QDir(path).exists()) {
                foldersList->addItem(path);
            }
        }
        if (foldersList->count() == 0) {
            foldersList->addItem(QDir::homePath());
        }
    } else {
        for (int i = 0; i < cfg.folder_count; i++) {
            foldersList->addItem(QString::fromUtf8(cfg.folders[i]));
        }
    }

    if (foldersList->count() > 0) {
        foldersList->setCurrentRow(0);
    }
}

void MainWindow::onAddFolder() {
    QString dir = QFileDialog::getExistingDirectory(this, "Add wallpaper folder", QDir::homePath());
    if (dir.isEmpty()) return;

    for (int i = 0; i < foldersList->count(); i++) {
        if (foldersList->item(i)->text() == dir) {
            foldersList->setCurrentRow(i);
            return;
        }
    }

    foldersList->addItem(dir);
    foldersList->setCurrentRow(foldersList->count() - 1);

    config_t cfg;
    config_load(&cfg);
    config_add_folder(&cfg, dir.toUtf8().constData());
    config_save(&cfg);
}

void MainWindow::onRemoveFolder() {
    int row = foldersList->currentRow();
    if (row < 0) return;

    config_t cfg;
    config_load(&cfg);
    config_remove_folder(&cfg, row);
    config_save(&cfg);

    delete foldersList->takeItem(row);
}

void MainWindow::onFolderSelected() {
    QListWidgetItem *item = foldersList->currentItem();
    if (!item) return;

    currentFolder = item->text();
    loadImagesFromFolder(currentFolder);
}

static QPixmap createThumbnail(const QString &path, const QSize &targetSize) {
    QImageReader reader(path);
    if (!reader.canRead()) {
        return QPixmap();
    }

    QSize orig = reader.size();
    if (orig.isValid() && !orig.isNull()) {
        reader.setScaledSize(orig.scaled(targetSize, Qt::KeepAspectRatio));
    }

    QImage img = reader.read();
    if (img.isNull()) {
        return QPixmap();
    }

    QPixmap pixmap = QPixmap::fromImage(img);

    /* Add a subtle border. */
    QPixmap rounded(targetSize);
    rounded.fill(Qt::transparent);
    QPainter painter(&rounded);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QBrush(pixmap.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    painter.drawRoundedRect(QRect(QPoint(0, 0), targetSize), 8, 8);
    painter.end();

    return rounded;
}

void MainWindow::loadImagesFromFolder(const QString &folder) {
    imagesList->clear();
    currentFolder = folder;

    QDir dir(folder);
    QStringList filters;
    filters << "*.png" << "*.jpg" << "*.jpeg" << "*.webp" << "*.bmp" << "*.gif" << "*.tif" << "*.tiff";
    QFileInfoList files = dir.entryInfoList(filters, QDir::Files, QDir::Name);

    QApplication::setOverrideCursor(Qt::WaitCursor);

    for (const QFileInfo &info : files) {
        QString path = info.absoluteFilePath();

        QIcon icon;
        QPixmap thumb = createThumbnail(path, QSize(180, 120));
        if (!thumb.isNull()) {
            icon = QIcon(thumb);
        } else {
            icon = QIcon::fromTheme("image-x-generic");
        }

        auto *item = new QListWidgetItem(icon, info.fileName());
        item->setData(Qt::UserRole, path);
        item->setToolTip(path);
        item->setTextAlignment(Qt::AlignHCenter | Qt::AlignBottom);
        imagesList->addItem(item);
    }

    QApplication::restoreOverrideCursor();

    updateStatus(QString("%1 images in %2").arg(imagesList->count()).arg(folder));
    onFilterChanged(filterEdit->text());
}

void MainWindow::onImageSelected() {
    QListWidgetItem *item = imagesList->currentItem();
    if (!item) return;

    QString path = item->data(Qt::UserRole).toString();
    updatePreview(path);
}

void MainWindow::onImageDoubleClicked(QListWidgetItem *item) {
    if (!item) return;
    QString path = item->data(Qt::UserRole).toString();
    updatePreview(path);
    applySelectedImage(path);
}

void MainWindow::onFilterChanged(const QString &text) {
    for (int i = 0; i < imagesList->count(); i++) {
        auto *item = imagesList->item(i);
        bool match = text.isEmpty() || item->text().contains(text, Qt::CaseInsensitive);
        item->setHidden(!match);
    }

    if (!text.isEmpty()) {
        int visible = 0;
        for (int i = 0; i < imagesList->count(); i++) {
            if (!imagesList->item(i)->isHidden()) visible++;
        }
        updateStatus(QString("%1 of %2 images match").arg(visible).arg(imagesList->count()));
    } else {
        updateStatus(QString("%1 images in %2").arg(imagesList->count()).arg(currentFolder));
    }
}

void MainWindow::updatePreview(const QString &path) {
    QImageReader reader(path);
    if (!reader.canRead()) {
        previewLabel->setText("Could not load preview");
        previewLabel->setPixmap(QPixmap());
        return;
    }

    QSize orig = reader.size();
    if (orig.isValid() && !orig.isNull()) {
        QSize previewSize = previewLabel->size() - QSize(28, 28);
        previewSize = previewSize.boundedTo(QSize(2560, 1600));
        reader.setScaledSize(orig.scaled(previewSize, Qt::KeepAspectRatio));
    }

    QImage img = reader.read();
    if (img.isNull()) {
        previewLabel->setText("Could not load preview");
        previewLabel->setPixmap(QPixmap());
        return;
    }

    QPixmap pix = QPixmap::fromImage(img);
    int radius = 12;
    QPixmap rounded(pix.size());
    rounded.fill(Qt::transparent);
    QPainter painter(&rounded);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QBrush(pix));
    painter.drawRoundedRect(pix.rect(), radius, radius);
    painter.end();

    previewLabel->setPixmap(rounded);
    previewLabel->setText("");
}

void MainWindow::onBackendChanged(int index) {
    (void)index;
    int idx = currentBackendIndex();
    backend_t b = (idx == 1) ? BACKEND_HYPRPAPER : BACKEND_SWAYBG;
    if (!backend_available(b)) {
        updateStatus(QString("Backend '%1' not available").arg(backend_to_string(b)));
    }
}

void MainWindow::onModeChanged(int index) {
    (void)index;
}

void MainWindow::onWallustToggled(bool checked) {
    (void)checked;
    saveCurrentConfig(nullptr);
}

void MainWindow::onWallustHookChanged() {
    saveCurrentConfig(nullptr);
}

void MainWindow::onWallustHookBrowse() {
    QString file = QFileDialog::getOpenFileName(this,
                                                "Select wallust hook",
                                                QDir::homePath(),
                                                "Scripts (*.sh);;All files (*)");
    if (!file.isEmpty()) {
        wallustHookEdit->setText(file);
        saveCurrentConfig(nullptr);
    }
}

int MainWindow::currentBackendIndex() const {
    return backendCombo->currentIndex();
}

void MainWindow::updateStatus(const QString &msg) {
    statusLabel->setText(msg);
}

void MainWindow::onApply() {
    QListWidgetItem *item = imagesList->currentItem();
    if (!item) {
        updateStatus("Select an image first");
        return;
    }

    QString path = item->data(Qt::UserRole).toString();
    applySelectedImage(path);
}

void MainWindow::applySelectedImage(const QString &path) {
    if (!QFile::exists(path)) {
        updateStatus("The image does not exist");
        return;
    }

    backend_t b = (currentBackendIndex() == 1) ? BACKEND_HYPRPAPER : BACKEND_SWAYBG;
    if (!backend_available(b)) {
        updateStatus(QString("Backend not available: %1").arg(backend_to_string(b)));
        return;
    }

    QByteArray mode = modeCombo->currentText().toUtf8();
    if (set_wallpaper(b, path.toUtf8().constData(), mode.constData()) != 0) {
        updateStatus("Error applying wallpaper");
        return;
    }

    saveCurrentConfig(path.toUtf8().constData());

    if (wallustCheck->isChecked()) {
        if (wallust_available()) {
            wallust_run(path.toUtf8().constData());
        }
        config_t cfg;
        config_load(&cfg);
        wallust_hook_run(cfg.wallust_hook, path.toUtf8().constData());
        if (wallust_available()) {
            updateStatus(QString("Applied with wallust: %1").arg(QFileInfo(path).fileName()));
        } else {
            updateStatus(QString("Applied; wallust not available: %1").arg(QFileInfo(path).fileName()));
        }
    } else {
        updateStatus(QString("Applied: %1").arg(QFileInfo(path).fileName()));
    }
}

void MainWindow::onRandom() {
    if (imagesList->count() == 0) {
        updateStatus("No images available");
        return;
    }

    int visibleCount = 0;
    for (int i = 0; i < imagesList->count(); i++) {
        if (!imagesList->item(i)->isHidden()) visibleCount++;
    }
    if (visibleCount == 0) {
        updateStatus("No images match the filter");
        return;
    }

    std::srand(static_cast<unsigned>(std::time(nullptr)));
    int idx;
    do {
        idx = std::rand() % imagesList->count();
    } while (imagesList->item(idx)->isHidden());

    imagesList->setCurrentRow(idx);
    onImageSelected();
    QString path = imagesList->item(idx)->data(Qt::UserRole).toString();
    applySelectedImage(path);
    updateStatus(QString("Random: %1").arg(imagesList->item(idx)->text()));
}

void MainWindow::onClear() {
    clear_wallpaper();
    updateStatus("Wallpaper cleared");
}

bool MainWindow::readDaemonPid(int *pid) {
    FILE *f = fopen("/tmp/archpaper.pid", "r");
    if (!f) return false;
    int p;
    bool ok = (fscanf(f, "%d", &p) == 1);
    fclose(f);
    if (ok && pid) *pid = p;
    return ok;
}

void MainWindow::onDaemonToggle(bool checked) {
    if (checked) {
        if (currentFolder.isEmpty() || !QDir(currentFolder).exists()) {
            QMessageBox::warning(this, "Daemon", "Select a valid folder first.");
            daemonButton->setChecked(false);
            return;
        }

        backend_t b = (currentBackendIndex() == 1) ? BACKEND_HYPRPAPER : BACKEND_SWAYBG;
        QByteArray mode = modeCombo->currentText().toUtf8();
        int interval = intervalSpin->value();

        int enable_wallust = wallustCheck->isChecked() ? 1 : 0;
        config_t cfg;
        config_load(&cfg);
        if (daemonize_random(currentFolder.toUtf8().constData(), interval, b, mode.constData(),
                             enable_wallust, cfg.wallust_hook) != 0) {
            QMessageBox::critical(this, "Daemon", "Could not start daemon.");
            daemonButton->setChecked(false);
            return;
        }

        daemonButton->setText("⏹  Stop daemon");
        updateStatus(QString("Daemon started (%1s)").arg(interval));
        daemonRunning = true;
    } else {
        int pid = 0;
        if (readDaemonPid(&pid)) {
            if (kill(pid, SIGTERM) == 0) {
                updateStatus("Daemon stopped");
            } else {
                updateStatus("Could not stop daemon");
            }
        } else {
            updateStatus("No active daemon");
        }
        daemonButton->setText("▶  Start daemon");
        daemonRunning = false;
    }
}

void MainWindow::resizeEvent(QResizeEvent *event) {
    QMainWindow::resizeEvent(event);
    QListWidgetItem *item = imagesList->currentItem();
    if (item) {
        updatePreview(item->data(Qt::UserRole).toString());
    }
}
