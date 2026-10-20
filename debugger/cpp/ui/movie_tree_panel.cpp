#include "movie_tree_panel.hpp"

#include <QHeaderView>
#include <QLineEdit>
#include <QSettings>
#include <QTreeWidget>
#include <QVBoxLayout>

#include "libreshockwave/player/player.hpp"
#include "libreshockwave/player/cast/cast_lib.hpp"
#include "libreshockwave/player/cast/cast_lib_manager.hpp"
#include "libreshockwave/chunks/script_chunk.hpp"
#include "libreshockwave/format/script_format_utils.hpp"

namespace libreshockwave::debugger {

static const char* k_setting_tree_filter = "debugger/movieTreeFilter";

MovieTreePanel::MovieTreePanel(QWidget* parent)
    : QWidget(parent) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    filter_edit_ = new QLineEdit(this);
    filter_edit_->setPlaceholderText(
        QStringLiteral("Filter movies, scripts, handlers…"));
    filter_edit_->setClearButtonEnabled(true);
    layout->addWidget(filter_edit_);

    tree_ = new QTreeWidget(this);
    tree_->setHeaderHidden(true);
    tree_->setColumnCount(1);
    tree_->setIndentation(12);
    tree_->setRootIsDecorated(true);
    tree_->setAnimated(true);
    layout->addWidget(tree_, 1);

    connect(tree_, &QTreeWidget::itemClicked, this, &MovieTreePanel::onItemClicked);
    connect(filter_edit_, &QLineEdit::textChanged, this, &MovieTreePanel::onFilterTextChanged);

    // Restore the persisted filter so it is applied to the first populate.
    QSettings settings;
    filter_edit_->setText(settings.value(QString::fromLatin1(k_setting_tree_filter)).toString());
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
    const auto& cast_libs = player.castLibManager().castLibs();
    for (const auto& [number, cast_lib] : cast_libs) {
        if (cast_lib == nullptr) continue;

        MovieTreeSnapshot::MovieEntry movie;
        movie.cast_lib_number = number;
        movie.name = cast_lib->name();
        movie.file_name = cast_lib->fileName();

        const auto names = cast_lib->scriptNames();
        for (const auto& script : cast_lib->allScripts()) {
            if (script == nullptr) continue;

            MovieTreeSnapshot::ScriptEntry script_entry;
            script_entry.script_id = script->id().value();
            script_entry.cast_lib_number = number;
            script_entry.display_name = script->displayName();
            script_entry.type_name =
                libreshockwave::format::getScriptTypeName(script->resolvedScriptType());
            for (const auto& handler : script->handlers()) {
                script_entry.handlers.push_back(
                    MovieTreeSnapshot::HandlerEntry{script->resolveName(handler.name_id)});
            }
            for (const auto& property : script->properties()) {
                script_entry.property_names.push_back(
                    script->resolveName(property.name_id, names.get()));
            }
            for (const auto& global : script->globals()) {
                script_entry.global_names.push_back(
                    script->resolveName(global.name_id, names.get()));
            }
            movie.scripts.push_back(std::move(script_entry));
        }
        snapshot.movies.push_back(std::move(movie));
    }
    return snapshot;
}

