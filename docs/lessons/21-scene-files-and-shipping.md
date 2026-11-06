# Lesson 21 - Scene files, and how a game gets shipped

## Understand

**A scene file answers one question: how do I rebuild this scene?** There are two honest answers. *Package everything*: copy the vertices, textures and settings into one file of our own. *Reference and describe*: store the **path** of each asset plus **only what you changed**. We chose the second. The ship's vertices already live in its `.fbx`; copying them would bloat the scene file, hide the data from other tools, and go stale the moment someone edits the model.

**Packaging still has its place, later.** A real engine builds a *pack* (all assets compressed into one archive) as a step when it exports a game. That is the same references-plus-settings scene, run through a bundler. It is not the format you edit.

## Visualize

**File > Save Scene As...**, then open the `.scene.json` in a text editor. You can read it: the camera, the lights, then one entry per object. Change `"waveSpeed"` for the water, save, and use **File > Open Scene...** in the editor: the water moves differently. A scene you can read and diff is a scene you can debug.

## Why it is built this way

**Text, and a version.** The file starts with `"format"` and `"version"`. A loader that reads the format first can say "this is not a scene" instead of crashing on garbage, and the version lets a future editor read old files.

**Settings are plain structs, so saving them is mechanical.** Each struct lists its fields once (`SceneJson.h`); the same list reads and writes it. A debug-only field (the wireframe switch, a debug view) is deliberately left out: it is not part of what a scene *is*.

**Missing fields keep their default.** If a later patch adds a field to `TerrainSettings`, an old file simply lacks it, and the default fills in. Unknown keys are ignored. That is how old files keep loading after the program grows.

**Enums are stored by name.** `"toneMapper": "aces"`, not `2`. Reordering the enum later cannot change what an old file means. An unknown name falls back to the first entry instead of failing.

**Paths are relative, and we check portability.** A model path is stored relative to the scene file, so the project folder can move as a whole. If a model lives *outside* the `assets` folder (your Downloads, say), saving still works but warns you: that file will not be there on another machine.

**Saving cannot corrupt your scene.** The file is written to a `.tmp` next to it and swapped in afterwards, so a crash half-way leaves the old scene intact.

**Loading is two steps.** First the whole file is read and parsed; if that fails, nothing in the scene has changed. Only then are the contents replaced. After that, a problem affects one object at a time: a model whose file is missing is skipped and reported, and everything else still loads.

**Order inside an object matters.** Settings first, transform last. Setting the transform of some objects changes other state (a cube stops spinning, the water's height follows its position), so the transform must not be undone by a later step.

**Ids are not saved.** An object's id exists only to tell objects apart *within a session*. After loading, new ids are given out, and the editor's selection is cleared.

**One start scene for the game and the editor.** `BuildStartScene` loads `assets/scenes/main.scene.json` if it exists and loads, and builds the demo scene otherwise. Both programs call it, so the scene you save as `main.scene.json` is what the game shows.

## Shipping a game

1. In the editor, build your scene and **Save Scene As...** `assets/scenes/main.scene.json`. Saved scenes go to the `assets/scenes` folder **next to the running program**: copy the file into your project's `assets/scenes` so it survives a rebuild and goes into git.
2. Put every model you use inside `assets/` (for example `assets/models/ship/`), and import from there so the saved paths are portable.
3. Build the `D3D11Example` target. It links no editor code.
4. Copy the program, `shaders/` and `assets/` together. The build already does this next to the executable.

## Other ways, and the trade-offs

**A binary format** loads much faster and is smaller, but cannot be read or diffed. Real engines have both: text while you work, binary in the shipped game. **Packaging into one file** is wonderful for distribution and awkward while editing. **A reflection system** that lists fields automatically means you can never forget one; in C++ that costs macros or a code generator, so here the lists are written by hand and checked by a small script.

## Predict, then check

1. You rename `assets/models/ship` to `assets/models/dutch_ship`. What happens when the scene loads?
2. A newer version of the program adds `"fogDensity"` to the water settings. Does an old scene file still load?
3. Why is the transform applied last when loading a cube?

**Answers.** (1) The model is skipped, listed in the warnings pop-up, and the rest of the scene loads. (2) Yes: the missing key keeps its default. (3) Setting a cube's transform by hand stops its spinning, so the saved "spinning" flag is applied after the transform; the other order would lose it.
