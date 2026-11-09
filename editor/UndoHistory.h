#pragma once
// The two pieces of undo/redo that need no scene and no UI, so they can be tested on their own:
//   UndoHistory    - the stack of things that were done, with an "undo position" in it
//   ChangeDetector - notices that the user FINISHED an edit, and what the state was before it
#include <memory>
#include <string>
#include <utility>
#include <vector>

// One thing the user did. It has ALREADY happened when it is recorded: the user dragged the slider, so the world
// is already changed. Undo puts the world back; Redo does it again.
class UndoCommand {
public:
    virtual ~UndoCommand() = default;
    virtual void Undo() = 0;
    virtual void Redo() = 0;
    virtual std::string Name() const = 0;
};

class UndoHistory {
public:
    explicit UndoHistory(size_t limit = 100) : limit_(limit) {}

    // Record a command that was just done. Anything that had been undone (the "redo" branch) is thrown away:
    // after you undo and then do something new, the old future no longer makes sense.
    void Push(std::unique_ptr<UndoCommand> command)
    {
        ++revision_;
        commands_.resize(position_);                 // forget the redo branch
        commands_.push_back(std::move(command));
        position_ = commands_.size();
        if (commands_.size() > limit_) {             // keep the history bounded: drop the OLDEST
            commands_.erase(commands_.begin());
            --position_;
        }
    }

    bool CanUndo() const { return position_ > 0; }
    bool CanRedo() const { return position_ < commands_.size(); }
    void Undo() { if (CanUndo()) { ++revision_; commands_[--position_]->Undo(); } }
    void Redo() { if (CanRedo()) { ++revision_; commands_[position_++]->Redo(); } }

    // Undo or redo as many steps as it takes to stand at `position` (0 = before everything).
    void JumpTo(size_t position)
    {
        if (position > commands_.size()) position = commands_.size();
        while (position_ > position) Undo();
        while (position_ < position) Redo();
    }

    void Clear() { ++revision_; commands_.clear(); position_ = 0; }
    // Changes with EVERY push, undo, redo and clear, and never repeats. "Has anything changed since the last save?" is then
    // `Revision() != revisionAtSave`. (Undo followed by redo counts as a change: safe, the worst case is one extra prompt.)
    unsigned Revision() const { return revision_; }
    size_t Size() const { return commands_.size(); }
    size_t Position() const { return position_; }   // commands [0, Position) are "done"; the rest can be redone
    const UndoCommand& At(size_t index) const { return *commands_[index]; }
    std::string UndoName() const { return CanUndo() ? commands_[position_ - 1]->Name() : std::string(); }
    std::string RedoName() const { return CanRedo() ? commands_[position_]->Name() : std::string(); }

private:
    size_t limit_;
    size_t position_ = 0;
    unsigned revision_ = 0;
    std::vector<std::unique_ptr<UndoCommand>> commands_;
};

// Turns "the state, sampled once per frame" into "one finished edit".
//
// A slider drag changes the value on many frames in a row; the user thinks of it as ONE edit, and Ctrl+Z should
// undo all of it at once. So while the user is interacting the baseline is frozen. The first frame AFTER the
// interaction, if the state differs from the baseline, exactly one edit is reported, along with the state from
// before it started.
class ChangeDetector {
public:
    // `key` says WHAT is being watched (an object's id, say): a new key starts a new baseline instead of reporting
    // the difference between two different things. `state` is its snapshot; `interacting` is true while a slider is
    // held, a gizmo is dragged or a field is being typed in. Returns true once per finished edit and fills `before`.
    bool Observe(unsigned key, const std::string& state, bool interacting, std::string& before)
    {
        if (interacting) return false;
        if (!valid_ || key != key_) {
            valid_ = true;
            key_ = key;
            baseline_ = state;
            return false;
        }
        if (state == baseline_) return false;
        before = baseline_;
        baseline_ = state;
        return true;
    }

    // Forget the baseline. Call it after something OTHER than the user changed the state (an undo, a load), so that
    // change is not mistaken for a new edit.
    void Reset() { valid_ = false; }

private:
    bool valid_ = false;
    unsigned key_ = 0;
    std::string baseline_;
};
