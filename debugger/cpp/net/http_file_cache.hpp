#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>

#include <optional>

namespace libreshockwave::debugger {

/// Persistent cache for HTTP GET responses fetched by the debugger.
/// Each URL maps to <root>/<scheme>/<authority>/<request path>. A non-empty
/// query string adds a short hash suffix to the file name. Only http and
/// https URLs are stored.
class HttpFileCache {
public:
    /// Number of cached files and their total size.
    struct Stats {
        int file_count{0};
        qint64 byte_count{0};
    };

    /// Create a cache rooted at root_path. The default is _cache in the
    /// current working directory.
    explicit HttpFileCache(QString root_path = defaultRootPath());

    /// Default cache root: the _cache directory in the current working
    /// directory.
    [[nodiscard]] static QString defaultRootPath();

    /// Absolute cache root path.
    [[nodiscard]] const QString& rootPath() const;

    /// Map an http(s) URL to its cache file. Returns an empty string for other
    /// URLs. The path is deterministic and safe to use as a file name on all
    /// supported platforms.
    [[nodiscard]] QString pathForUrl(const QUrl& url) const;

    /// Return cached bytes, or nullopt when the URL is not cacheable or the
    /// file is missing or unreadable.
    [[nodiscard]] std::optional<QByteArray> read(const QUrl& url) const;

    /// Atomically store a non-empty response. Returns false when the URL is
    /// not cacheable or the file cannot be written.
    [[nodiscard]] bool write(const QUrl& url, const QByteArray& data);

    /// Walk the cache and report its file count and total size.
    [[nodiscard]] Stats stats() const;

    /// Delete the cache root and its contents. Returns false and fills error
    /// when the directory cannot be removed.
    [[nodiscard]] bool clear(QString& error);

private:
    QString root_path_;
};

} // namespace libreshockwave::debugger
