#include <QApplication>
#include <QImageReader>
#include "mainwindow.h"

extern "C" {
#include "archpaper/cli.h"
}

int main(int argc, char *argv[]) {
    /* Si se pasan argumentos, actuamos como CLI (útil para autostart/scripts). */
    if (argc > 1) {
        return archpaper_cli(argc, argv);
    }

    QApplication app(argc, argv);
    app.setApplicationName("archpaper");
    app.setApplicationDisplayName("archpaper");

    /* Eliminar el límite de 256 MB de Qt para imágenes grandes. */
    QImageReader::setAllocationLimit(0);

    MainWindow window;
    window.show();

    return app.exec();
}
