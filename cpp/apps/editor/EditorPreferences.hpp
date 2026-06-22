#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

namespace libreshockwave::editor {

class EditorPreferences final {
public:
    [[nodiscard]] static QString lastOpenDirectory();
    static void setLastOpenDirectory(const QString& path);

    [[nodiscard]] static QStringList recentProjects();
    static void addRecentProject(const QString& path);
    static void clearRecentProjects();

    [[nodiscard]] static QMap<QString, QString> movieParams(const QString& moviePath);
    [[nodiscard]] static bool hasMovieParams(const QString& moviePath);
    static void setMovieParams(const QString& moviePath, const QMap<QString, QString>& params);

    [[nodiscard]] static QString movieBreakpoints(const QString& moviePath);
    static void setMovieBreakpoints(const QString& moviePath, const QString& serialized);
    static void clearMovieBreakpoints(const QString& moviePath);

    [[nodiscard]] static int stageZoomPercent();
    static void setStageZoomPercent(int percent);

    [[nodiscard]] static bool viewOption(const QString& key, bool defaultValue);
    static void setViewOption(const QString& key, bool enabled);

    [[nodiscard]] static bool preferenceBool(const QString& group, const QString& key, bool defaultValue);
    static void setPreferenceBool(const QString& group, const QString& key, bool enabled);

    [[nodiscard]] static int preferenceInt(const QString& group, const QString& key, int defaultValue);
    static void setPreferenceInt(const QString& group, const QString& key, int value);

    [[nodiscard]] static QString preferenceString(const QString& group, const QString& key, const QString& defaultValue);
    static void setPreferenceString(const QString& group, const QString& key, const QString& value);

private:
    static QString movieParamKey(const QString& moviePath);
};

} // namespace libreshockwave::editor
