#ifndef ARCHPAPER_EXPORTDIALOG_H
#define ARCHPAPER_EXPORTDIALOG_H

#include <QDialog>
#include "archpaper/export.h"

class QLabel;
class QTreeWidget;
class QProgressBar;
class QPushButton;
class QThread;

class ExportDialog : public QDialog {
public:
    explicit ExportDialog(const QString &project, QWidget *parent = nullptr);
    ~ExportDialog() override;
    void reject() override;

private:
    void scan(const QString &project);
    void populate();
    void exportSelected();
    void setBusy(bool busy);
    void cancel();

    ap_resource_list m_resources = {};
    QThread *m_worker = nullptr;
    QTreeWidget *m_list;
    QLabel *m_status;
    QProgressBar *m_progress;
    QPushButton *m_export;
    QPushButton *m_cancel;
    QPushButton *m_all;
    QPushButton *m_none;
    bool m_closeRequested = false;
};

#endif
