#include "MovieTreePanel.hpp"

#include <QHeaderView>

#include "libreshockwave/player/Player.hpp"
#include "libreshockwave/player/cast/CastLib.hpp"
#include "libreshockwave/player/cast/CastLibManager.hpp"
#include "libreshockwave/chunks/ScriptChunk.hpp"
#include "libreshockwave/format/ScriptFormatUtils.hpp"

namespace libreshockwave::debugger {

MovieTreePanel::MovieTreePanel(QWidget* parent)
    : QTreeWidget(parent) {
    setHeaderHidden(true);
    setColumnCount(1);
    setIndentation(12);
    setRootIsDecorated(true);
    setAnimated(true);

    connect(this, &QTreeWidget::itemClicked, this, &MovieTreePanel::onItemClicked);
}

void MovieTreePanel::populate(libreshockwave::player::Player* player) {
    clearAll();
    if (player == nullptr) return;

    const auto& castLibs = player->castLibManager().castLibs();
    for (const auto& [number, castLib] : castLibs) {
        if (castLib == nullptr) continue;

        auto* movieItem = new QTreeWidgetItem(this);
        movieItem->setText(0, QString::fromStdString(castLib->name()));
        movieItem->setToolTip(0, QString::fromStdString(castLib->fileName()));
        movieItem->setData(0, Qt::UserRole, number);       // castLibNumber
        movieItem->setData(0, Qt::UserRole + 1, -1);       // not a script
        movieItem->setExpanded(true);

        for (const auto& script : castLib->allScripts()) {
            if (script == nullptr) continue;

            auto* scriptItem = new QTreeWidgetItem(movieItem);
            const auto typeName = QString::fromStdString(
                libreshockwave::format::getScriptTypeName(script->resolvedScriptType()));
            scriptItem->setText(0, QStringLiteral("%1  [%2]")
                .arg(QString::fromStdString(script->displayName()), typeName));
            scriptItem->setData(0, Qt::UserRole, number);        // castLibNumber
            scriptItem->setData(0, Qt::UserRole + 1, script->id().value()); // scriptId

            // Add handler children
            for (const auto& handler : script->handlers()) {
                auto* handlerItem = new QTreeWidgetItem(scriptItem);
                const auto hName = QString::fromStdString(
                    script->resolveName(handler.nameId));
                handlerItem->setText(0, hName);
                handlerItem->setData(0, Qt::UserRole, number);
                handlerItem->setData(0, Qt::UserRole + 1, script->id().value());
                handlerItem->setData(0, Qt::UserRole + 2, hName); // handler name
            }
        }
    }
}

void MovieTreePanel::clearAll() {
    clear();
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
