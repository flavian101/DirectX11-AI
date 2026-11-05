# Lesson 20 - Selecting, moving and managing objects

## Understand

**A scene editor is three loops.** *Select* (click something), *manipulate* (drag a handle), *manage* (add, duplicate, delete). Each needs a different piece of maths or bookkeeping; none needs new rendering.

**A click on the image is a line through the world.** The 3D view is a flat picture, but every pixel stands for a whole ray of points that would land on it. Selecting means finding the first object that ray hits.

## Visualize

**Click a cube or the ship in the Viewport.** An orange box outlines it and the Inspector fills in. Press `1`, `2`, `3` for move, rotate, scale, and drag a handle. Hold Ctrl to snap. Press `F` to fly the camera to it, `Ctrl+D` to duplicate it, `Del` to delete it, `Esc` to let go. Right-click in the Hierarchy for the same actions.

## Why it is built this way

**From pixel to ray: unprojecting.** Rendering turns a world point into a screen point with `view * projection`, then a divide by w. To go backwards, take the click's position in *normalised device coordinates*, add a depth, and multiply by the inverse of `view * projection`. Direct3D's depth runs 0 (near plane) to 1 (far plane), so unprojecting depth 0 and depth 1 gives two points on the same ray. The line through them is the click. Two details cost me nothing to get right in a test and a lot to debug on screen: screen y grows *downward* but NDC y grows *upward* (hence `1 - ...`), and the click must be measured relative to the *image's corner*, not the window's.

**Testing the ray against a box.** A rotated box is awkward; an axis-aligned one is easy. So instead of rotating the box into the world, carry the *ray* into the box's own space with the inverse of its world matrix, where the box is axis-aligned again. The "slab" test then asks, for each of the three pairs of planes, when the ray enters and leaves; it hits only if the three intervals overlap. The direction is transformed *without* renormalising, so the distance comes out in world units.

**Nearest wins.** Every pickable object tests the ray; the smallest distance is selected. Terrain, water and the sky have no oriented bounds, so they cannot be clicked (select them in the Hierarchy).

**Boxes are coarse.** A box is a loose fit for a ship with a mast. Clicking the empty corner of its box still selects it. Exact picking needs triangles or a GPU pass; see below.

**Selection is an id, not an index.** Indices change when something is added or deleted; ids do not. Everything that used `selectedObject_` now asks the scene `Find(id)`.

**Actions are queued, not executed.** A menu item or right-click inside the Hierarchy loop sets a flag; the real work happens at the start of the *next* frame, before any list is drawn. Deleting the object whose row you are in the middle of drawing is how editors crash.

**The gizmo edits one matrix; objects store three vectors.** `ComposeTransform` builds the matrix the same way every object builds its world matrix (scale, rotate X, Y, Z, translate). While you drag, `DecomposeTransform` reads position, scale and rotation back out: the translation is the last row; each of the first three rows is an axis stretched by the scale, so its length is the scale; divide the scale out and a pure rotation is left. Multiplying `Rx * Ry * Rz` out shows that `r[0][2] = -sin(b)` and the other two angles come from `atan2` of neighbouring entries. When b is +-90 degrees two of the rotations line up (*gimbal lock*) and only their sum is defined; we keep it all in X. I checked the round trip on 20,000 random rotations and on every gimbal case.

**The gizmo sits at the object's pivot.** A model's world matrix includes a shift that moves its base to the origin. The gizmo uses the *editor transform* (position, rotation, scale), so its handles sit where the base is, which is also where you expect them.

**Left-handed worlds.** ImGuizmo works out the handedness from the projection matrix (`m[2][3]` is negative for right-handed, positive for left-handed), so our left-handed camera works without changes.

**Click versus handle.** A click on an arrow must not also select whatever object is behind it. The gizmo is drawn first; the click only picks if the pointer is not over a handle.

**Ctrl and keys.** Ctrl+D contains a D, and D moves the camera. While Ctrl is held the scene ignores the keyboard, just as it does while you type into a text box.

**Fixtures versus content.** Cubes and imported models are *user content*: they can be duplicated and deleted. Terrain, water and sky are fixtures; the editor will not delete them, because the rest of the scene looks for them.

**The demo cube spun by itself.** That fights the gizmo, so moving the cube by hand (gizmo or Inspector) switches the spin off, and the Inspector has a Spin checkbox.

## Other ways, and the trade-offs

**ID-buffer picking.** Draw every object in a flat colour that encodes its id into a hidden image, then read the one pixel under the cursor. Exact, and works for any shape, but every object type needs a picking draw path. **Triangle picking** with a bounding-volume tree is exact too and needs the triangles kept on the CPU.

**Your own gizmo** is a good exercise (a ray-versus-axis distance test per handle) and a lot of work for rotation and scale. **Undo and redo** is usually a stack of small command objects that each know how to apply and reverse themselves; it is the natural next addition.

## Predict, then check

1. The click is at the very top-left pixel of the image. What are its NDC x and y?
2. Why is the ray's direction left un-normalised inside the box test?
3. What goes wrong if you delete an object inside the loop that draws the hierarchy?

**Answers.** (1) x = -1, y = +1: NDC y is up, screen y is down. (2) So the number of steps along the ray equals world units no matter how the box is scaled, which lets us compare distances between objects. (3) The list you are walking changes under you, so you can skip or crash; queuing the action for the next frame avoids it.
