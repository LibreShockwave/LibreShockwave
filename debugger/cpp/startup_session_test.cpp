#include "StartupSession.hpp"

#include <cassert>

using libreshockwave::debugger::select_startup_movie_source;

int main() {
    assert(select_startup_movie_source(QStringLiteral("movie.dir"),
                                       QStringLiteral("session.lswdebug")) ==
           QStringLiteral("movie.dir"));
    assert(select_startup_movie_source(QString(), QStringLiteral("movie.dcr")) ==
           QStringLiteral("movie.dcr"));
    assert(select_startup_movie_source(QStringLiteral("session.lswdebug"),
                                       QStringLiteral("other.lswdebug"))
               .isEmpty());
    assert(select_startup_movie_source(QStringLiteral("SESSION.LSWDEBUG"),
                                       QStringLiteral("movie.cst")) ==
           QStringLiteral("movie.cst"));
    return 0;
}
