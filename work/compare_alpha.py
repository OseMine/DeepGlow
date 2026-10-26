"""Composite our 1920x1080 render over Resolve's transparency grid, then put it
next to the user's screenshot for a like-for-like comparison."""
import numpy as np
from PIL import Image

W, H = 1920, 1080
SQUARE = 33          # checker square in comp px (15 px in the 877-wide view)


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    # P6\n<w> <h>\n255\n
    parts = data.split(b"\n", 3)
    w, h = [int(v) for v in parts[1].split()]
    px = np.frombuffer(parts[3], dtype=np.uint8)[: w * h * 3].reshape(h, w, 3)
    return px


rgb = read_ppm(r"X:\coding\DeepGlow\smoke_out\our_1920x1080.ppm").astype(np.float32) / 255.0

# alpha comes from the same render; re-read it from a float dump is not
# available, so rebuild it from the glyph mask: everything non-zero in rgb is
# either glyph or glow, and the glyphs are the saturated pixels.
alpha = np.clip(rgb.max(axis=2) * 3.0, 0.0, 1.0)      # only used for the "glyph" core
# The real alpha is the source alpha: hard 1 inside the glyph strokes. Detect
# them as pixels at full white.
glyph = (rgb.max(axis=2) > 0.99).astype(np.float32)

yy, xx = np.mgrid[0:H, 0:W]
checker = np.where(((xx // SQUARE) + (yy // SQUARE)) % 2 == 0, 52.0, 80.0) / 255.0
checker = checker[:, :, None].repeat(3, axis=2)

# premultiplied over: rgb + (1 - a) * background
over = rgb + (1.0 - glyph[:, :, None]) * checker
over = np.clip(over, 0, 1)

ours = Image.fromarray((over * 255.0 + 0.5).astype(np.uint8)).resize((877, 489), Image.LANCZOS)

shot = Image.open(r"X:\coding\DeepGlow\smoke_out\user_shot.png").convert("RGB")

canvas = Image.new("RGB", (877, 489 * 2 + 12), (24, 24, 24))
canvas.paste(shot, (0, 0))
canvas.paste(ours, (0, 489 + 12))
canvas.save(r"X:\coding\DeepGlow\smoke_out\compare_alpha.png")
print("wrote compare_alpha.png (top = your render, bottom = fixed build, synthetic type)")
