#include <QApplication>
#include <QCoreApplication>
#include <QTimer>
#include <QString>

#include "MainWindow.hpp"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("LibreShockwave"));
    QCoreApplication::setApplicationName(QStringLiteral("LibreShockwave Editor"));

    libreshockwave::editor::MainWindow window;
    window.show();

    if (argc > 1) {
        window.openInitialPath(QString::fromLocal8Bit(argv[1]));
    } else {
        QTimer::singleShot(0, &window, [&window] { window.showStartScreen(); });
    }

    return app.exec();
}
