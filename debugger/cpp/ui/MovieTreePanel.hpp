#pragma once

#include <QWidget>
#include <string>

#include "model/DebuggerModel.hpp"

class QLineEdit;
class QTreeWidget;
class QTreeWidgetItem;

namespace libreshockwave {
namespace player {
class Player;
}
} // namespace libreshockwave

namespace libreshockwave::debugger {

/// Tree panel showing loaded movies → scripts → handlers, with a search
/// filter on top.  The filter text persists across sessions (QSettings) and
/// is re-applied on every tree (re)population, including runtime cast loads.
class MovieTreePanel : public QWidget {
    Q_OBJECT

public:
    explicit MovieTreePanel(QWidget* parent = nullptr);

    /// Populate the tree from the player's cast libraries.  Main thread
    /// only, while the worker is stopped (initial load / navigation).
    void populate(libreshockwave::player::Player* player);

    /// Rebuild the tree from a snapshot captured on the VM thread.  Used for
    /// runtime cast loads, when the tree must not read live cast state
    /// concurrently with the tick loop.
    void populateFromSnapshot(const MovieTreeSnapshot& snapshot);

    /// Capture the current movie/script list.  Must be called on the VM
    /// thread — the only thread that touches cast state.
    static MovieTreeSnapshot buildSnapshot(libreshockwave::player::Player& player);

    /// Last built snapshot, so other panels (e.g. the symbol index) can reuse
    /// it without re-reading the player.
    [[nodiscard]] const MovieTreeSnapshot& lastSnapshot() const;

    /// Expand the movie and select the script with the given ID in the tree
    /// without emitting a selection signal (used for go-to-declaration).
    void selectScript(int castLibNumber, int scriptId);

    /// Like selectScript, then expand the script and select the handler
    /// child, if present.
    void selectHandler(int castLibNumber, int scriptId,
                       const std::string& handlerName);

    /// Clear all items.
    void clearAll();

signals:
    /// Emitted when the user selects a script (for loading handler list).
    void scriptSelected(int castLibNumber, int scriptId);
    /// Emitted when the user selects a specific handler (for loading code).
    void handlerSelected(int castLibNumber, int scriptId,
                         const std::string& handlerName);

private slots:
    void onFilterTextChanged(const QString& text);

private:
    void onItemClicked(QTreeWidgetItem* item, int column);

    QLineEdit* filterEdit_{nullptr};
    QTreeWidget* tree_{nullptr};

    /// Last built snapshot, kept so an edit to the filter can re-render the
    /// tree without touching the player (which the VM thread owns).
    MovieTreeSnapshot lastSnapshot_;
};

} // namespace libreshockwave::debugger
