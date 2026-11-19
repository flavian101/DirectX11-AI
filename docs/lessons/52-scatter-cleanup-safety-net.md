# Lesson 52 - Cleaning up scatter without moving a tree (S11, first half)

## Understand

**A refactor promises nothing changes.** Deleting dead code and merging structs should leave every tree where it was. The only way to know is to measure before and after, so the measuring comes first.

**The golden test is that measurement.** `tests/scatter_golden_test.cpp` grows the starter scatter (pines and four-look boulders) over a fixed made-up ground and hashes every instance. The hash was recorded on the unchanged code, then the deletions started. Same hash after means same forest.

**Hash the rounded numbers, not the raw bytes.** `sin` and `acos` can differ in the last bit between compilers, so positions are rounded to 1 cm and scale and rotation to 0.001 before hashing. A real change (one slope limit moved by one degree) still changes the hash; I checked that by making exactly that edit and watching the test fail.

**Your machine may need its own reference.** My hashes come from g++ on Linux. Run the test on the unchanged code first; if MSVC prints different hashes, put yours into the two constants, then apply the cleanup. What the test protects is before equals after on one machine.

## What was removed

**`GenerateAllNow` had no caller.** Not the editor, not a test. It is gone.

**The species-level `modelHeight` was never set.** `MakeBaseRules` left it at 1.0, and a variant only fell back to it when the variant could not be drawn, which has weight 0 and is never chosen. A variant's `modelHeight` is now the only one, default 1.0.

**`memcmp` became `operator==`.** `ScatterObject::Refresh` compared two `ScatterRules` byte by byte to decide whether to place a species again. That works only while every member is 4 bytes with no padding. The comparison is now written field by field, and two `static_assert`s on the struct sizes stop the build when someone adds a field and forgets to extend it.

**The comparison has its own test.** `TestRulesComparison` changes one field at a time and checks each change is seen, and that a variant beyond `variantCount` is ignored.

## Still to do in S11

**Three items are left.** Declare each rule once (`ScatterSpeciesSettings` holds a `ScatterRules`), always store variants (drop `model` and `EffectiveVariants`), and move the scatter inspector out of `EditorLayer.cpp`. They touch the JSON and the editor, so they get their own lesson and the same golden test.

## Check yourself

1. The two `static_assert`s protect against forgetting a field in `operator==`. What would still go wrong if you forgot a field and the struct size happened to stay the same?
2. Why does the golden test call `RulesFor(...)` instead of building `ScatterRules` inline? What changes in the next step, and what must not?
3. A rule is added on purpose and the starter forest now looks different. Which constants change, and where do you write down why?
