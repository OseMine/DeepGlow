"""fnrange.py <lo-hex> <hi-hex> -- list .pdata functions starting in [lo,hi)."""
import json, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
F = json.load(open(os.path.join(HERE, "..", "work", "funcs.json")))
lo, hi = int(sys.argv[1], 16), int(sys.argv[2], 16)
rows = sorted((f["begin"], f["end"]) for f in F["funcs"])
for b, e in rows:
    if lo <= b < hi:
        print(f"{b:08x}-{e:08x} size={e-b}")