void MovieTreePanel::populateFromSnapshot(const MovieTreeSnapshot& snapshot) {
    last_snapshot_ = snapshot;

    // Preserve the user's expansion state across repopulates so a runtime
    // cast load doesn't collapse the tree.  Movie nodes start collapsed by
    // default.
    QSet<int> expanded_movies;
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        auto* item = tree_->topLevelItem(i);
        if (item->isExpanded()) {
            expanded_movies.insert(item->data(0, Qt::UserRole).toInt());
        }
    }

    tree_->clear();

    const QString filter_text = filter_edit_->text().trimmed();
    const bool filtering = !filter_text.isEmpty();
    const auto matches = [&filter_text](const QString& text) {
        return text.contains(filter_text, Qt::CaseInsensitive);
    };

    for (const auto& movie : snapshot.movies) {
        const auto movie_name = QString::fromStdString(movie.name);
        const auto file_name = QString::fromStdString(movie.file_name);

        auto* movie_item = new QTreeWidgetItem(tree_);
        movie_item->setText(0, movie_name);
        movie_item->setToolTip(0, file_name);
        movie_item->setData(0, Qt::UserRole, movie.cast_lib_number); // castLibNumber
        movie_item->setData(0, Qt::UserRole + 1, -1);              // not a script

        bool any_child_visible = false;
        for (const auto& script : movie.scripts) {
            const auto display_name = QString::fromStdString(script.display_name);
            const auto type_name = QString::fromStdString(script.type_name);
            const bool script_matches =
                !filtering || matches(display_name) || matches(type_name);

            auto* script_item = new QTreeWidgetItem(movie_item);
            script_item->setText(0, QStringLiteral("%1  [%2]").arg(display_name, type_name));
            script_item->setData(0, Qt::UserRole, script.cast_lib_number); // castLibNumber
            script_item->setData(0, Qt::UserRole + 1, script.script_id);  // scriptId

            // Add handler children
            bool any_handler_visible = false;
            for (const auto& handler : script.handlers) {
                const auto h_name = QString::fromStdString(handler.name);
                if (filtering && !matches(h_name)) {
                    continue;
                }
                any_handler_visible = true;
                auto* handler_item = new QTreeWidgetItem(script_item);
                handler_item->setText(0, h_name);
                handler_item->setData(0, Qt::UserRole, script.cast_lib_number);
                handler_item->setData(0, Qt::UserRole + 1, script.script_id);
                handler_item->setData(0, Qt::UserRole + 2, h_name); // handler name
            }

            if (script_matches || any_handler_visible) {
                any_child_visible = true;
            } else {
                delete script_item;
            }
        }

        const bool movie_visible = !filtering ||
                                  matches(movie_name) || matches(file_name) ||
                                  any_child_visible;
        if (!movie_visible) {
            delete movie_item;
            continue;
        }

        // Matched movies expand so the matching scripts/handlers are
        // visible; without a filter the tree starts collapsed and keeps the
        // user's expansion state across repopulates.
        movie_item->setExpanded(filtering || expanded_movies.contains(movie.cast_lib_number));
    }
}

const MovieTreeSnapshot& MovieTreePanel::lastSnapshot() const {
    return last_snapshot_;
}

void MovieTreePanel::selectScript(int cast_lib_number, int script_id) {
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        auto* movie = tree_->topLevelItem(i);
        if (movie->data(0, Qt::UserRole).toInt() != cast_lib_number) continue;
        for (int j = 0; j < movie->childCount(); ++j) {
            auto* script = movie->child(j);
            if (script->data(0, Qt::UserRole + 1).toInt() != script_id) continue;
            movie->setExpanded(true);
            script->setSelected(true);
            tree_->scrollToItem(script);
            return;
        }
    }
}

void MovieTreePanel::selectHandler(int cast_lib_number, int script_id,
                                   const std::string& handler_name) {
    if (handler_name.empty()) {
        selectScript(cast_lib_number, script_id);
        return;
    }
    for (int i = 0; i < tree_->topLevelItemCount(); ++i) {
        auto* movie = tree_->topLevelItem(i);
        if (movie->data(0, Qt::UserRole).toInt() != cast_lib_number) continue;
        for (int j = 0; j < movie->childCount(); ++j) {
            auto* script = movie->child(j);
            if (script->data(0, Qt::UserRole + 1).toInt() != script_id) continue;
            script->setExpanded(true);
            for (int k = 0; k < script->childCount(); ++k) {
                auto* handler = script->child(k);
                if (handler->text(0).toStdString() != handler_name) continue;
                handler->setSelected(true);
                tree_->scrollToItem(handler);
                return;
            }
            // The handler child may be hidden by the filter; still select the script.
            script->setSelected(true);
            tree_->scrollToItem(script);
            return;
        }
    }
}

void MovieTreePanel::clearAll() {
    tree_->clear();
    last_snapshot_ = MovieTreeSnapshot{};
}

void MovieTreePanel::onFilterTextChanged(const QString& /*text*/) {
    QSettings settings;
    settings.setValue(QString::fromLatin1(k_setting_tree_filter), filter_edit_->text());

    // Re-render from the last snapshot rather than re-reading the player:
    // the snapshot is a value copy, safe to use on the main thread while the
    // VM thread owns the live cast state.
    populateFromSnapshot(last_snapshot_);
}

void MovieTreePanel::onItemClicked(QTreeWidgetItem* item, int /*column*/) {
    if (item == nullptr) return;

    const int cast_lib_number = item->data(0, Qt::UserRole).toInt();
    const int script_id = item->data(0, Qt::UserRole + 1).toInt();
    const QString handler_name = item->data(0, Qt::UserRole + 2).toString();

    if (script_id >= 0 && handler_name.isEmpty()) {
        // Script level — emit scriptSelected
        emit scriptSelected(cast_lib_number, script_id);
    } else if (script_id >= 0 && !handler_name.isEmpty()) {
        // Handler level — emit handlerSelected
        emit handlerSelected(cast_lib_number, script_id, handler_name.toStdString());
    }
}

} // namespace libreshockwave::debugger
