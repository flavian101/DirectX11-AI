# Lesson 23 - Undo and redo

## Understand

**Undo is a list of things you did, plus a bookmark.** Each entry knows how to undo itself and how to do itself again. The bookmark says where you are: entries before it are "done", entries after it can be redone. Undo moves the bookmark back one step; redo moves it forward.

**Doing something new after an undo throws the redo branch away.** If you undo three steps and then change a colour, the three undone steps no longer lead anywhere sensible, so they are dropped. Every editor behaves this way.

## Visualize

**Edit menu: Undo / Redo (Ctrl+Z, Ctrl+Y or Ctrl+Shift+Z).** Drag the gizmo, change a slider, add a cube, delete the ship. Open **View > History** and you will see one line per thing you did, with `>` marking where you stand. Click any line to jump there.

## Why it is built this way

**Two kinds of change, two ways to record them.** Changing a property (a slider, a colour, a gizmo drag) happens inside dozens of different widgets; adding or deleting an object happens in a few places. Wrapping every widget in recording code would be a huge, easy-to-forget job. So properties are *found* by comparing snapshots, and object lifetime is *recorded* where it happens.

**A snapshot is a piece of text.** Lesson 21 gave every object a description (the scene file). The same description, as compact JSON, is a snapshot of that object. Keys are sorted, so the same state always produces the same text, and "did anything change?" is comparing two strings. This also means saving, loading and undo share one set of code: when undo restores a snapshot it calls the same function that loading a scene uses.

**One edit, not sixty.** A slider drag changes the value on many frames in a row, and you think of it as one edit. So while you interact (hold a slider, drag a gizmo, type in a field) the baseline stays frozen. On the first frame after you let go, if the state differs from the baseline, `ChangeDetector` reports exactly one edit, with the state from *before* it started. A checkbox click, which needs no drag, is caught the same way on the next frame. Both cases have unit tests.

**Watching only what matters.** Each frame we snapshot the *selected object* and the *global settings* (lights, shadows, post-processing, sky lighting), not the whole scene. The detector is told what it is watching (the object's id): selecting another object starts a fresh baseline instead of reporting the difference between two different things.

**After an undo, forget the baseline.** Undo changes the scene behind the detector's back. Without `Reset`, the next frame would see "the state differs from my baseline" and record the undo itself as a new edit, which would erase your redo branch. So after every undo or redo the baselines are reset.

**The spinning demo cube.** It changes its rotation by itself every frame, which would look like an edit on every frame. A spinning cube's transform is therefore left out of its snapshot (a file still stores it). The first time you move it by hand it stops spinning, and from then on it is tracked.

**Delete does not destroy.** Deleting an object takes it *out* of the scene and keeps it alive inside the history command, along with its place in the list. Undo puts the very same object back, with its GPU resources, id and name; nothing is rebuilt or re-read from disk. Adding an object is the mirror image: its undo takes it out and keeps it, and redo puts it back. The history is capped at 100 entries; when the oldest entry is dropped, a held object is finally destroyed.

**Opening a scene clears the history.** Every id in it would refer to an object that is gone.

**What is not undoable.** Flying the camera is not an edit. Importing a *terrain image* is, because the image paths are part of the terrain's snapshot.

## Other ways, and the trade-offs

**One command class per kind of edit** (MoveCommand, SetColorCommand...). Precise and cheap in memory, but every new widget needs a new command. **Snapshot the whole scene** on every edit: trivially correct, but far too slow and memory-hungry when a scene holds a terrain and models. **Delta or persistent data structures** (only the changed parts are stored) are what big editors use. Ours is a middle path: snapshots, but only of the thing being edited.

## Predict, then check

1. You drag a slider for two seconds. How many history entries appear, and why?
2. You delete a cube, add a new one, then press Ctrl+Z twice. What do you see?
3. Why must the detectors be reset after an undo?

**Answers.** (1) One: the baseline is frozen while you hold the slider, and a single edit is reported afterwards. (2) The first undo removes the new cube; the second brings the deleted one back, the same object. (3) Otherwise the next frame would read the undo as a fresh edit and record it, which would wipe out the redo branch.
