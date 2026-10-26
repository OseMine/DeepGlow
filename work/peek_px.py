import numpy as np
from PIL import Image

a = np.asarray(Image.open(r"X:\coding\DeepGlow\smoke_out\user_shot.png").convert("RGB")).astype(int)
print("shape", a.shape)
pts = [(244, 300, "inside a stroke"), (244, 290, "inside stroke2"),
       (244, 180, "halo left of text"), (244, 120, "further out"),
       (300, 436, "halo below text"), (340, 436, "below, further"),
       (40, 40, "corner bg"), (300, 60, "left bg"), (400, 700, "bottom-right bg"),
       (244, 650, "right of text"), (244, 760, "far right")]
for y, x, label in pts:
    print(f"{label:22s} y={y:3d} x={x:3d} rgb={a[y, x]}")

for name, sl in [("corner 20:120,20:120", (slice(20, 120), slice(20, 120))),
                 ("bg bottom-right", (slice(380, 460), slice(640, 820)))]:
    patch = a[sl[0], sl[1], 0].ravel()
    vals, counts = np.unique(patch, return_counts=True)
    print(name, "greys:", list(zip(vals.tolist(), counts.tolist()))[:8])
