# Lesson 53 - Declare each rule once, always keep the looks (S11, second half)

## Understand

**One rule used to live in six files.** A number such as `clusterScale` was declared in the species settings and again in the placement rules, copied between them, clamped by borrowing, saved in the JSON and drawn in the editor. Forget one place and the rule silently stops working.

**Inheritance removes the copy.** `ScatterSpeciesRules` (spacing, density, slope, height, clusters, scale, lean, sink) is one struct in `ScatterPlacement.h`. `ScatterRules` (the placement) and `ScatterSpeciesSettings` (scene file and editor) both inherit it, so `species.spacing` and `rules.spacing` are the same declaration. Variants do the same with `ScatterVariantBase`.

**Copy the whole base in one line.** `MakeBaseRules` no longer copies twenty fields; it assigns the base part of the settings to the base part of the rules. The old "borrow the rules to clamp" trick is gone too: there is one `ClampScatterSpeciesRules`, used by both sides.

**The JSON lists the shared numbers once.** The macros are written for the two base structs; the settings add only their own keys (`name`, `enabled`, `seed`, level-of-detail and shadow numbers, `variants`).

## Always keep the looks

**A species now always has a look.** The old one-look species kept a `model` and an empty `variants` list, and a second code path in the editor handled it. Now `variants` always has at least one entry: the constructor makes one (a pine) and `Clamp` adds one to an empty list.

**Old scene files still load.** A species saved before lesson 36 has a `"model"` and no looks. The loader turns that model into the one look, named like the species, and then it is never seen again. Written files have no species-level `model`.

**The editor lost a branch.** The "Several looks..." button and the one-look layout are gone. A species with one look cannot remove it, because the Remove button only appears when there is more than one.

**Three getters lost their callers.** `IsSpeciesReady` and `GetSpeciesError` were only used by the deleted branch and are removed. `GetSpeciesLodLevels` is kept and now shows in the "placed in all" line.

## Moving the inspector

**The scatter inspector has its own file.** `editor/ScatterInspector.cpp` holds `EditorLayer::DrawScatterInspector` and its helpers. The two file pickers they share with the rest of the editor moved to `editor/FileDialogs.cpp`, because an anonymous namespace cannot be shared between files.

## What was and was not checked

**Same forest, checked.** The golden hashes from lesson 52 are unchanged after every step, and the placement, settings and golden tests pass. Every file that touches scatter (the editor and the scene files included) compiles against the real Direct3D and ImGui headers.

**Not checked: the editor itself.** Nobody has opened a scene, added a look, saved and reloaded. That is the step the roadmap leaves open.

## Check yourself

1. `static_cast<ScatterSpeciesRules&>(r) = s;` copies from a `ScatterSpeciesSettings`. Which members of `s` are not copied, and why is that correct here?
2. A rule is added to `ScatterSpeciesRules` but not to the clamp or the JSON list. Which of the two mistakes stops the build, which does not, and how would you notice the other one?
3. Why does the old-file conversion live in the JSON loader and not in `ScatterSpeciesSettings::Clamp`?
