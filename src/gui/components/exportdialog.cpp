#include "exportdialog.h"

#include <QFileDialog>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QStandardPaths>
#include <QThread>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <memory>
#include <vector>

#include "archpaper/process.h"

namespace {
void enableCancellation() {
    ap_process_set_cancel_check([](void *) -> int {
        return QThread::currentThread()->isInterruptionRequested();
    }, nullptr);
}

QString resourceError(ap_result rc) {
    if (rc == AP_UNSUPPORTED) return "Unsupported TEX variant (animation, encoding or size)";
    if (rc == AP_INVALID) return "Invalid or truncated resource";
    if (rc == AP_BUSY) return "Source changed or destination unavailable; reopen the resource list and retry";
    return QString::fromUtf8(ap_error_string(rc));
}
}

ExportDialog::ExportDialog(const QString &project, QWidget *parent) : QDialog(parent) {
    setWindowTitle("Export Wallpaper Engine resources");
    resize(760, 520);
    auto *layout = new QVBoxLayout(this);
    auto *description = new QLabel("Choose original images and videos to save. Scene effects and scripts are not included. "
                                  "Static TEX textures are decoded to PNG; embedded media keeps its original format.");
    description->setWordWrap(true);
    layout->addWidget(description);
    m_list = new QTreeWidget;
    m_list->setHeaderLabels({"Resource", "Export format", "Source size", "Location"});
    m_list->setRootIsDecorated(false);
    m_list->setAlternatingRowColors(true);
    m_list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_list->setColumnWidth(1, 125);
    m_list->setColumnWidth(2, 105);
    layout->addWidget(m_list, 1);
    auto *selection = new QHBoxLayout;
    m_all = new QPushButton("Select all");
    m_none = new QPushButton("Select none");
    selection->addWidget(m_all); selection->addWidget(m_none); selection->addStretch();
    layout->addLayout(selection);
    auto select = [this](Qt::CheckState state) {
        for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
            auto *item = m_list->topLevelItem(i);
            if (item->flags() & Qt::ItemIsUserCheckable) item->setCheckState(0, state);
        }
    };
    connect(m_all, &QPushButton::clicked, this, [select]() { select(Qt::Checked); });
    connect(m_none, &QPushButton::clicked, this, [select]() { select(Qt::Unchecked); });
    m_status = new QLabel("Reading project resources…");
    m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::PlainText);
    layout->addWidget(m_status);
    m_progress = new QProgressBar;
    layout->addWidget(m_progress);
    auto *buttons = new QHBoxLayout;
    auto *close = new QPushButton("Close");
    m_cancel = new QPushButton("Cancel task");
    m_export = new QPushButton("Export selected…");
    m_export->setObjectName("primaryButton");
    buttons->addWidget(close); buttons->addStretch(); buttons->addWidget(m_cancel); buttons->addWidget(m_export);
    layout->addLayout(buttons);
    connect(close, &QPushButton::clicked, this, &ExportDialog::reject);
    connect(m_cancel, &QPushButton::clicked, this, &ExportDialog::cancel);
    connect(m_export, &QPushButton::clicked, this, &ExportDialog::exportSelected);
    scan(project);
}

ExportDialog::~ExportDialog() {
    if (m_worker) {
        m_worker->disconnect(this);
        m_worker->requestInterruption();
        m_worker->wait();
        delete m_worker;
    }
    ap_resource_list_free(&m_resources);
}

void ExportDialog::reject() {
    if (m_worker) { m_closeRequested = true; cancel(); }
    else QDialog::reject();
}

void ExportDialog::cancel() {
    if (!m_worker) return;
    m_worker->requestInterruption();
    m_cancel->setEnabled(false);
    m_status->setText("Cancelling… Completed exports will be kept.");
}

void ExportDialog::setBusy(bool busy) {
    m_list->setEnabled(!busy);
    m_all->setEnabled(!busy && m_resources.count);
    m_none->setEnabled(!busy && m_resources.count);
    m_export->setEnabled(!busy && m_resources.count);
    m_cancel->setEnabled(busy);
}

void ExportDialog::scan(const QString &project) {
    auto result = std::make_shared<ap_result>(AP_OK);
    m_progress->setRange(0, 0);
    setBusy(true);
    m_worker = QThread::create([this, project, result]() {
        enableCancellation();
        *result = ap_resources_read(project.toUtf8().constData(), &m_resources);
        ap_process_set_cancel_check(nullptr, nullptr);
    });
    connect(m_worker, &QThread::finished, this, [this, result]() {
        m_worker->wait();
        m_worker->deleteLater(); m_worker = nullptr;
        m_progress->setRange(0, 1); m_progress->setValue(0);
        setBusy(false);
        if (m_closeRequested) { QDialog::reject(); return; }
        if (*result != AP_OK) { m_status->setText("Could not read resources: " + resourceError(*result)); return; }
        populate();
    });
    m_worker->start();
}

