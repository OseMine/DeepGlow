"""Locate containing functions (from .pdata) and attribute string xrefs."""
import json, os, sys, bisect

HERE = os.path.dirname(os.path.abspath(__file__))
WORK = os.path.join(HERE, "..", "work")
F = json.load(open(os.path.join(WORK, "funcs.json")))
A = json.load(open(os.path.join(WORK, "analysis.json")))
IB = F["imagebase"]
funcs = [(f["begin"], f["end"]) for f in F["funcs"]]
funcs.sort()
starts = [b for b, e in funcs]

def contain(rva):
    i = bisect.bisect_right(starts, rva) - 1
    hits = []
    while i >= 0 and i > bisect.bisect_right(starts, rva) - 8:
        b, e = funcs[i]
        if b <= rva < e:
            hits.append((b, e))
        i -= 1
    return sorted(hits, key=lambda t: t[1] - t[0])

def xrefs_to(rva):
    v = IB + rva
    out = []
    for k, lst in A["refs"].items():
        if int(k, 16) == v:
            out.extend(int(x, 16) for x in lst)
    return out

if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "fn":
        rva = int(sys.argv[2], 16)
        for b, e in contain(rva):
            print(f"func {b:08x}-{e:08x} size={e-b}")
    elif cmd == "refs":
        rva = int(sys.argv[2], 16)
        xs = xrefs_to(rva)
        print(f"{len(xs)} xrefs to {rva:08x}")
        seen = {}
        for x in sorted(xs):
            hits = contain(x - IB)
            key = hits[0][0] if hits else None
            seen.setdefault(key, []).append(x - IB)
        for k, v in seen.items():
            print(f"  in func {k:08x}: {', '.join(f'{y:08x}' for y in v[:12])}{' ...' if len(v) > 12 else ''}")
