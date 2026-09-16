#include "components/exportdialog.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QLabel>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeWidget>
#include <cstdio>
#include <cstdlib>

#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); std::exit(1); } } while (0)

static void writeFile(const QString &path, const QByteArray &bytes) {
    QFile file(path); CHECK(file.open(QIODevice::WriteOnly)); CHECK(file.write(bytes) == bytes.size());
}
static QPushButton *button(ExportDialog &dialog, const QString &text) {
    for (auto *b : dialog.findChildren<QPushButton *>()) if (b->text() == text) return b;
    CHECK(false); return nullptr;
}

int main(int argc, char **argv) {
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    QTemporaryDir temp; CHECK(temp.isValid());
    QString project = temp.path() + "/project", destination = temp.path() + "/output";
    CHECK(QDir().mkpath(project) && QDir().mkpath(destination));
    writeFile(project + "/project.json", "{\"type\":\"video\",\"file\":\"clip.mp4\",\"preview\":\"preview.png\"}");
    writeFile(project + "/clip.mp4", "video bytes");
    writeFile(project + "/preview.png", "preview bytes");

    ExportDialog dialog(project);
    QTimer timeout; timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, []() { CHECK(false); }); timeout.start(8000);
    QTimer poll; poll.setInterval(10);
    int phase = 0;
    QObject::connect(&poll, &QTimer::timeout, [&]() {
        if (phase == 0) {
            auto *tree = dialog.findChild<QTreeWidget *>(); CHECK(tree);
            if (tree->topLevelItemCount() != 2) return;
            int checked = 0;
            for (int i = 0; i < 2; ++i) checked += tree->topLevelItem(i)->checkState(0) == Qt::Checked;
            CHECK(checked == 1); /* Preview must not be silently exported by default. */
            button(dialog, "Select none")->click();
            for (int i = 0; i < 2; ++i) CHECK(tree->topLevelItem(i)->checkState(0) == Qt::Unchecked);
            button(dialog, "Select all")->click();
            for (int i = 0; i < 2; ++i) CHECK(tree->topLevelItem(i)->checkState(0) == Qt::Checked);
            phase = 1;
            QTimer::singleShot(0, [&]() { button(dialog, "Export selected…")->click(); });
        } else if (phase == 1) {
            auto *chooser = dialog.findChild<QFileDialog *>();
            if (!chooser || !chooser->isVisible()) return;
            chooser->setDirectory(destination);
            phase = 2;
            CHECK(QMetaObject::invokeMethod(chooser, "accept", Qt::QueuedConnection));
        } else if (phase == 2) {
            if (!button(dialog, "Export selected…")->isEnabled()) return;
            if (!QFile::exists(destination + "/clip.mp4")) return;
            CHECK(QFile::exists(destination + "/preview.png"));
            CHECK(QDir(destination).entryList(QDir::Files | QDir::Hidden).size() == 2);
            QFile video(destination + "/clip.mp4"); CHECK(video.open(QIODevice::ReadOnly) && video.readAll() == "video bytes");
            phase = 3; dialog.reject();
        }
    });
    poll.start(); dialog.exec(); CHECK(phase == 3); poll.stop();
    /* Closing before scan completion joins the worker; destruction is also safe. */
    { ExportDialog closing(project); closing.reject(); closing.exec(); }
    { ExportDialog destroyed(project); }
    std::puts("Export dialog tests passed");
    return 0;
}
