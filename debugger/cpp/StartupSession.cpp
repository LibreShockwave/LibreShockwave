#include "StartupSession.hpp"

#include <QFileInfo>

namespace libreshockwave::debugger {

namespace {

bool is_recording_path(const QString& path) {
    return QFileInfo(path).suffix().compare(QStringLiteral("lswdebug"),
                                            Qt::CaseInsensitive) == 0;
}

} // namespace

QString select_startup_movie_source(const QString& last_movie,
                                    const QString& last_session) {
    if (!last_movie.isEmpty() && !is_recording_path(last_movie)) {
        return last_movie;
    }
    if (!last_session.isEmpty() && !is_recording_path(last_session)) {
        return last_session;
    }
    return {};
}

} // namespace libreshockwave::debugger
