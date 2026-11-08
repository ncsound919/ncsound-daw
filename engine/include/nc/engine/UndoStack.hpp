#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace nc::engine
{
/** Multi-level snapshot undo/redo for document state (clips, MIDI notes,
    tempo map, markers). The arrange view previously kept a single
    `undoSnapshot` with no redo; a snapshot stack is the smallest correct
    step up: every edit pushes the *before* state, undo walks back, redo
    walks forward, and any new edit clears the redo branch.

    Drag coalescing is the caller's job: push once at drag start, mutate
    freely, and the stack holds one entry for the whole gesture. Snapshots
    are values, so undo can never alias live state. */
template <typename State>
class UndoStack
{
public:
    static constexpr std::size_t defaultMaxDepth = 100;

    UndoStack() = default;
    explicit UndoStack (std::size_t depthLimit) : maxDepth_ (depthLimit)
    {
        trimToDepth();
    }

    std::size_t maxDepthLimit() const noexcept { return maxDepth_; }

    /** Deepens or shallows the history. Shrinking trims the oldest entries.
        Not noexcept: trimming move-assigns State values, and a throwing State
        must propagate instead of terminating. */
    void setMaxDepth (std::size_t depthLimit)
    {
        maxDepth_ = depthLimit;
        trimToDepth();
    }

    void clear() noexcept
    {
        past_.clear();
        future_.clear();
    }

    /** Record the state *before* an edit. */
    void push (State before)
    {
        past_.push_back (std::move (before));
        trimToDepth();
        future_.clear();
    }

    bool canUndo() const noexcept { return ! past_.empty(); }
    bool canRedo() const noexcept { return ! future_.empty(); }

    /** Returns the state to restore, parking `current` on the redo stack. */
    std::optional<State> undo (State current)
    {
        if (past_.empty())
            return std::nullopt;

        future_.push_back (std::move (current));
        State restored = std::move (past_.back());
        past_.pop_back();
        return restored;
    }

    std::optional<State> redo (State current)
    {
        if (future_.empty())
            return std::nullopt;

        past_.push_back (std::move (current));
        State restored = std::move (future_.back());
        future_.pop_back();
        return restored;
    }

    std::size_t undoDepth() const noexcept { return past_.size(); }
    std::size_t redoDepth() const noexcept { return future_.size(); }

private:
    void trimToDepth()
    {
        const auto trim = [] (std::vector<State>& stack, std::size_t limit) {
            if (stack.size() > limit)
                stack.erase (stack.begin(),
                             stack.begin() + static_cast<std::ptrdiff_t> (stack.size() - limit));
        };

        trim (past_, maxDepth_);
        trim (future_, maxDepth_);
    }

    std::size_t maxDepth_ = defaultMaxDepth;
    std::vector<State> past_;
    std::vector<State> future_;
};
} // namespace nc::engine
