#!/usr/bin/env python3
"""Turn 64x32 framebuffer PPMs into LED-matrix-style PNGs.

Usage: make_pngs.py <ppm dir> <png dir>
Each pixel becomes a round LED with a soft glow; unlit LEDs stay faintly
visible, like a real P3 panel.
"""
import sys
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter, ImageChops

PITCH = 10          # output pixels per LED
RADIUS = 3.6        # LED dot radius
PANEL = (10, 10, 12)
OFF = (26, 26, 30)


def render(src: Path) -> Image.Image:
    fb = Image.open(src).convert("RGB")
    w, h = fb.size
    size = (w * PITCH, h * PITCH)
    dots = Image.new("RGB", size, PANEL)
    lit = Image.new("RGB", size, (0, 0, 0))
    d_dots, d_lit = ImageDraw.Draw(dots), ImageDraw.Draw(lit)
    px = fb.load()
    for y in range(h):
        for x in range(w):
            cx, cy = x * PITCH + PITCH / 2, y * PITCH + PITCH / 2
            box = (cx - RADIUS, cy - RADIUS, cx + RADIUS, cy + RADIUS)
            c = px[x, y]
            if c == (0, 0, 0):
                d_dots.ellipse(box, fill=OFF)
            else:
                d_dots.ellipse(box, fill=c)
                d_lit.ellipse(box, fill=c)
    glow = lit.filter(ImageFilter.GaussianBlur(PITCH * 0.9))
    glow = Image.eval(glow, lambda v: int(v * 0.55))
    return ImageChops.add(dots, glow)


def main() -> None:
    src_dir, out_dir = Path(sys.argv[1]), Path(sys.argv[2])
    out_dir.mkdir(parents=True, exist_ok=True)
    for ppm in sorted(src_dir.glob("*.ppm")):
        render(ppm).save(out_dir / f"{ppm.stem}.png", optimize=True)


if __name__ == "__main__":
    main()
