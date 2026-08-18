#pragma once

#include <QString>

namespace libreshockwave::debugger {

// Selects the movie source to restore at startup. Recording files require an
// explicit open or playback request and must not become the default movie.
QString select_startup_movie_source(const QString& last_movie,
                                    const QString& last_session);

} // namespace libreshockwave::debugger
