#pragma once

#include <QTreeWidget>
#include <string>

namespace libreshockwave {
namespace player {
class Player;
}
} // namespace libreshockwave

namespace libreshockwave::debugger {

/// Tree panel showing loaded movies → scripts → handlers.
class MovieTreePanel : public QTreeWidget {
    Q_OBJECT

public:
    explicit MovieTreePanel(QWidget* parent = nullptr);

    /// Populate the tree from the player's cast libraries.
    void populate(libreshockwave::player::Player* player);

    /// Clear all items.
    void clearAll();

signals:
    /// Emitted when the user selects a script (for loading handler list).
    void scriptSelected(int castLibNumber, int scriptId);
    /// Emitted when the user selects a specific handler (for loading code).
    void handlerSelected(int scriptId, const std::string& handlerName);

private:
    void onItemClicked(QTreeWidgetItem* item, int column);
};

} // namespace libreshockwave::debugger
