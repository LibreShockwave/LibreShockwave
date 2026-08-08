#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDebug>

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

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("LibreShockwave desktop debugger and input recording replay tool"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption playOption(
        {QStringLiteral("p"), QStringLiteral("play")},
        QStringLiteral("start playback after loading the movie (or replay a recording)"));
    parser.addOption(playOption);
    parser.addPositionalArgument(
        QStringLiteral("movie-or-recording"),
        QStringLiteral("Director movie or .lswdebug recording to open"));
    parser.process(app);

    const QStringList positional = parser.positionalArguments();
    if (positional.size() > 1) {
        qCritical().noquote() << QStringLiteral("Only one movie or recording may be specified");
        return 2;
    }

    libreshockwave::debugger::DebuggerWindow window;
    window.show();

    if (!positional.isEmpty() && !window.openMovie(positional.front())) {
        return 1;
    }
    if (parser.isSet(playOption)) {
        window.startPlayback();
    }

    return app.exec();
}
