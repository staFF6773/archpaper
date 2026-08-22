#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

QT_BEGIN_NAMESPACE
class QListWidget;
class QListWidgetItem;
class QLabel;
class QComboBox;
class QSpinBox;
class QPushButton;
class QLineEdit;
class QSplitter;
class QCheckBox;
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

private slots:
    void onAddFolder();
    void onRemoveFolder();
    void onFolderSelected();
    void onImageSelected();
    void onImageDoubleClicked(QListWidgetItem *item);
    void onFilterChanged(const QString &text);
    void onApply();
    void onRandom();
    void onClear();
    void onDaemonToggle(bool checked);
    void onBackendChanged(int index);
    void onModeChanged(int index);
    void onWallustToggled(bool checked);
    void onWallustHookChanged();
    void onWallustHookBrowse();

private:
    void setupUi();
    void loadConfig();
    void saveCurrentConfig(const char *path);
    void loadFolders();
    void loadImagesFromFolder(const QString &folder);
    void applySelectedImage(const QString &path);
    void updatePreview(const QString &path);
    void updateStatus(const QString &msg);
    bool readDaemonPid(int *pid);
    int currentBackendIndex() const;

    void resizeEvent(QResizeEvent *event) override;

    QListWidget *foldersList;
    QListWidget *imagesList;
    QLabel *previewLabel;
    QLabel *statusLabel;
    QLineEdit *filterEdit;
    QComboBox *backendCombo;
    QComboBox *modeCombo;
    QSpinBox *intervalSpin;
    QPushButton *addFolderButton;
    QPushButton *removeFolderButton;
    QPushButton *applyButton;
    QPushButton *randomButton;
    QPushButton *clearButton;
    QPushButton *daemonButton;
    QCheckBox *wallustCheck;
    QLineEdit *wallustHookEdit;
    QPushButton *wallustHookBrowseButton;

    QString currentFolder;
    bool daemonRunning = false;
};

#endif
