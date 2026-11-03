#pragma once

// How the finished HDR image is turned into what the monitor shows.
struct PostSettings {
    bool srgbEncode = true;   // apply the linear -> sRGB curve (turn off only to SEE why it matters)
};
