"""Measure the visible glow in a Resolve/OFX screenshot.

The frame sits on Resolve's transparency checkerboard.  The checker is a
high-frequency pattern of two constant greys, so a box average wider than one
checker square removes it and leaves (constant + glow).  Everything below is
scale invariant: distances are expressed in text cap-heights.
"""
import sys

import numpy as np
from PIL import Image


def box_blur(a, r):
    """Separable box blur with a (2r+1) window, edge clamped."""
    k = 2 * r + 1
    pad = np.pad(a, ((r, r), (r, r)), mode="edge")
    c = np.cumsum(pad, axis=0)
    c = np.vstack([np.zeros((1, c.shape[1])), c])
    a2 = (c[k:] - c[:-k]) / k
    c = np.cumsum(a2, axis=1)
    c = np.hstack([np.zeros((c.shape[0], 1)), c])
    return (c[:, k:] - c[:, :-k]) / k


def main(path):
    img = Image.open(path).convert("RGB")
    a = np.asarray(img).astype(np.float32) / 255.0
    h, w, _ = a.shape
    luma = 0.2126 * a[:, :, 0] + 0.7152 * a[:, :, 1] + 0.0722 * a[:, :, 2]
    print(f"image {w}x{h}")

    # --- checker period: scan one row near the top for the transition spacing
    row = luma[2, :]
    d = np.abs(np.diff(row))
    edges = np.where(d > 0.01)[0]
    if len(edges) > 4:
        period = float(np.median(np.diff(edges)))
        print(f"checker period ~ {period:.1f} px ({len(edges)} edges)")
    else:
        period = 16.0
        print("checker period not detected, assuming 16 px")

    # --- text mask: clearly brighter than any checker grey
    thr = np.percentile(luma, 99.5)
    text = luma > max(0.75, thr * 0.9)
    ys, xs = np.nonzero(text)
    print(f"text bbox x {xs.min()}..{xs.max()}  y {ys.min()}..{ys.max()}  "
          f"({text.sum()} px, cap height {ys.max() - ys.min()})")
    cap = float(ys.max() - ys.min())

    # --- remove the checker, keep the glow
    r = int(max(2.0 * period, 8))
    low = box_blur(luma, r)

    # background = the mode of the low-passed field far from the text
    far = low.copy()
    yy, xx = np.mgrid[0:h, 0:w]
    cx, cy = (xs.min() + xs.max()) / 2, (ys.min() + ys.max()) / 2
    dist = np.hypot((xx - cx) / cap, (yy - cy) / cap)
    bg = float(np.median(low[dist > 12]))
    print(f"background level {bg:.4f}")

    glow = low - bg
    glow[text] = np.nan

    peak = float(np.nanpercentile(glow, 99.9))
    print(f"peak glow (above bg) {peak:.4f}")

    # --- radial profile in cap-height units
    print("\n dist/cap   glow/peak   glow")
    for d0 in [0, 0.5, 1, 1.5, 2, 3, 4, 6, 8, 12, 18, 26, 40]:
        m = (dist >= d0) & (dist < d0 + (0.5 if d0 < 2 else (1 if d0 < 6 else 4)))
        if not np.any(m):
            continue
        v = float(np.nanmean(glow[m]))
        print(f"   {d0:5.1f}      {v / peak:7.4f}   {v:8.5f}")

    # --- half-max radius (scale invariant) and frame coverage
    rad = np.nanmax(np.where(glow > 0.5 * peak, dist, 0))
    print(f"\nhalf-max radius  {rad:.2f} cap heights")
    print(f"glow > 5% of peak extends to "
          f"{np.nanmax(np.where(glow > 0.05 * peak, dist, 0)):.2f} cap heights")
    print(f"frame is {h / cap:.1f} cap heights tall, {w / cap:.1f} wide")

    out = np.clip(glow / max(peak, 1e-6), 0, 1)
    Image.fromarray((np.nan_to_num(out) * 255).astype(np.uint8)).save(
        path.replace(".png", "_glowfield.png"))
    print(f"\nwrote {path.replace('.png', '_glowfield.png')}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else r"X:\coding\DeepGlow\smoke_out\user_shot.png")
