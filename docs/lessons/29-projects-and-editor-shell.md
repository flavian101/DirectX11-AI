# Lesson 29 - Projects, and an editor that looks like an engine

## Understand

**A scene file is one level; a project is the level above it.** Until now the editor opened and saved single `.scene.json` files, and the game always started with `assets/scenes/main.scene.json`. A project is a folder that holds several scenes and ONE assets folder, plus a small file (`Name.d3dproj`) listing the scenes and saying which one starts the game.

**It does not restyle anything by itself.** What it gives the editor is a root to build on: an Assets panel needs to know *which* folder to show, and scene paths need to know *which* folder counts as shipped. The look comes from the panels and the theme.

## Visualize

**Folder layout.** `MyGame/MyGame.d3dproj` sits next to `assets/`. Inside it: `assets/scenes/*.scene.json`, `assets/models`, `assets/textures`, and a file the editor writes for you, `assets/startup.json`.

**File > New Project...** Pick a folder and a name. The scene you are editing is saved as `assets/scenes/main.scene.json` and becomes the project's first scene. Nothing is thrown away.

**Bottom panels.** *Assets* lists the folder (double-click a scene to open it, a model to import it). *Console* shows every `LOG_*` line live, with level toggles and a filter. *Scenes*, under the Hierarchy, lists the project's scenes: a star marks the one that starts the game.

## Why it is built this way

**Why a separate `Project` module?** `src/Project.h` has no Direct3D and no ImGui, so `tests/project_test.cpp` checks the rules (duplicates, the start scene moving on when you remove it, bad files, paths that try to escape) without a window.

**Why does a project write `startup.json`?** A shipped game only has `assets/`: the `.d3dproj` stays on your machine. So the one fact the game needs, the start scene, is written inside `assets/`. `ReadStartupScene` refuses absolute paths and `..`, because that file is read on someone else's machine. With no `startup.json`, the game starts exactly as before.

**Why one assets root per project?** `SaveScene` warns when a model lives outside the folder that ships. That folder used to be the hard-coded `"assets"` next to the program. It is now a parameter, so each project warns about its own folder.

**Why ask before opening another scene?** Opening replaces what is on screen. The history has a `Revision()` that changes on every push, undo, redo and clear; "unsaved" is just `Revision() != revisionAtLastSave`. A request to open something waits in `nav_` while a small prompt asks Save / Discard / Cancel. Choosing Save runs the save first, and the navigation only continues if the scene is clean afterwards.

**Why do the panels only report?** `AssetBrowserPanel`, `ScenesPanel` and `ConsolePanel` draw and return an action. `EditorLayer` carries it out at the start of the next frame, like every other request, so no scene is replaced while a list is being walked.

**The Console reads the logger's memory.** `Logger` now keeps the last 1000 lines and hands out only the newer ones (`CopyNewerThan`), under its existing mutex. The file `d3d11_log.txt` is unchanged.

**The theme is one function.** `ApplyEditorTheme` sets colours, rounding and spacing in one place, so panels do not carry their own styling. It avoids the tab-colour names that changed between ImGui versions.

## Other ways, and the trade-offs

**Project file inside `assets/`.** Then nothing extra is needed for shipping, but the editor's own data would be shipped too. Keeping `.d3dproj` outside and writing a tiny `startup.json` inside is cleaner.

**An asset database with ids and a cache** (Unity-style). Renaming a file would never break a reference. It is a big system; paths relative to the scene file are enough for now.

**Limits.** Paths are handled as the system's narrow text, as before, so non-ASCII folder names may not work yet. Existing `imgui.ini` files keep your old layout: use *View > Reset layout* to see the new panels docked.

## Predict, then check

1. You delete the start scene from the project (Remove from project). Which scene starts the game, and what does `startup.json` say after?
2. The scene is open with unsaved changes. You double-click another scene in the Scenes panel and choose Save, then cancel the Save dialog (new scene, never saved). What happens to the other scene?
3. Why does `RemoveScene` not delete the file?

**Answers.** (1) The first remaining scene; `SaveProject` rewrites `startup.json` with it (or leaves it alone when no scenes remain). (2) Nothing opens: the save was cancelled, the scene is still dirty, so the navigation is dropped. (3) Removing from a list is reversible with "Add open scene"; deleting a file is not, and the project must never destroy work on its own.
