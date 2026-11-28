# Lesson 68 - The boats' sea: the CPU mirror (O2)

> **Revised by lesson 71.** The mirror described here made a whole height map with an FFT every frame. The scene asks for the height under the camera every frame, so that cost was paid with no boat in sight (8 FPS in the editor). It now adds up the strongest waves directly; lesson 71 explains how. The ideas below (why the CPU needs its own copy, the packing trick, the bound, the sideways-push limit) still hold.

## Understand

**Why the CPU needs its own copy.** The GPU makes the sea you see, but a boat, a swimmer or a splash is gameplay code on the CPU. Reading the GPU's texture back every frame would stall the pipeline. So the CPU computes the same sea itself, from the same spectrum, at the same time. Both start from the same numbers, so they agree.

**Only the height.** The GPU makes eight fields (lesson 64); a boat needs one. So the mirror does one FFT per band instead of four.

**Packing again.** Two bands of the same size can share one FFT, exactly like two fields did on the GPU: `IFFT(A + iB) = a + ib` when A and B are both conjugate-symmetric. The default bands are 128, 256 and 128 modes, so the two 128 ones share, and an update is two FFTs, not three.

## How it works

**`EvolveHeights`.** For each mode: the same `h0(k) e^{-iwt} + conj(h0(-k)) e^{+iwt}` as the GPU; for a pair, `value_a + i value_b`; one 2-D inverse FFT; the real part is band a's height, the imaginary part band b's.

**Sampling.** A patch repeats, so a world position is wrapped into the patch and the four nearest heights are blended (`Surface::HeightAt`). The bands are added. The GPU samples its height texture with the same wrap and a bilinear blend, so the two agree to the GPU's half-float precision.

**`Update(t)` is a function of time.** It remembers the time it last ran for, so a boat asking five times in a frame costs one update. Going back in time (a paused or rewound scene) gives the same sea again, because nothing depends on history.

**The height bound.** The mesh margins and "is this tile under dry ground?" need a height the sea does not pass. The sea's height is close to a Gaussian with a standard deviation sigma that the spectrum gives exactly (the sum of the energy of its modes). Six sigma is practically never reached: in the test the highest crest of two million samples was 4.4 to 5.1 sigma. Too small a bound would drop water that is on screen; too large only draws a bit more.

## What it costs, and what it cannot do

**Cost.** About 5 ms per update in an optimised build, paid only when something asks and only once per time. With no boats it costs nothing. If many boats make that too much, the next steps are a float FFT, a worker thread, or sampling fewer bands.

**The sideways push.** The water on screen is the height field read at p and drawn at `p + choppiness x D(p)`. A boat reads the height at the position it is, so near a steep crest it can be off by up to `choppiness x D` along the wave: centimetres in a breeze, up to about a metre in a storm. Closing it means iterating `p = x - choppiness x D(p)`, which needs the displacement on the CPU too (two more fields). We leave it until it shows.

## What the tests show

**Same sea.** Every band's heights equal `OceanFFT::Evolve`'s to float rounding, in deep and shallow water, and so does the sum at 200 positions up to kilometres from the origin, negative ones included. Pairing the bands changes nothing. A swapped real and imaginary part, a missing conjugate, a missing factor of i and a cache that ignores the time each make the test fail.

## Check yourself

1. Why must the two paired bands have the same resolution, and what would you do if the default bands were 128, 256 and 512?
2. The bound is 6 sigma. If you changed it to 3, what would you see on screen, and in which situation first?
3. A boat's `SampleHeight` is called with `time_` from the scene. The GPU's water is drawn with the same clock. What would the boat do if the CPU mirror used a clock that ran 0.5 s behind?
