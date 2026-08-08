#include <QApplication>

#include "DebuggerWindow.hpp"
#include "model/DebuggerModel.hpp"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setOrganizationName(QStringLiteral("LibreShockwave"));
    app.setApplicationName(QStringLiteral("LibreShockwave Debugger"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));

    // Register the metatypes used by cross-thread queued signals
    // (DebugStateBridge emits paused(SnapshotData), DebuggerContext emits
    // castLoaded(MovieTreeSnapshot) from the worker thread).
    qRegisterMetaType<libreshockwave::debugger::SnapshotData>("SnapshotData");
    qRegisterMetaType<libreshockwave::debugger::MovieTreeSnapshot>("MovieTreeSnapshot");

    libreshockwave::debugger::DebuggerWindow window;
    window.show();

    // If a file path was passed on the command line, open it
    if (argc > 1) {
        window.openMovie(QString::fromLocal8Bit(argv[1]));
    }

    return app.exec();
}
