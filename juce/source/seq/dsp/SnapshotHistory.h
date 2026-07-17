// Bounded undo/redo substrate for the editing verbs (editing-concept: "undo
// covers editing verbs only, not knob/param changes"). Generic over the
// snapshot type — SQ-4 uses SeqProcessor::currentSessionJson() strings — and
// deliberately gesture-oriented: callers push exactly ONE snapshot per
// gesture (a continuous drag coalesces to one entry), captured BEFORE the
// mutation. Header-only, JUCE-free.
#pragma once

#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace fable {

template <typename T>
class SnapshotHistory {
public:
    explicit SnapshotHistory(size_t limit = 50) : limit_(limit) {}

    // Record the pre-edit state of the gesture about to run. A new edit
    // invalidates the redo branch (standard linear history).
    void push(T snapshotBeforeEdit) {
        undo_.push_back(std::move(snapshotBeforeEdit));
        if (undo_.size() > limit_) undo_.erase(undo_.begin());
        redo_.clear();
    }

    // Step back: hand in the CURRENT state (it becomes the redo target) and
    // receive the state to restore, or nullopt when there is nothing to undo.
    std::optional<T> undo(T current) {
        if (undo_.empty()) return std::nullopt;
        redo_.push_back(std::move(current));
        if (redo_.size() > limit_) redo_.erase(redo_.begin());
        T out = std::move(undo_.back());
        undo_.pop_back();
        return out;
    }

    // Step forward again; mirror image of undo().
    std::optional<T> redo(T current) {
        if (redo_.empty()) return std::nullopt;
        undo_.push_back(std::move(current));
        if (undo_.size() > limit_) undo_.erase(undo_.begin());
        T out = std::move(redo_.back());
        redo_.pop_back();
        return out;
    }

    void clear() { undo_.clear(); redo_.clear(); }
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }
    size_t undoDepth() const { return undo_.size(); }
    size_t redoDepth() const { return redo_.size(); }

private:
    size_t limit_;
    std::vector<T> undo_, redo_;
};

} // namespace fable
