#include "mainwindow.h"
#include <QApplication>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    app.setApplicationName("UDoc Editor");
    app.setOrganizationName("UDocProject");

    // FIX: Don't use namespace prefix here since we include the header
    UDoc::MainWindow window;
    window.show();

    return app.exec();
}
