"""Print every string referenced inside a function, in address order (function outline)."""
import json, os, sys, bisect
HERE = os.path.dirname(os.path.abspath(__file__))
WORK = os.path.join(HERE, "..", "work")
F = json.load(open(os.path.join(WORK, "funcs.json")))
A = json.load(open(os.path.join(WORK, "analysis.json")))
IB = F["imagebase"]

strs = {}
for line in open(os.path.join(WORK, "strings_dg2.txt"), encoding="utf-8"):
    p = line.rstrip("\n").split("\t")
    if len(p) >= 3:
        strs[int(p[0], 16)] = p[2]

# refs as (insn_rva, target_rva)
pairs = []
for k, lst in A["refs"].items():
    t = int(k, 16) - IB
    for x in lst:
        pairs.append((int(x, 16) - IB, t))
pairs.sort()

def outline(begin, end, only_strings=True, multiline=True):
    out = []
    i = bisect.bisect_left(pairs, (begin, -1))
    while i < len(pairs) and pairs[i][0] < end:
        insn, t = pairs[i]
        if t in strs:
            s = strs[t]
            if not multiline:
                s = s.replace("\n", "\\n")
            out.append(f"{insn:06x}  ->{t:06x}  {s[:300]}")
        i += 1
    return out

if __name__ == "__main__":
    b = int(sys.argv[1], 16)
    e = int(sys.argv[2], 16) if len(sys.argv) > 2 else None
    if e is None:
        fs = [f for f in F["funcs"] if f["begin"] == b]
        e = fs[0]["end"] if fs else b + 0x400
    for l in outline(b, e):
        print(l)
