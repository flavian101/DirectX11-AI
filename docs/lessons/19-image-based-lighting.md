# Lesson 19 - Image-based lighting (IBL) and physically based shading

## Understand

**The sky is a light, and it lights everything.** Until now the sun and point lights did the work and "ambient" was one flat colour added everywhere. In the real world most light arrives from the whole sky and the things around. IBL treats the sky image as a light source: every direction of the sky is a tiny light.

**Two different questions, two different answers.** A *diffuse* surface (chalk, wood) scatters light, so it needs the *average* of the sky over a half-sphere. A *glossy* surface (metal, wet stone) reflects, so it needs the sky in *one mirror direction*, blurred according to how rough it is.

## Visualize

**Lights panel > Image-based lighting.** Tick "Sky lighting" off and on and watch the shadowed sides of the ship and the terrain: flat colour against sky-coloured light. Set "Show as sky background" to *Irradiance*: you see the sky as a very blurry version of itself, which is exactly what a diffuse surface receives. Switch to *Prefiltered* and drag the mip slider: mip 0 is the sharp sky, the last mip is a smear. Below, the BRDF table is drawn as a red/green picture.

**Select a model's material** and drag "Metallic" up (set "Roughness" low). It stops showing its own colour in the light and starts showing the sky.

## Why it is built this way

**Precompute, because integrating is slow.** Averaging the whole sky for every pixel would cost thousands of reads per pixel per frame. The result only depends on the *direction*, so we bake it once into small textures: `EnvironmentLighting::Bake` runs three full-screen passes on the GPU on the first frame.

**Bake 1: irradiance (diffuse).** For each direction N we average the sky over the hemisphere around N, weighting each sample by cos(angle) because light arriving at a slant is spread over more surface. We store E/pi, so a surface multiplies it by its albedo. The sum steps through angles at the *midpoints* of equal steps: stepping from the edge made the result about 2 % dark, which I measured in a Python copy before writing the shader.

**Bake 2: prefiltered specular.** A rough surface sees a blurred sky, and the blur grows with roughness. We bake one blurred sky per roughness and store them as the **mip levels** of one cube map (mip 0 = mirror, last mip = chalk). Samples are chosen by *importance sampling* the GGX distribution: instead of random directions, we pick the ones that matter most for that roughness, using a low-discrepancy sequence (Hammersley) so they cover the area evenly. Each sample reads a smaller mip of the original sky if its patch of sky is large, which removes speckle.

**Bake 3: the BRDF table.** The prefilter assumes the viewer looks straight down the normal. The table corrects that: for each view angle and roughness it stores a scale and a bias for the surface's base reflectivity. It depends on nothing else, so it is baked once. The shader then does `prefiltered * (F * scale + bias)`: two lookups instead of an integral. This is the "split sum" approximation.

**Face mapping must match the GPU.** Drawing into a cube map face means working out which world direction each pixel stands for. The six formulas in `ibl_common.hlsli` are the exact inverse of how the GPU turns a direction into a face and texel; I checked it by round-tripping 20,000 random directions (error 1e-16).

**The shading itself (Cook-Torrance).** The highlight is `D * G * F / (4 N.V N.L)`: D says how many tiny facets point along the half vector, G how much they shadow each other, F is Fresnel (every surface becomes a mirror at a grazing angle). Whatever is reflected (F) cannot also be absorbed and re-emitted, so diffuse gets `(1 - F)`, and metals (`metallic = 1`) get no diffuse at all. A metal's reflectivity F0 is its own colour; everything else is about 4 %.

**Our light units did not change.** Textbook lighting divides diffuse by pi. We multiply the light by pi to cancel that, so a white surface facing a light with intensity 1 still shows 1. Your existing light settings keep their meaning.

**Binding and states.** Three new slots (t11 to t13) hold the baked maps. Four new floats in the per-frame buffer say whether IBL is on and how strong. The bake's own parameters live in `b5`, so it cannot disturb the scene's buffers, and the baker restores the one sampler it replaces.

**Metallic and roughness needed a clearer rule.** With an ORM map the shader multiplies the map by a scale. Without one, the neutral map is white and the settings' *fallback* roughness and metallic take the scale's place. This replaced a workaround from lesson 18.

**Limits.** The sky is an ordinary 8-bit picture, so its sun is no brighter than its clouds; real HDR skies make IBL much punchier. The sky light is not shadowed (that needs ambient occlusion, which only the baked occlusion map gives). Terrain is treated as non-metal.

## Other ways, and the trade-offs

**Spherical harmonics** store the diffuse sky in 9 numbers instead of a cube map: tiny, but only for diffuse. **Reflection probes** bake the *scene* (not just the sky) at several places so a room reflects its walls. More realistic, more machinery. **Screen-space reflections** reflect what is on screen: sharp and local, with gaps at the edges.

## Predict, then check

1. Why does a mirror-like surface read mip 0 of the prefiltered map, and a chalky one the last mip?
2. What happens to a metal with `metallic = 1` if the sky is black?
3. Why is the sun's shadow applied to the direct light but not to the sky light?

**Answers.** (1) Each mip holds the sky blurred for one roughness: none for a mirror, almost a flat average for chalk. (2) It goes nearly black: metals have no diffuse, and all they show is reflected light; with nothing to reflect (and no sun on it) there is nothing to see. (3) The shadow map only knows what blocks the *sun*. The sky comes from every direction, so one shadow map cannot say what blocks it; that needs a different technique, which is why the baked occlusion map matters.
