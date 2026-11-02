#include "net/http_file_cache.hpp"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringList>

namespace libreshockwave::debugger {
namespace {

constexpr int kMaxPathSegmentLength = 120;
constexpr int kQueryHashLength = 16;
constexpr int kLongSegmentHashLength = 8;
constexpr int kDefaultHttpPort = 80;
constexpr int kDefaultHttpsPort = 443;
constexpr ushort kFirstPrintableCharacter = 0x20;

bool isCacheableUrl(const QUrl& url) {
    if (!url.isValid() || url.host().isEmpty()) {
        return false;
    }
    const QString scheme = url.scheme().toLower();
    return scheme == QStringLiteral("http") || scheme == QStringLiteral("https");
}

QString sanitizePathSegment(const QString& value) {
    QString result;
    result.reserve(value.size());
    const QString forbidden = QStringLiteral("<>:\"/\\|?*");
    for (const QChar ch : value) {
        if (ch.unicode() < kFirstPrintableCharacter || forbidden.contains(ch)) {
            result.push_back(QLatin1Char('_'));
        } else {
            result.push_back(ch);
        }
    }
    while (result.endsWith(QLatin1Char('.')) || result.endsWith(QLatin1Char(' '))) {
        result.chop(1);
    }
    if (result.isEmpty() || result == QStringLiteral(".") || result == QStringLiteral("..")) {
        return QStringLiteral("_");
    }
    if (result.size() > kMaxPathSegmentLength) {
        const QByteArray digest =
            QCryptographicHash::hash(result.toUtf8(), QCryptographicHash::Sha256)
                .toHex()
                .left(kLongSegmentHashLength);
        const QString suffix = QStringLiteral("_") + QString::fromLatin1(digest);
        result = result.left(kMaxPathSegmentLength - suffix.size()) + suffix;
    }
    return result;
}

QString authorityForUrl(const QUrl& url) {
    QString authority = sanitizePathSegment(url.host());
    const QString scheme = url.scheme().toLower();
    const int default_port = scheme == QStringLiteral("https") ? kDefaultHttpsPort
                                                               : kDefaultHttpPort;
    const int port = url.port();
    if (port > 0 && port != default_port) {
        authority += QStringLiteral("_%1").arg(port);
    }
    return authority;
}

QString queryHash(const QString& query) {
    const QByteArray digest = QCryptographicHash::hash(query.toUtf8(), QCryptographicHash::Sha256)
                                  .toHex()
                                  .left(kQueryHashLength);
    return QStringLiteral(".q") + QString::fromLatin1(digest);
}

} // namespace

HttpFileCache::HttpFileCache(QString root_path)
    : root_path_(root_path.isEmpty()
                     ? defaultRootPath()
                     : QDir::cleanPath(QDir(root_path).absolutePath())) {}

QString HttpFileCache::defaultRootPath() {
    return QDir::current().absoluteFilePath(QStringLiteral("_cache"));
}

const QString& HttpFileCache::rootPath() const {
    return root_path_;
}

QString HttpFileCache::pathForUrl(const QUrl& url) const {
    if (!isCacheableUrl(url)) {
        return {};
    }

    QStringList relative;
    relative.push_back(url.scheme().toLower());
    relative.push_back(authorityForUrl(url));

    const QString path = url.path(QUrl::FullyDecoded);
    const QStringList segments = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (const QString& segment : segments) {
        relative.push_back(sanitizePathSegment(segment));
    }

    QString file_name = QStringLiteral("index");
    if (!segments.isEmpty() && !path.endsWith(QLatin1Char('/'))) {
        file_name = relative.takeLast();
    }
    if (!url.query().isEmpty()) {
        file_name += queryHash(url.query());
    }
    relative.push_back(file_name);

    QString result = root_path_;
    for (const QString& segment : relative) {
        result = QDir(result).filePath(segment);
    }
    return result;
}

std::optional<QByteArray> HttpFileCache::read(const QUrl& url) const {
    const QString path = pathForUrl(url);
    if (path.isEmpty()) {
        return std::nullopt;
    }

    const QFileInfo info(path);
    if (!info.exists()) {
        return std::nullopt;
    }
    if (!info.isFile()) {
        qWarning().noquote() << "http cache: expected a file at"
                             << QDir::toNativeSeparators(path)
                             << "for" << url.toString();
        return std::nullopt;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning().noquote() << "http cache: cannot read" << QDir::toNativeSeparators(path)
                             << "for" << url.toString() << ":" << file.errorString();
        return std::nullopt;
    }

    QByteArray data = file.readAll();
    if (file.error() != QFileDevice::NoError) {
        qWarning().noquote() << "http cache: cannot read" << QDir::toNativeSeparators(path)
                             << "for" << url.toString() << ":" << file.errorString();
        return std::nullopt;
    }
    return data;
}

bool HttpFileCache::write(const QUrl& url, const QByteArray& data) {
    if (data.isEmpty()) {
        return false;
    }

    const QString path = pathForUrl(url);
    if (path.isEmpty()) {
        return false;
    }

    const QFileInfo info(path);
    if (!QDir().mkpath(info.absolutePath())) {
        qWarning().noquote() << "http cache: cannot create directory"
                             << QDir::toNativeSeparators(info.absolutePath())
                             << "for" << url.toString();
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        qWarning().noquote() << "http cache: cannot open" << QDir::toNativeSeparators(path)
                             << "for" << url.toString() << ":" << file.errorString();
        return false;
    }
    if (file.write(data) != data.size()) {
        qWarning().noquote() << "http cache: short write to" << QDir::toNativeSeparators(path)
                             << "for" << url.toString() << ":" << file.errorString();
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        qWarning().noquote() << "http cache: cannot commit" << QDir::toNativeSeparators(path)
                             << "for" << url.toString() << ":" << file.errorString();
        return false;
    }
    return true;
}

HttpFileCache::Stats HttpFileCache::stats() const {
    Stats result;
    if (!QFileInfo::exists(root_path_)) {
        return result;
    }

    QDirIterator it(root_path_, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        ++result.file_count;
        result.byte_count += it.fileInfo().size();
    }
    return result;
}

bool HttpFileCache::clear(QString& error) {
    error.clear();
    QDir root(root_path_);
    if (!root.exists()) {
        return true;
    }
    if (!root.removeRecursively()) {
        error = QStringLiteral("Could not remove %1").arg(QDir::toNativeSeparators(root_path_));
        qWarning().noquote() << "http cache: cannot clear"
                             << QDir::toNativeSeparators(root_path_);
        return false;
    }
    return true;
}

} // namespace libreshockwave::debugger
