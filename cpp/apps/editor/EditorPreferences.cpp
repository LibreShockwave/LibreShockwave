#include "EditorPreferences.hpp"

#include <algorithm>
#include <vector>

#include <QSettings>

#include "EditorModels.hpp"

namespace libreshockwave::editor {
namespace {

constexpr auto kLastOpenDirectoryKey = "editor/lastOpenDirectory";
constexpr auto kRecentProjectsKey = "editor/recentProjects";
constexpr auto kMovieParamsGroup = "editor/movieParams";
constexpr auto kMovieParamsPresentKey = "__present";
constexpr auto kMovieBreakpointsGroup = "editor/movieBreakpoints";
constexpr auto kStageZoomPercentKey = "editor/stageZoomPercent";
constexpr auto kViewOptionsGroup = "editor/view";
constexpr auto kPreferenceOptionsGroup = "editor/preferences";

std::vector<std::string> toStdVector(const QStringList& values) {
    std::vector<std::string> result;
    result.reserve(values.size());
    for (const auto& value : values) {
        result.push_back(value.toStdString());
    }
    return result;
}

QStringList toQStringList(const std::vector<std::string>& values) {
    QStringList result;
    result.reserve(static_cast<int>(values.size()));
    for (const auto& value : values) {
        result.push_back(QString::fromStdString(value));
    }
    return result;
}

} // namespace

QString EditorPreferences::lastOpenDirectory() {
    return QSettings{}.value(QString::fromLatin1(kLastOpenDirectoryKey)).toString();
}

void EditorPreferences::setLastOpenDirectory(const QString& path) {
    QSettings{}.setValue(QString::fromLatin1(kLastOpenDirectoryKey), path);
}

QStringList EditorPreferences::recentProjects() {
    return QSettings{}.value(QString::fromLatin1(kRecentProjectsKey)).toStringList();
}

void EditorPreferences::addRecentProject(const QString& path) {
    const auto updated = recentProjectsWithAdded(toStdVector(recentProjects()), path.toStdString());
    QSettings{}.setValue(QString::fromLatin1(kRecentProjectsKey), toQStringList(updated));
}

void EditorPreferences::clearRecentProjects() {
    QSettings{}.remove(QString::fromLatin1(kRecentProjectsKey));
}

QMap<QString, QString> EditorPreferences::movieParams(const QString& moviePath) {
    QSettings settings;
    QMap<QString, QString> params;
    const QString key = movieParamKey(moviePath);
    if (key.isEmpty()) {
        return params;
    }

    settings.beginGroup(QString::fromLatin1(kMovieParamsGroup));
    settings.beginGroup(key);
    for (const auto& paramKey : settings.childKeys()) {
        if (paramKey == QString::fromLatin1(kMovieParamsPresentKey)) {
            continue;
        }
        params.insert(paramKey, settings.value(paramKey).toString());
    }
    settings.endGroup();
    settings.endGroup();
    return params;
}

bool EditorPreferences::hasMovieParams(const QString& moviePath) {
    QSettings settings;
    const QString key = movieParamKey(moviePath);
    if (key.isEmpty()) {
        return false;
    }

    settings.beginGroup(QString::fromLatin1(kMovieParamsGroup));
    settings.beginGroup(key);
    const bool exists = settings.value(QString::fromLatin1(kMovieParamsPresentKey), false).toBool()
        || !settings.childKeys().isEmpty();
    settings.endGroup();
    settings.endGroup();
    return exists;
}

void EditorPreferences::setMovieParams(const QString& moviePath, const QMap<QString, QString>& params) {
    QSettings settings;
    const QString key = movieParamKey(moviePath);
    if (key.isEmpty()) {
        return;
    }

    settings.beginGroup(QString::fromLatin1(kMovieParamsGroup));
    settings.beginGroup(key);
    settings.remove({});
    settings.setValue(QString::fromLatin1(kMovieParamsPresentKey), true);
    for (auto it = params.cbegin(); it != params.cend(); ++it) {
        settings.setValue(it.key(), it.value());
    }
    settings.endGroup();
    settings.endGroup();
}

QString EditorPreferences::movieBreakpoints(const QString& moviePath) {
    const QString key = movieParamKey(moviePath);
    if (key.isEmpty()) {
        return {};
    }

    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kMovieBreakpointsGroup));
    const QString serialized = settings.value(key).toString();
    settings.endGroup();
    return serialized;
}

void EditorPreferences::setMovieBreakpoints(const QString& moviePath, const QString& serialized) {
    const QString key = movieParamKey(moviePath);
    if (key.isEmpty()) {
        return;
    }

    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kMovieBreakpointsGroup));
    if (serialized.trimmed().isEmpty()) {
        settings.remove(key);
    } else {
        settings.setValue(key, serialized);
    }
    settings.endGroup();
}

void EditorPreferences::clearMovieBreakpoints(const QString& moviePath) {
    setMovieBreakpoints(moviePath, {});
}

int EditorPreferences::stageZoomPercent() {
    const int percent = QSettings{}.value(QString::fromLatin1(kStageZoomPercentKey), 100).toInt();
    if (percent <= 0) {
        return 100;
    }
    return std::clamp(percent, 25, 400);
}

void EditorPreferences::setStageZoomPercent(int percent) {
    QSettings{}.setValue(QString::fromLatin1(kStageZoomPercentKey), std::clamp(percent, 25, 400));
}

bool EditorPreferences::viewOption(const QString& key, bool defaultValue) {
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kViewOptionsGroup));
    const bool value = settings.value(key, defaultValue).toBool();
    settings.endGroup();
    return value;
}

void EditorPreferences::setViewOption(const QString& key, bool enabled) {
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kViewOptionsGroup));
    settings.setValue(key, enabled);
    settings.endGroup();
}

bool EditorPreferences::preferenceBool(const QString& group, const QString& key, bool defaultValue) {
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kPreferenceOptionsGroup));
    settings.beginGroup(group);
    const bool value = settings.value(key, defaultValue).toBool();
    settings.endGroup();
    settings.endGroup();
    return value;
}

void EditorPreferences::setPreferenceBool(const QString& group, const QString& key, bool enabled) {
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kPreferenceOptionsGroup));
    settings.beginGroup(group);
    settings.setValue(key, enabled);
    settings.endGroup();
    settings.endGroup();
}

int EditorPreferences::preferenceInt(const QString& group, const QString& key, int defaultValue) {
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kPreferenceOptionsGroup));
    settings.beginGroup(group);
    const int value = settings.value(key, defaultValue).toInt();
    settings.endGroup();
    settings.endGroup();
    return value;
}

void EditorPreferences::setPreferenceInt(const QString& group, const QString& key, int value) {
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kPreferenceOptionsGroup));
    settings.beginGroup(group);
    settings.setValue(key, value);
    settings.endGroup();
    settings.endGroup();
}

QString EditorPreferences::preferenceString(const QString& group, const QString& key, const QString& defaultValue) {
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kPreferenceOptionsGroup));
    settings.beginGroup(group);
    const QString value = settings.value(key, defaultValue).toString();
    settings.endGroup();
    settings.endGroup();
    return value;
}

void EditorPreferences::setPreferenceString(const QString& group, const QString& key, const QString& value) {
    QSettings settings;
    settings.beginGroup(QString::fromLatin1(kPreferenceOptionsGroup));
    settings.beginGroup(group);
    settings.setValue(key, value);
    settings.endGroup();
    settings.endGroup();
}

QString EditorPreferences::movieParamKey(const QString& moviePath) {
    return QString::fromLatin1(moviePath.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

} // namespace libreshockwave::editor
