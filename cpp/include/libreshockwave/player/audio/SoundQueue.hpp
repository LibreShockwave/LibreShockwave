#pragma once

#include <optional>
#include <vector>

#include "libreshockwave/lingo/Datum.hpp"

namespace libreshockwave::player::audio {

// The clips waiting on one sound channel, as built by sound(n).queue() and
// sound(n).setPlaylist(). Entries are stored as deep copies in queue order.
// Queueing alone plays nothing: start() marks the queue running, and a running
// queue hands the channel its next entry each time the current clip ends. It
// stops running when cleared, which the channel does once nothing is left.
//
//   SoundQueue queue;
//   queue.push(memberRef);
//   queue.start();
//   if (auto next = queue.popFront()) { /* play *next */ }
class SoundQueue {
public:
    // Appends one entry; a one-element list wrapping a property list is unwrapped.
    void push(const lingo::Datum& entry);

    // Replaces all entries with the items of a list, or with a single non-list
    // entry. VOID empties the queue. Does not change whether it is running.
    void replace(const lingo::Datum& playlist);

    // Deep copies of the waiting entries, first to play first.
    [[nodiscard]] std::vector<lingo::Datum> entries() const;

    // Removes and returns the first entry, or nullopt when the queue is empty.
    [[nodiscard]] std::optional<lingo::Datum> popFront();

    // Marks the queue running, so the channel advances through it as clips end.
    void start();

    // Drops every entry and stops running.
    void clear();

    // True from start() until clear().
    [[nodiscard]] bool isRunning() const;

    // True while the queue is running and entries still wait to play.
    [[nodiscard]] bool hasWaiting() const;

private:
    std::vector<lingo::Datum> entries_;
    bool running_{false};
};

} // namespace libreshockwave::player::audio
