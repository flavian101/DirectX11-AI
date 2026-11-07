#!/usr/bin/env python3
"""Generates stand-in terrain layer textures (grass, rock, sand, snow) so the layered terrain has
something to show before you drop in real ones (e.g. from Poly Haven).

    pip install numpy pillow
    python tools/make_terrain_textures.py            # writes assets/Terrain/<Layer>/{albedo,normal,orm}.png

Each layer gets:  albedo.png (sRGB colour), normal.png (OpenGL-style tangent-space normal, ALPHA = height),
orm.png (R = ambient occlusion, G = roughness, B = metallic). All textures tile seamlessly: the noise
is built in the frequency domain, which is periodic by construction.
"""
import os
import sys
import numpy as np
from PIL import Image

SIZE = 512
rng = np.random.default_rng(7)


def noise(beta, seed_offset=0):
    """Tileable noise in 0..1. Higher beta = smoother (energy concentrated in low frequencies)."""
    white = np.random.default_rng(7 + seed_offset).standard_normal((SIZE, SIZE))
    fx = np.fft.fftfreq(SIZE)[:, None]
    fy = np.fft.fftfreq(SIZE)[None, :]
    f = np.sqrt(fx * fx + fy * fy)
    f[0, 0] = 1.0
    spectrum = np.fft.fft2(white) / (f ** (beta / 2.0))
    spectrum[0, 0] = 0.0
    out = np.real(np.fft.ifft2(spectrum))
    out -= out.min()
    return out / out.max()


def mix(a, b, t):
    t = t[..., None]
    return a * (1 - t) + b * t


def normal_from_height(h, strength):
    """OpenGL-style: R = slope toward +U, G = slope toward image-UP, B = out of the surface."""
    dx = (np.roll(h, -1, 1) - np.roll(h, 1, 1)) * 0.5
    dy = (np.roll(h, -1, 0) - np.roll(h, 1, 0)) * 0.5      # +y = image DOWN
    n = np.stack([-dx * strength, dy * strength, np.ones_like(h)], -1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return n * 0.5 + 0.5


def save(folder, albedo, height, normal_strength, occlusion, roughness):
    os.makedirs(folder, exist_ok=True)
    n = normal_from_height(height, normal_strength)
    rgba = np.concatenate([n, height[..., None]], -1)
    Image.fromarray((np.clip(rgba, 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA").save(os.path.join(folder, "normal.png"), optimize=True)
    Image.fromarray((np.clip(albedo, 0, 1) * 255 + 0.5).astype(np.uint8), "RGB").save(os.path.join(folder, "albedo.png"), optimize=True)
    orm = np.stack([occlusion, roughness, np.zeros_like(height)], -1)
    Image.fromarray((np.clip(orm, 0, 1) * 255 + 0.5).astype(np.uint8), "RGB").save(os.path.join(folder, "orm.png"), optimize=True)


def main(root):
    # Grass: green, with patches of yellow-green and fine blade-like grain.
    broad, fine = noise(2.5, 1), noise(0.6, 2)
    albedo = mix(np.array([0.13, 0.24, 0.06]), np.array([0.30, 0.42, 0.11]), 0.65 * broad + 0.35 * fine)
    height = 0.55 * fine + 0.45 * noise(1.2, 3)
    save(os.path.join(root, "Grass"), albedo, height, 5.0, 0.55 + 0.45 * height, 0.80 + 0.15 * fine)

    # Rock: grey-brown with ridged cracks (1 - |noise|) and big patches.
    ridged = 1.0 - np.abs(noise(2.0, 4) * 2.0 - 1.0)
    ridged = ridged ** 2.0
    patches = noise(2.8, 5)
    albedo = mix(np.array([0.22, 0.20, 0.18]), np.array([0.50, 0.47, 0.42]), 0.5 * patches + 0.5 * ridged)
    height = 0.7 * ridged + 0.3 * noise(1.0, 6)
    save(os.path.join(root, "Rock"), albedo, height, 9.0, 0.35 + 0.65 * height, 0.90 - 0.1 * ridged)

    # Sand: warm tan with fine grain and soft ripples. Its colours are in the TEXTURE; the editor's tints are
    # white by default and only adjust it.
    grain, ripples = noise(0.3, 7), noise(3.0, 8)
    albedo = mix(np.array([0.60, 0.51, 0.36]), np.array([0.80, 0.72, 0.54]), 0.55 * ripples + 0.45 * grain)
    height = 0.6 * ripples + 0.4 * grain
    save(os.path.join(root, "Sand"), albedo, height, 3.0, 0.8 + 0.2 * height, 0.95 - 0.05 * grain)

    # Snow: near-white with a faint blue cast, very soft undulation, a little sparkle.
    soft, sparkle = noise(3.0, 9), noise(0.2, 10)
    albedo = mix(np.array([0.86, 0.90, 0.96]), np.array([0.97, 0.98, 1.00]), 0.7 * soft + 0.3 * sparkle)
    height = 0.8 * soft + 0.2 * sparkle
    save(os.path.join(root, "Snow"), albedo, height, 2.0, 0.85 + 0.15 * height, 0.55 + 0.1 * sparkle)
    print("wrote", root)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.join("assets", "Terrain"))
