import numpy as np
from PIL import Image

im = Image.open(r"X:\coding\DeepGlow\smoke_out\user_shot.png").convert("RGB")
a = np.asarray(im).astype(int)
g = a.max(axis=2)

print("min", g.min(), "max", g.max())
vals, counts = np.unique(g, return_counts=True)
print("distinct grey levels:", len(vals))
for v, c in zip(vals.tolist(), counts.tolist()):
    if c > 50:
        print(f"  {v:3d} : {c}")

ys, xs = np.nonzero(g > 120)
if len(ys):
    print("bright pixels bbox y", ys.min(), ys.max(), "x", xs.min(), xs.max(), "count", len(ys))
    print("brightest", g.max(), "at", np.argwhere(g == g.max())[:5].tolist())
else:
    print("no pixels above 120")
