"""Compact flow view of a function: calls + float math + rip-relative float constants."""
import json, os, sys, struct, bisect
HERE = os.path.dirname(os.path.abspath(__file__))
WORK = os.path.join(HERE, "..", "work")
F = json.load(open(os.path.join(WORK, "funcs.json")))
IB = F["imagebase"]
PE = r"C:\Users\Oskar\Desktop\DeepGlow\DeepGlow2.aex"
raw = open(PE, "rb").read()

import pefile
pe = pefile.PE(PE, fast_load=True)
secs = [(s.VirtualAddress, s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData), s.PointerToRawData, s.Name.rstrip(b"\x00").decode()) for s in pe.sections]

def read_va(va):
    rva = va - IB
    for a, b, p, n in secs:
        if a <= rva < b:
            off = p + (rva - a)
            return raw[off:off + 8]
    return b""

lines = {}
for l in open(os.path.join(WORK, "disasm.txt"), encoding="utf-8"):
    p = l.split("  ", 1)
    lines[int(p[0].strip(), 16)] = p[1].rstrip("\n")

addrs = sorted(lines)

def show(begin, end):
    va0, va1 = IB + begin, IB + end
    i = bisect.bisect_left(addrs, va0)
    out = []
    while i < len(addrs) and addrs[i] < va1:
        a = addrs[i]
        txt = lines[a]
        keep = False
        note = ""
        if "call " in txt:
            keep = True
        for k in ("mulss", "divss", "addss", "subss", "cvtsi2ss", "cvtsi2sd", "mulps", "divps", "comiss", "maxss", "minss", "sqrtss", "roundss"):
            if txt.startswith(k + " ") or (" " + k + " ") in txt:
                keep = True
        if "rip + 0x" in txt and ("ss " in txt or "ps " in txt):
            # decode float constant
            try:
                disp = int(txt.split("rip + 0x")[1].split("]")[0], 16)
                nxt = a + (len(txt.split("   ")[0]) if False else 0)
                # instruction length from bytes column
                pass
            except Exception:
                pass
        if keep:
            out.append(f"{a - IB:06x}  {txt}")
        i += 1
    return out

if __name__ == "__main__":
    b = int(sys.argv[1], 16)
    e = int(sys.argv[2], 16)
    for l in show(b, e):
        print(l)
