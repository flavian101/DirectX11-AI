# Lesson 18 - Loading models from files with Assimp

## Understand

**A model file is a bundle of parts, materials and a family tree.** Each *mesh* is a list of vertices and triangles that uses one *material*. Meshes hang off *nodes*, and each node carries a transform relative to its parent. Assimp reads glTF, FBX, OBJ and many more into one common structure, so we write the loading code once.

**Two different jobs, kept apart.** `ModelImporter` turns a file into plain CPU data (vertices, indices, a description of each material). `ModelObject` takes that data and creates the GPU objects. The importer needs no graphics device, which is why I could test it on real glTF, GLB and OBJ files without opening a window.

## Visualize

**File > Import Model... in the editor.** Pick a `.glb` or `.gltf`. The model lands 20 units in front of the camera, 10 units long, with its base at the point you are looking at. Select it: the Inspector shows its parts and triangles and one entry per material. Open one and try "Debug view > Mapped normal", exactly as on the cube.

## Why it is built this way

**Assimp converts to our conventions in one flag.** glTF and OBJ are right-handed; Direct3D (and this engine) is left-handed with clockwise front faces and V running downward. `aiProcess_ConvertToLeftHanded` flips the Z axis, reverses the triangle winding and flips V. The test checks that every triangle's face direction still agrees with its normal afterwards.

**We bake the node tree into the vertices.** The importer walks the nodes, multiplying transforms on the way down, and applies the result to each mesh's positions. Normals use the *inverse transpose* of that matrix, or a non-uniform scale would skew them. A node that mirrors the model turns its triangles inside out, so the importer detects a negative determinant and swaps two indices. The cost: the pieces can no longer move independently. For scenery that is fine.

**We compute tangents ourselves.** Assimp can do it, but our own `ComputeTangents` (lesson 16) follows exactly the convention the shader uses, so a model looks the same as the cube.

**Files lie about their texture paths.** Artists save absolute paths from their own PC, with backslashes, or with `%20` for spaces. `ResolveTexturePath` tries the path as written, then the file name beside the model, then a `textures` folder. Textures can also be *embedded* inside a `.glb`; those are decoded in memory and handed to the GPU with `Texture::FromPixels`.

**glTF splits one map into two, we want one.** glTF stores occlusion in one texture and roughness/metallic in another. Our shader reads one ORM texture (R = occlusion, G = roughness, B = metallic). If they are different files, the importer repacks them. The R channel of glTF's metallic-roughness map is *undefined*, so it is always overwritten, or garbage there would make the model dark.

**"Bump" in OBJ can mean two things.** The same keyword holds height maps and normal maps. A normal map is mostly (0.5, 0.5, 1.0), a blue picture, so the importer looks at the average colour and only treats it as a normal map if it is blue.

**Per-material factors.** A glTF material carries a base colour factor, a roughness factor and an alpha mode. The base colour multiplies the albedo; roughness multiplies the ORM map's green (or *is* the roughness when there is no map). `MASK` materials become a cut-out: pixels below the alpha cutoff are `discard`ed in the shader. `BLEND` is approximated the same way, because real blending needs back-to-front sorting.

**One draw per part, grouped by material.** Each Assimp mesh becomes a `Renderable`. Parts are sorted by material so the four texture slots change as rarely as possible. Double-sided materials (leaves, cloth) switch the rasterizer to "no culling" for their draw and restore the previous state after.

**Scale and pivot.** Files arrive in any unit. `FitToSize(10)` scales the longest side to 10 units, and the pivot is the bottom centre of the model's box, so scaling and rotating happen about its base, and "position" says where the base sits.

**The Open dialog changes your working directory, unless you stop it.** `OFN_NOCHANGEDIR` is essential: without it, picking a file in another folder moves the program's current directory there, and every relative path afterwards ("shaders/vs.hlsl", "assets/...") fails.

## Other ways, and the trade-offs

**Keep the node hierarchy.** Meshes stay parented to nodes, so parts can be moved, hidden or animated individually. More code and a scene graph. **Merge parts that share a material.** Far fewer draw calls for something like a voxel castle, at the cost of losing per-part culling.

**Your own binary format.** Importing a glTF every time is slow for big models. Real engines convert once to a fast format and load that at runtime. **Load on a background thread.** The editor currently freezes while a big model imports.

## Predict, then check

1. A mirrored node (scale -1 on X) has a negative determinant. What would you see if the importer did nothing about it, and why?
2. Why is the roughness scale `factor / 0.5` when there is no ORM map, but just `factor` when there is?
3. What does `OFN_NOCHANGEDIR` protect?

**Answers.** (1) The model looks hollow: the back faces are drawn and the front faces are culled, because the mirror reversed the winding. (2) The neutral stand-in ORM map has green = 0.5, and the shader multiplies the map by the scale, so we need 0.5 * scale = factor. A real map already holds the file's own values and the glTF rule is `factor * texture`. (3) The program's working directory, which every relative asset path depends on.
