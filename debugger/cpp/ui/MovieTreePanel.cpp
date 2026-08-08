#include "MovieTreePanel.hpp"

#include <QHeaderView>
#include <QLineEdit>
#include <QSettings>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "libreshockwave/player/Player.hpp"
#include "libreshockwave/player/cast/CastLib.hpp"
#include "libreshockwave/player/cast/CastLibManager.hpp"
#include "libreshockwave/chunks/ScriptChunk.hpp"
#include "libreshockwave/format/ScriptFormatUtils.hpp"

namespace libreshockwave::debugger {

static const char* kSettingTreeFilter = "debugger/movieTreeFilter";

MovieTreePanel::MovieTreePanel(QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    filterEdit_ = new QLineEdit(this);
    filterEdit_->setPlaceholderText(
        QStringLiteral("Filter movies, scripts, handlers…"));
    filterEdit_->setClearButtonEnabled(true);
    layout->addWidget(filterEdit_);

    tree_ = new QTreeWidget(this);
    tree_->setHeaderHidden(true);
    tree_->setColumnCount(1);
    tree_->setIndentation(12);
    tree_->setRootIsDecorated(true);
    tree_->setAnimated(true);
    layout->addWidget(tree_, 1);

    connect(tree_, &QTreeWidget::itemClicked, this, &MovieTreePanel::onItemClicked);
    connect(filterEdit_, &QLineEdit::textChanged, this, &MovieTreePanel::onFilterTextChanged);

    // Restore the persisted filter so it is applied to the first populate.
    QSettings settings;
    filterEdit_->setText(settings.value(QString::fromLatin1(kSettingTreeFilter)).toString());
}

void MovieTreePanel::populate(libreshockwave::player::Player* player) {
    if (player == nullptr) {
        clearAll();
        return;
    }
    populateFromSnapshot(buildSnapshot(*player));
}

MovieTreeSnapshot MovieTreePanel::buildSnapshot(libreshockwave::player::Player& player) {
    MovieTreeSnapshot snapshot;
    const auto& castLibs = player.castLibManager().castLibs();
    for (const auto& [number, castLib] : castLibs) {
        if (castLib == nullptr) continue;

        MovieTreeSnapshot::MovieEntry movie;
        movie.castLibNumber = number;
        movie.name = castLib->name();
        movie.fileName = castLib->fileName();

        for (const auto& script : castLib->allScripts()) {
            if (script == nullptr) continue;

            MovieTreeSnapshot::ScriptEntry scriptEntry;
            scriptEntry.scriptId = script->id().value();
            scriptEntry.castLibNumber = number;
            scriptEntry.displayName = script->displayName();
            scriptEntry.typeName =
                libreshockwave::format::getScriptTypeName(script->resolvedScriptType());
            for (const auto& handler : script->handlers()) {
                scriptEntry.handlers.push_back(
                    MovieTreeSnapshot::HandlerEntry{script->resolveName(handler.nameId)});
            }
            movie.scripts.push_back(std::move(scriptEntry));
        }
        snapshot.movies.push_back(std::move(movie));
    }
    return snapshot;
}

void MovieTreePanel::populateFromSnapshot(const MovieTreeSnapshot& snapshot) {
    lastSnapshot_ = snapshot;

    // Preserve the user's expansion state across repopulates so a runtime
    // cast load doesn't collapse the tree.  Movie nodes start collapsed by
    // default.
    QSet<int> expandedMovies;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        auto* item = tree_->topLevelItem(i);
        if (item->isExpanded()) {
            expandedMovies.insert(item->data(0, Qt::UserRole).toInt());
        }
    }

    tree_->clear();

    const QString filterText = filterEdit_->text().trimmed();
    const bool filtering = !filterText.isEmpty();
    const auto matches = [&filterText](const QString& text) {
        return text.contains(filterText, Qt::CaseInsensitive);
    };

    for (const auto& movie : snapshot.movies) {
        const auto movieName = QString::fromStdString(movie.name);
        const auto fileName = QString::fromStdString(movie.fileName);

        auto* movieItem = new QTreeWidgetItem(tree_);
        movieItem->setText(0, movieName);
        movieItem->setToolTip(0, fileName);
        movieItem->setData(0, Qt::UserRole, movie.castLibNumber); // castLibNumber
        movieItem->setData(0, Qt::UserRole + 1, -1);              // not a script

        bool anyChildVisible = false;
        for (const auto& script : movie.scripts) {
            const auto displayName = QString::fromStdString(script.displayName);
            const auto typeName = QString::fromStdString(script.typeName);
            const bool scriptMatches =
                !filtering || matches(displayName) || matches(typeName);

            auto* scriptItem = new QTreeWidgetItem(movieItem);
            scriptItem->setText(0, QStringLiteral("%1  [%2]").arg(displayName, typeName));
            scriptItem->setData(0, Qt::UserRole, script.castLibNumber); // castLibNumber
            scriptItem->setData(0, Qt::UserRole + 1, script.scriptId);  // scriptId

            // Add handler children
            bool anyHandlerVisible = false;
            for (const auto& handler : script.handlers) {
                const auto hName = QString::fromStdString(handler.name);
                if (filtering && !matches(hName)) {
                    continue;
                }
                anyHandlerVisible = true;
                auto* handlerItem = new QTreeWidgetItem(scriptItem);
                handlerItem->setText(0, hName);
                handlerItem->setData(0, Qt::UserRole, script.castLibNumber);
                handlerItem->setData(0, Qt::UserRole + 1, script.scriptId);
                handlerItem->setData(0, Qt::UserRole + 2, hName); // handler name
            }

            if (scriptMatches || anyHandlerVisible) {
                anyChildVisible = true;
            } else {
                delete scriptItem;
            }
        }

        const bool movieVisible = !filtering ||
                                  matches(movieName) || matches(fileName) ||
                                  anyChildVisible;
        if (!movieVisible) {
            delete movieItem;
            continue;
        }

        // Matched movies expand so the matching scripts/handlers are
        // visible; without a filter the tree starts collapsed and keeps the
        // user's expansion state across repopulates.
        movieItem->setExpanded(filtering || expandedMovies.contains(movie.castLibNumber));
    }
}

void MovieTreePanel::clearAll() {
    tree_->clear();
    lastSnapshot_ = MovieTreeSnapshot{};
}

void MovieTreePanel::onFilterTextChanged(const QString& /*text*/) {
    QSettings settings;
    settings.setValue(QString::fromLatin1(kSettingTreeFilter), filterEdit_->text());

    // Re-render from the last snapshot rather than re-reading the player:
    // the snapshot is a value copy, safe to use on the main thread while the
    // VM thread owns the live cast state.
    populateFromSnapshot(lastSnapshot_);
}

void MovieTreePanel::onItemClicked(QTreeWidgetItem* item, int /*column*/) {
    if (item == nullptr) return;

    const int castLibNumber = item->data(0, Qt::UserRole).toInt();
    const int scriptId = item->data(0, Qt::UserRole + 1).toInt();
    const QString handlerName = item->data(0, Qt::UserRole + 2).toString();

    if (scriptId >= 0 && handlerName.isEmpty()) {
        // Script level — emit scriptSelected
        emit scriptSelected(castLibNumber, scriptId);
    } else if (scriptId >= 0 && !handlerName.isEmpty()) {
        // Handler level — emit handlerSelected
        emit handlerSelected(scriptId, handlerName.toStdString());
    }
}

} // namespace libreshockwave::debugger
