# Lesson 71 - Asking the sea a question (O2, found in the editor)

## Understand

**What went wrong.** The editor ran at 8 FPS whenever the water was around. The cause was not the water's drawing but a question the scene asks every frame: "is the camera under the surface?" To answer it the scene asks the water for its height under the camera, and the CPU mirror answered by making a complete 256 x 256 height map with an FFT, every frame, to read one number from it. About 6 ms in an optimised build and many times that in a debug build, with no boat anywhere.

**The general lesson.** Cost should follow what is asked, not what could be asked. A structure that is expensive to build and cheap to read (the FFT map) is right when you read it thousands of times a frame, as the GPU does. For one question per frame it is the wrong tool; adding up the waves for that one point is cheaper.

## How it works

**The sea as a sum.** The height at x is the real part of a sum over modes: `value(k, t) e^{i k.x}`, where `value` is the spectrum turned to time t. That is exactly what the FFT computes for every texel at once. For one point you can do the sum yourself.

**Most modes do not matter.** A sea's energy sits in a small part of its modes. Sorting them by energy, the strongest 15 to 18 thousand of 98 thousand carry all but 0.001% of the variance. The rest add under a centimetre even in a storm, so they are dropped. A mode and its partner `-k` are kept or dropped together, which keeps the sum real.

**Turning to the time.** `Update(t)` computes `value` for every kept mode: one sine and cosine each, 0.26 ms. Asking again for the same time costs nothing.

**The trick that makes a query cheap.** `e^{i(kx x + kz z)} = e^{i kx x} e^{i kz z}`: a mode's x factor depends only on its column and its z factor only on its row. So a query makes two short tables (one entry per column, one per row) by repeated multiplication, with no trigonometry per mode, and each mode costs one complex multiply-add. The modes are stored sorted by row, so a row's modes are summed first and then multiplied by the row's z factor once. About 18 microseconds per query. Negative frequencies are the conjugates of the positive ones, so the table is half the work.

**Dry ground.** `SampleHeight` first asks how deep the water is. Where the ground is dry the shore limit flattens the sea to nothing, so the answer is just the water level and the sea is not summed at all. A camera standing on the beach is over the water body's flat plane but over dry sand.

## What the tests show

**Right answer.** The direct sum agrees with adding up every one of the 98 thousand modes (the brute force) to a few millimetres, at positions up to kilometres away. At the texels of each band it agrees with the FFT's value. Shifting by whole patches changes nothing. The six ways to get it wrong (a missing conjugate, negative frequencies not conjugated, the x and z tables swapped, time running the wrong way, the partners not kept, a cache that ignores the time) are each caught.

**Cheap answer.** 18 thousand modes, 0.26 ms to update, 18 microseconds per query. The old mirror cost about 6 ms per frame regardless.

## Check yourself

1. A band has 256 x 256 modes and a query needs two tables. How many complex multiplications do the tables cost, compared with the 6 thousand modes that band keeps?
2. If you kept every mode instead of 18 thousand, what would each query cost, and why is that still no worse than the FFT for a single question per frame?
3. Twenty floating objects each ask for 8 points every frame. Which is cheaper now, this sum or the FFT map, and where would the crossover be?
