# Lesson 43 - The water inspector

## Understand

**The problem.** The water had one long panel of about sixty controls, and it had grown, not been designed. The same thing was set in more than one place (Level was also the transform's Y; shore settings were in Surface, Foam and Shore), controls had names that did not say what they did ("Opacity" was the opacity of DEEP water, "Absorption" the rate colour fades with depth, "Refraction" an amount that was also an on/off switch), and a "Reset to type defaults" button built a fresh `WaterSettings`, so it also threw away a hand-drawn river and its carve.

**The cause underneath.** "Which setting belongs to which part of the panel" was written nowhere. It was only the order of ImGui calls. So nothing could reset a part, mark a part, or copy a part: there was no part to point at.

## Visualize

**Sections are data.** `WaterSettings::VisitGroup(group, a, b, f)` calls `f(a.field, b.field)` for every field of a section: one list. Everything else walks it:
- **reset a section:** `ResetGroup` copies each field from the defaults;
- **mark a section:** `GroupModified` is true if any field differs from the defaults (a * on the header, and a dot beside each control that is not at its default);
- **apply a preset:** `ApplyPreset` takes the look sections of another body. Not its place, kind, current, performance choices or river: a preset says how water LOOKS, not where it is.

Rule for the next person who adds a setting: add it to `VisitGroup`, or it will never reset, never be marked, and never be in a preset.

**Right-click to reset.** Each section header resets its section; each control resets itself. The defaults are those of the body's own kind (`WaterSettings::Defaults(type)`): a river's defaults are not an ocean's. "Reset all sections" does every section and keeps the level, size, shape and the river you drew.

**The layout.** Presets, then where the water is and how it moves (Shape, Current), then how it looks (Appearance, Waves, Reflection, Refraction, Lighting, Foam, Shoreline, Underwater), then what it costs (Performance), then Debug. One level, no two settings that mean the same thing. Level is shown, not edited: it is the transform's Position Y, and the gizmo moves it. Name stays in the inspector, as it does for every other kind of object.

**Names that say what they do.** Opacity -> Deep opacity; Absorption -> Colour fade; Shore softness -> Edge softness (and it moved to Shoreline); Sun glint -> Sun highlight; Glint sharpness -> Highlight tightness; the foam controls are grouped as Shore foam, Bubbles and Crest foam. Reflection is a Mode (sky, or a mirror) and a Quality (the mirror's resolution): two views of the one saved value, so old scenes are unaffected.

**Presets.** Looks (one click), Copy look / Paste look between bodies, and your own: Save current look writes `assets/presets/water/<name>.waterpreset`, a small JSON file that holds only the look (the keys for place, kind, current, river and performance are left out, so a preset cannot move a lake or flatten a river). A file that is not a preset, or is from a newer editor, is refused with a message and leaves the body alone; a hand-edited value out of range is clamped.

**Foam intensity.** One master control that scales the shore foam and the crest foam together, applied where the material is sent to the GPU, so it needed no shader change.

## A bug this found

`WaterSettings::Clamp` limited the body type to 0..3, but there are five types (Ocean, Lake, River, Pond, Custom = 4). Clamp runs on every edit, so choosing Custom turned the body into a Pond at its next change: Custom was unreachable. It is fixed, with a regression test. Finding it needed the settings to be testable at all, which is why `WaterSettings` moved out of `Water.h` (which needs Direct3D) into `WaterSettings.h`.

## How it was checked

- `tests/water_settings_test.cpp`: every body type survives Clamp; changing one setting marks exactly its own section; resetting a section touches nothing else; a preset takes the look and leaves the body; the foam clamp.
- `tests/water_preset_test.cpp`: the file round-trips exactly; what is not a look is not in the file; bad files are refused; out-of-range values are clamped; file names cannot escape the folder; a scene saved before the foam control loads with the foam it always had.
- The inspector itself was run headless in a real ImGui for every kind of water (no assertion across 120 frames), and its right-click menus were driven with simulated mouse input: about 690 control resets and 128 section resets, every one of them putting the right fields back. Two controls (the wind-source and wave-model menus) had no reset until that test found them.

## What this does NOT do

**No colour gradient and no foam colour.** Both need the shading rewrite (roadmap C), because the shader has the colours baked in.

**No search box.** With the sections and names it is not needed yet; if the panel grows again it will be.

**Underwater has only a section reset.** Its controls edit the underwater settings directly, not the water's.

## Questions before you read the code

1. Why is "Reset all sections" not the same as building a fresh `WaterSettings`, and what did the old button lose?
2. `GroupModified` compares floats within 1e-6. What would a plain `==` do to the dots on the controls?
3. Colour editors needed an explicit popup id for their reset menu. Why does a slider not?
4. Why does `ApplyPreset` leave out the Current and Performance sections?