void ExportDialog::populate() {
    size_t available = 0;
    for (size_t i = 0; i < m_resources.count; ++i) {
        const auto &r = m_resources.items[i];
        auto *item = new QTreeWidgetItem(m_list);
        QString name = QString::fromUtf8(r.name);
        if (r.preview) name += " (project preview)";
        item->setText(0, name);
        item->setToolTip(0, QString::fromUtf8(r.name));
        item->setData(0, Qt::UserRole, static_cast<qulonglong>(i));
        QString format = QString::fromLatin1(r.extension).toUpper();
        if (r.kind == AP_RESOURCE_TEXTURE) format.prepend("TEX → ");
        item->setText(1, r.support == AP_OK ? format : "Unavailable");
        item->setText(2, QLocale().formattedDataSize(static_cast<qint64>(r.size)));
        item->setText(3, r.packed ? "Package" : "Project folder");
        if (r.support == AP_OK) {
            ++available;
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(0, r.preview ? Qt::Unchecked : Qt::Checked);
        } else {
            item->setFlags(item->flags() & ~Qt::ItemIsUserCheckable & ~Qt::ItemIsEnabled);
            item->setToolTip(1, resourceError(r.support));
        }
    }
    m_export->setEnabled(available > 0);
    m_status->setText(m_resources.count ? QString("%1 exportable resources; %2 unsupported or invalid. "
        "Existing files are kept; name conflicts receive a numeric suffix.").arg(available).arg(m_resources.count - available)
        : "No image, video or TEX resources found in this project.");
}

void ExportDialog::exportSelected() {
    std::vector<size_t> selected;
    for (int i = 0; i < m_list->topLevelItemCount(); ++i) {
        auto *item = m_list->topLevelItem(i);
        if ((item->flags() & Qt::ItemIsUserCheckable) && item->checkState(0) == Qt::Checked)
            selected.push_back(static_cast<size_t>(item->data(0, Qt::UserRole).toULongLong()));
    }
    if (selected.empty()) { m_status->setText("Select at least one resource to export."); return; }
    QString directory = QFileDialog::getExistingDirectory(this, "Export resources to",
        QStandardPaths::writableLocation(QStandardPaths::PicturesLocation));
    if (directory.isEmpty()) return;
    struct Result { size_t saved = 0; bool cancelled = false; QStringList errors; };
    auto result = std::make_shared<Result>();
    m_progress->setRange(0, static_cast<int>(selected.size())); m_progress->setValue(0);
    setBusy(true);
    m_worker = QThread::create([this, selected, directory, result]() {
        enableCancellation();
        for (size_t i = 0; i < selected.size(); ++i) {
            if (ap_process_cancel_requested()) { result->cancelled = true; break; }
            const auto &r = m_resources.items[selected[i]];
            QString name = QString::fromUtf8(r.name);
            QMetaObject::invokeMethod(this, [this, name, i]() {
                m_status->setText("Exporting: " + name);
                m_progress->setValue(static_cast<int>(i));
            }, Qt::QueuedConnection);
            char output[4096];
            ap_result rc = ap_resource_export(&r, directory.toUtf8().constData(), output, sizeof(output));
            if (rc == AP_CANCELLED) { result->cancelled = true; break; }
            if (rc == AP_OK) ++result->saved;
            else result->errors.append(name + ": " + resourceError(rc));
        }
        ap_process_set_cancel_check(nullptr, nullptr);
    });
    connect(m_worker, &QThread::finished, this, [this, result, directory]() {
        m_worker->wait();
        m_worker->deleteLater(); m_worker = nullptr;
        setBusy(false);
        m_progress->setValue(static_cast<int>(result->saved + result->errors.size()));
        if (m_closeRequested) { QDialog::reject(); return; }
        m_status->setText(QString("%1%2 resources exported to %3. %4 failed.")
            .arg(result->cancelled ? "Cancelled. " : "").arg(result->saved).arg(directory).arg(result->errors.size()));
        if (!result->errors.isEmpty()) {
            QMessageBox box(QMessageBox::Warning, "Resource export", "Some resources could not be exported.", QMessageBox::Ok, this);
            box.setDetailedText(result->errors.join('\n'));
            box.exec();
        }
    });
    m_worker->start();
}
