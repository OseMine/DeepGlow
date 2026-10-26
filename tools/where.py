import json, os, sys, bisect
HERE = os.path.dirname(os.path.abspath(__file__))
WORK = os.path.join(HERE, "..", "work")
F = json.load(open(os.path.join(WORK, "funcs.json")))
A = json.load(open(os.path.join(WORK, "analysis.json")))
IB = F["imagebase"]
funcs = sorted((f["begin"], f["end"]) for f in F["funcs"])
starts = [b for b, e in funcs]

def contain(rva):
    i = bisect.bisect_right(starts, rva) - 1
    best = None
    while i >= 0:
        b, e = funcs[i]
        if b <= rva < e:
            if best is None or (e - b) < best[1] - best[0]:
                best = (b, e)
        i -= 1
        if best and rva - funcs[i][0] > 200000:
            break
    return best

strs = {}
for line in open(os.path.join(WORK, "strings_dg2.txt"), encoding="utf-8"):
    p = line.rstrip("\n").split("\t")
    if len(p) >= 3:
        strs[int(p[0], 16)] = p[2]

refmap = {}
for k, lst in A["refs"].items():
    refmap[int(k, 16) - IB] = [int(x, 16) - IB for x in lst]

def where(name):
    hits = [(r, s) for r, s in strs.items() if s.strip() == name]
    out = []
    for r, s in hits:
        for x in refmap.get(r, []):
            c = contain(x)
            out.append((c[0] if c else None, x, r))
    return out

names = sys.argv[1:]
for n in names:
    res = where(n)
    print(f"### {n}")
    for c, x, r in res:
        print(f"   str@{r:06x} ref@{x:06x} func={c:08x}" if c else f"   str@{r:06x} ref@{x:06x} func=?")
    if not res:
        print("   (no xref)")
