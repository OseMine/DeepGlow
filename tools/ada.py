"""Ad-hoc analysis helper for DeepGlow2.aex.
usage:
  ada.py xref <rva-hex>            xrefs to a data/code rva
  ada.py str <rva-hex> [n]         strings near rva
  ada.py dis <rva-hex> [len]       disassemble
  ada.py callers <rva-hex>         callers of a function rva
  ada.py find <substring>          string search
"""
import sys, json, os, bisect

HERE = os.path.dirname(os.path.abspath(__file__))
WORK = os.path.join(HERE, "..", "work")
A = json.load(open(os.path.join(WORK, "analysis.json")))
IB = A["imagebase"]
TEXT_VA = A["text"]["va"]
TEXT_SIZE = A["text"]["size"]

strings = []
for line in open(os.path.join(WORK, "strings_dg2.txt"), encoding="utf-8"):
    p = line.rstrip("\n").split("\t")
    if len(p) >= 3:
        strings.append((int(p[0], 16), p[1], p[2]))
strings.sort()
saddr = [s[0] for s in strings]

dis = []
for line in open(os.path.join(WORK, "disasm.txt"), encoding="utf-8"):
    parts = line.split("  ", 1)
    a = int(parts[0].strip(), 16)
    dis.append((a, (parts[1] if len(parts) > 1 else "").rstrip("\n")))
daddr = [d[0] for d in dis]

def rva_of(va):
    return va - IB

def va_of(rva):
    return rva + IB

def dis_range(rva, ln=400):
    va = va_of(rva)
    i = bisect.bisect_left(daddr, va)
    out = []
    end = va + ln
    while i < len(dis) and dis[i][0] < end:
        out.append(f"{rva_of(dis[i][0]):08x}  {dis[i][1]}")
        i += 1
    return "\n".join(out)

def str_at(rva):
    va = va_of(rva)
    i = bisect.bisect_left(saddr, va)
    if i < len(strings) and strings[i][0] == va:
        return strings[i][2]
    return None

def xrefs(rva):
    return A["refs"].get(f"{va_of(rva):x}") or A["refs"].get(hex(va_of(rva)), [])

if __name__ == "__main__":
    cmd = sys.argv[1]
    if cmd == "xref":
        rva = int(sys.argv[2], 16)
        xs = xrefs(rva)
        print(f"refs to {rva:08x}: {len(xs)}")
        for x in xs:
            print(" ", "rva", hex(rva_of(int(x, 16))), "|", dis_range(rva_of(int(x,16)), 6).split("\n")[0])
    elif cmd == "str":
        rva = int(sys.argv[2], 16)
        n = int(sys.argv[3]) if len(sys.argv) > 3 else 6
        va = va_of(rva)
        i = bisect.bisect_left(saddr, va)
        i = max(0, i - 2)
        for k in range(i, min(len(strings), i + n)):
            print(f"{strings[k][0] - IB:08x} {strings[k][1]} {strings[k][2][:220]}")
    elif cmd == "dis":
        rva = int(sys.argv[2], 16)
        ln = int(sys.argv[3], 16) if len(sys.argv) > 3 else 400
        print(dis_range(rva, ln))
    elif cmd == "callers":
        target = va_of(int(sys.argv[2], 16))
        found = []
        for k, v in A["call_targets"].items():
            if str(hex(target)) in v or target in [int(x,16) for x in v]:
                found.append(int(k, 16))
        print(f"callers of {int(sys.argv[2],16):08x}: {len(found)}")
        for f in found:
            print(f"  rva {rva_of(f):08x}")
    elif cmd == "find":
        sub = sys.argv[2].lower()
        for a, kind, s in strings:
            if sub in s.lower():
                print(f"{a-IB:08x} {kind} {s[:200]}")
