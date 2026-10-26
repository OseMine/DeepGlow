"""Readable linear flow of a function with resolved rip-relative data, floats, strings and call targets.

usage: flow.py <begin-rva-hex> <end-rva-hex> [--all]
  Without --all only interesting instructions are shown (calls, float math, rip refs, branches).
"""
import bisect
import json
import os
import struct
import sys

import pefile

HERE = os.path.dirname(os.path.abspath(__file__))
WORK = os.path.join(HERE, "..", "work")
F = json.load(open(os.path.join(WORK, "funcs.json")))
IB = F["imagebase"]
PE = r"C:\Users\Oskar\Desktop\DeepGlow\DeepGlow2.aex"
raw = open(PE, "rb").read()
pe = pefile.PE(PE, fast_load=True)
secs = []
for s in pe.sections:
    secs.append((s.VirtualAddress,
                 s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData),
                 s.PointerToRawData,
                 s.PointerToRawData + s.SizeOfRawData,
                 s.Name.rstrip(b"\x00").decode()))

def sec_of_rva(rva):
    for a, b, p, pe_, n in secs:
        if a <= rva < b:
            return n, p + (rva - a)
    return None, None

def read_va(va, n=32):
    rva = va - IB
    nme, off = sec_of_rva(rva)
    if off is None:
        return b""
    return raw[off:off + n]

# strings
strs = {}
for line in open(os.path.join(WORK, "strings_dg2.txt"), encoding="utf-8"):
    p = line.rstrip("\n").split("\t")
    if len(p) >= 3:
        try:
            strs[int(p[0], 16) + IB] = p[2]
        except ValueError:
            pass
saddr = sorted(strs)

def str_near(va, window=0):
    i = bisect.bisect_right(saddr, va) - 1
    if i >= 0 and va - saddr[i] <= window:
        return strs[saddr[i]]
    return None

import re
LINE_RE = re.compile(r"^([0-9a-f]+)\s\s([0-9a-f]+)\s+(.*)$")
insns = []
for line in open(os.path.join(WORK, "disasm.txt"), encoding="utf-8"):
    m = LINE_RE.match(line.rstrip("\n"))
    if not m:
        continue
    insns.append((int(m.group(1), 16), m.group(2), m.group(3).strip()))
iaddr = [i[0] for i in insns]

FLOAT_OPS = ("mulss", "divss", "addss", "subss", "minss", "maxss", "sqrtss",
             "cvtss2sd", "cvtsi2ss", "cvttss2si", "comiss", "ucomiss",
             "mulps", "addps", "subps", "divps", "roundss", "andps", "orps")
BRANCH = ("jmp", "je ", "jne ", "jz ", "jnz ", "jb ", "ja ", "jbe ", "jae ",
          "jl ", "jg ", "jle ", "jge ", "js ", "jns ", "jp ", "jnp ")

def fancy_float(b):
    if len(b) < 4:
        return None
    (f,) = struct.unpack("<f", b[:4])
    if f != f or abs(f) == float("inf"):
        return None
    if f == 0.0:
        return None
    if 1e-8 <= abs(f) <= 1e7:
        # only report if it has a short decimal repr or looks authored
        for prec in (1, 2, 3, 4, 6):
            s = f"{f:.{prec}g}"
            if abs(float(s) - f) <= abs(f) * 1e-6:
                return float(s)
    return None

def decode_target(va, ln):
    b = read_va(va, 64)
    if not b:
        return ""
    notes = []
    s = str_near(va)
    if s is not None:
        notes.append(f'"{s}"')
    for n, tag in ((4, "f32"), (8, "f64")):
        v = fancy_float(b[:n]) if n == 4 else None
        if n == 8:
            (d,) = struct.unpack("<d", b[:8])
            if 1e-8 <= abs(d) <= 1e7 and d == d:
                for prec in (1, 2, 3, 4, 6):
                    t = f"{d:.{prec}g}"
                    if abs(float(t) - d) <= abs(d) * 1e-9:
                        v = float(t)
                        break
        if v is not None:
            notes.append(f"{tag}={v}")
    # ivec2/vec2 pairs
    if len(b) >= 8:
        (x,) = struct.unpack("<i", b[:4])
        (y,) = struct.unpack("<i", b[4:8])
        if 0 <= x <= 8192 and 0 <= y <= 8192 and (x or y):
            notes.append(f"ivec2=({x},{y})")
    return "  ; " + " ".join(notes) if notes else ""

def main():
    begin = int(sys.argv[1], 16)
    end = int(sys.argv[2], 16)
    show_all = "--all" in sys.argv
    va0, va1 = IB + begin, IB + end
    i = bisect.bisect_left(iaddr, va0)
    while i < len(insns) and insns[i][0] < va1:
        a, code, txt = insns[i]
        ln = len(code) // 2 if code else 0
        keep = show_all
        note = ""
        if txt.startswith("call") or txt.startswith("jmp") and "[" not in txt:
            if txt.startswith("call"):
                pass
            tgt = txt.split()[-1]
            if tgt.startswith("0x") and ln:
                try:
                    tv = int(tgt, 16)
                except ValueError:
                    print(f"{a - IB:08x}  {txt}")
                    i += 1
                    continue
                if tv >= IB:
                    note = f"  -> {tv - IB:08x}"
                else:
                    note = f"  -> {a + ln + tv - IB:08x}"
                keep = True
            keep = True
        if "rip + 0x" in txt:
            try:
                disp = int(txt.split("rip + 0x")[1].split("]")[0], 16)
                tgt = a + ln + disp
                note += decode_target(tgt, ln)
                keep = True
            except Exception:
                pass
        for op in FLOAT_OPS:
            if txt.startswith(op + " "):
                keep = True
        for op in BRANCH:
            if txt.startswith(op):
                keep = True
        if "lea " in txt and "rip" not in txt:
            pass
        if keep:
            print(f"{a - IB:08x}  {txt}{note}")
        i += 1

if __name__ == "__main__":
    main()
