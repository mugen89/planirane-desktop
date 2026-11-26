"""Draws the application icon (a white stopwatch on blue) and writes res/app.ico.

Small sizes get thicker strokes so the tray icon stays readable.
"""
import math
import os

from PIL import Image, ImageDraw

SS = 8  # supersampling


def render(size):
    S = size * SS
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    pad = S * 0.04
    d.rounded_rectangle([pad, pad, S - pad, S - pad], radius=S * 0.22, fill=(29, 78, 216, 255))
    # subtle top light
    d.rounded_rectangle([pad, pad, S - pad, S * 0.55], radius=S * 0.22, fill=(37, 99, 235, 255))
    d.rectangle([pad, S * 0.33, S - pad, S * 0.55], fill=(37, 99, 235, 255))
    small = size <= 24
    w = S * (0.10 if small else 0.075)
    cx, cy, r = S * 0.5, S * 0.56, S * (0.27 if small else 0.28)
    white = (255, 255, 255, 255)
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=white, width=int(w))
    # crown
    d.rectangle([cx - w * 0.6, cy - r - w * 1.6, cx + w * 0.6, cy - r], fill=white)
    d.rectangle([cx - w * 1.6, cy - r - w * 2.3, cx + w * 1.6, cy - r - w * 1.3], fill=white)
    # hand toward two o'clock
    a = math.radians(60)
    hx, hy = cx + math.sin(a) * r * 0.62, cy - math.cos(a) * r * 0.62
    d.line([cx, cy, hx, hy], fill=white, width=int(w))
    d.ellipse([cx - w * 0.75, cy - w * 0.75, cx + w * 0.75, cy + w * 0.75], fill=white)
    return img.resize((size, size), Image.LANCZOS)


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    imgs = [render(s) for s in sizes]
    out = os.path.join(root, "res", "app.ico")
    imgs[-1].save(out, format="ICO", sizes=[(s, s) for s in sizes], append_images=imgs[:-1])
    print("wrote", out)


if __name__ == "__main__":
    main()
