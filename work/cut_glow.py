"""Horizontal cut through the text to show the actual falloff shape."""
import numpy as np
from PIL import Image

def box_blur(a, r):
    k = 2 * r + 1
    pad = np.pad(a, ((r, r), (r, r)), mode="edge")
    c = np.cumsum(pad, axis=0); c = np.vstack([np.zeros((1, c.shape[1])), c])
    a2 = (c[k:] - c[:-k]) / k
    c = np.cumsum(a2, axis=1); c = np.hstack([np.zeros((c.shape[0], 1)), c])
    return (c[:, k:] - c[:, :-k]) / k

for path, r in [(r"X:\coding\DeepGlow\smoke_out\user_shot.png", 32),
                (r"X:\coding\DeepGlow\smoke_out\our_1920x1080.png", 40)]:
    a = np.asarray(Image.open(path).convert("RGB")).astype(np.float32) / 255.0
    h, w, _ = a.shape
    luma = 0.2126*a[:, :, 0] + 0.7152*a[:, :, 1] + 0.0722*a[:, :, 2]
    low = box_blur(luma, r)
    cy = h // 2
    # vertical cut through the middle of the text, and horizontal cut well away
    col = low[:, w // 2]
    print(f"\n{path}  ({w}x{h})")
    print("  horizontal cut at y=h/2, x from centre outward (every 4% of width):")
    for f in np.arange(0, 0.51, 0.04):
        x = int(w/2 + f*w)
        if x < w:
            print(f"    +{f*100:4.0f}% of w : {low[cy, x]:.4f}")
    print("  vertical cut at x=w/2, y from centre outward:")
    for f in np.arange(0, 0.51, 0.04):
        y = int(h/2 + f*h)
        if y < h:
            print(f"    +{f*100:4.0f}% of h : {low[y, w//2]:.4f}")
