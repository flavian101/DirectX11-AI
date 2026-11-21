# Lesson 61 - The file of a baked tile (C6)

## Understand

**Most tiles need no file.** A tile is made from the ground function, and what you changed is kept as edits in the `.worldedits` file. A baked tile is different: it is the finished result stored as it is. That is what an imported world, a tile cache, or a hole mask for caves and tunnel mouths needs, because no function can make a hole.

**The format comes before the reader.** Nothing reads this file yet. Fixing the layout and the version number first means whatever reads it later is written against something that cannot drift.

**A file outlives the program.** A file saved today will be opened by a program that has changed. So the file says which version of the layout it uses, and the program says clearly when it meets a version it does not know.

## How it works

**A short header, then the data.** The header is 20 bytes: a magic `DXWT` so a wrong file is recognised at once, the version, flags, the number of cells, and which tile it is. After it come the heights, the layer weights, and the holes if there are any.

**Heights take 16 bits.** A height is 0 to 1 of the ground's range, stored as 0 to 65535. That is 2 bytes a sample instead of 4. A round trip is off by at most half a step, 7.6 millionths of the range, which is 1.8 mm on a 240 m hill.

**Layer weights are four bytes a cell.** They are the same grass, rock, sand and snow bytes `SplatMap` holds, one texel per cell, adding up to 255. A texel that does not add up is refused as bad content.

**Holes are one bit a cell.** A tile with no holes has no mask at all, and a flag in the header says which. In memory a hole is a byte, in the file it is a bit.

**Every number is written byte by byte, low byte first.** The file is the same on every machine, whatever the machine's own byte order.

**A checksum closes the file.** The last four bytes are a CRC-32 of everything before them, the same one zip and PNG use. A flipped byte anywhere is found.

**Newer is not damaged.** A reader checks the version first. A file from a newer program gets its own answer, "saved by a newer version", before anything else about its layout is trusted. Version 0 never existed, so it is refused.

**Saving cannot leave half a tile.** The bytes go to a temporary file beside the target and are renamed into place, so a crash leaves the old file or the new one.

## What the tests show

**The layout is frozen.** One test writes a small tile and compares it with a byte vector made by Python's `zlib`, not by this code, so the checksum is checked by something else. If that test ever fails, the layout changed, and the fix is a new version number.

**Damage is found.** Every change of every byte, three different ways, is refused. So is every shorter length and a longer file. None of them crashes, and nothing half-read is left behind.

**Not used yet.** The first readers will be the hole mask and a tile cache. Whether the file should also carry the world settings it was made with is left for them to decide.

## Check yourself

1. Why does the reader check the version before it checks the size and the checksum?
2. A file has 3 cells in its header but holds the data of 2. Which answer does the reader give, and why not "bad checksum"?
3. Why is the checksum computed over the whole file and not only over the heights?
