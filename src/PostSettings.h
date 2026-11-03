#pragma once
#include <algorithm>

// Which curve squeezes unlimited HDR brightness into the 0..1 range a screen can show.
// The numbers are sent to the shader, so they must match the switch in shaders/resolve_ps.hlsl.
enum class ToneMapper : int {
    Clamp = 0,      // no tone mapping: anything above 1.0 turns white (to SEE the problem)
    Reinhard = 1,   // x / (1 + x): simple, never clips, but washes out contrast
    ACES = 2,       // filmic S-curve (Narkowicz fit): punchy contrast, soft highlights
    Filmic = 3      // "Uncharted 2" curve (Hable): strong toe and shoulder, warm look
};

// How the finished HDR image is turned into what the monitor shows.
struct PostSettings {
    bool srgbEncode = true;                   // apply the linear -> sRGB curve (off only to SEE why it matters)
    float exposureEV = 0.0f;                  // exposure in stops: +1 doubles the light, -1 halves it
    ToneMapper toneMapper = ToneMapper::ACES;

    static constexpr float kExposureMin = -5.0f, kExposureMax = 5.0f;

    void Clamp()
    {
        exposureEV = std::clamp(exposureEV, kExposureMin, kExposureMax);
        const int mode = std::clamp(static_cast<int>(toneMapper), 0, 3);
        toneMapper = static_cast<ToneMapper>(mode);
    }
};
