#include "startup_session.hpp"

#include <QFileInfo>

namespace libreshockwave::debugger {

namespace {

bool isRecordingPath(const QString& path) {
    return QFileInfo(path).suffix().compare(QStringLiteral("lswdebug"),
                                           Qt::CaseInsensitive) == 0;
}

} // namespace

QString selectStartupMovieSource(const QString& last_movie,
                                 const QString& last_session) {
    if (!last_movie.isEmpty() && !isRecordingPath(last_movie)) {
        return last_movie;
    }
    if (!last_session.isEmpty() && !isRecordingPath(last_session)) {
        return last_session;
    }
    return {};
}

} // namespace libreshockwave::debugger
