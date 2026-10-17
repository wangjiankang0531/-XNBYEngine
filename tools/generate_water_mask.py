#!/usr/bin/env python3
"""Generate the global water mask (texture/earth/water_mask.png).

Classifies ocean from the repo's own blue-marble base textures
(texture/earth/0_0_0.jpg west + 0_1_0.jpg east, 4096x4096 each), cleans the
result morphologically, and writes an 8-bit grayscale equirectangular mask
(8192x4096, 255 = water). GlobeRenderer samples it by lat/lon in the tile
shader so the ocean water effect (Fresnel + sun glint) is gated by a globally
CONSISTENT mask instead of per-tile imagery color — which removes the blocky
water-boundary artifacts that patchy imagery (esp. Tianditu deep-sea tiles)
produces.

Run from the repo root:  python3 tools/generate_water_mask.py
Idempotent; regenerates the PNG from the jpgs every run.
"""

from PIL import Image, ImageFilter

WEST = "texture/earth/0_0_0.jpg"
EAST = "texture/earth/0_1_0.jpg"
OUT = "texture/earth/water_mask.png"


def classify(im: Image.Image) -> Image.Image:
    """Per-pixel water classification on a blue-marble RGB image."""
    rgb = im.convert("RGB")
    px = rgb.load()
    w, h = rgb.size
    out = Image.new("L", (w, h))
    op = out.load()
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            # Blue-marble ocean: blue-dominant, dark-to-mid luminance.
            # Land is green/brown/white (ice). Wider than the runtime GLSL
            # classifier — the morphology pass cleans up stragglers.
            luma = 0.299 * r + 0.587 * g + 0.114 * b
            blueness = (b + g) * 0.5 - r
            is_water = b > r + 8 and b > g and blueness > 6 and luma < 150
            op[x, y] = 255 if is_water else 0
    return out


def main() -> None:
    west = Image.open(WEST)
    east = Image.open(EAST)
    assert west.size == east.size, "hemisphere sizes differ"
    w, h = west.size

    print(f"classifying west {west.size} ...")
    mw = classify(west)
    print(f"classifying east {east.size} ...")
    me = classify(east)

    mask = Image.new("L", (w * 2, h))
    mask.paste(mw, (0, 0))
    mask.paste(me, (w, 0))

    # Morphological cleanup: close (dilate->erode) fills speckle holes in the
    # ocean (sensor noise, sun glint in the source photo), then open
    # (erode->dilate) removes isolated water speckles on land. MaxFilter =
    # dilate for 255=water, MinFilter = erode.
    print("morphology ...")
    mask = mask.filter(ImageFilter.MaxFilter(5)).filter(ImageFilter.MinFilter(5))
    mask = mask.filter(ImageFilter.MinFilter(5)).filter(ImageFilter.MaxFilter(5))

    # Soft edge: slight blur so the shader transition fades over a few mask
    # texels instead of stair-stepping at coastlines.
    mask = mask.filter(ImageFilter.GaussianBlur(1.5))

    mask.save(OUT, optimize=True)
    print(f"wrote {OUT} ({mask.size[0]}x{mask.size[1]})")


if __name__ == "__main__":
    main()
