"""Extract AE param definition blocks from the DeepGlow2 custom-UI builder.

The builder fills a 0xb0-byte PF_ParamDef at [rbp+0x4e0] for every parameter,
copying the human name into the PF_Str at [rbp+0x4f0] first.  We anchor on that
copy, then decode the immediate stores that follow.
"""
import bisect
import json
import os
import re
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
secs = [(s.VirtualAddress, s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData),
         s.PointerToRawData) for s in pe.sections]

def read_va(va, n=8):
    rva = va - IB
    for a, b, p in secs:
        if a <= rva < b:
            return raw[p + (rva - a):p + (rva - a) + n]
    return b""

strs = {}
for line in open(os.path.join(WORK, "strings_dg2.txt"), encoding="utf-8"):
    parts = line.rstrip("\n").split("\t")
    if len(parts) >= 3:
        try:
            strs[int(parts[0], 16) + IB] = parts[2]
        except ValueError:
            pass
saddr = sorted(strs)

def str_at(va):
    i = bisect.bisect_right(saddr, va) - 1
    if i >= 0 and va - saddr[i] <= 0:
        return strs[saddr[i]]
    return None

LINE_RE = re.compile(r"^([0-9a-f]+)\s\s([0-9a-f]+)\s+(.*)$")
insns = []
for line in open(os.path.join(WORK, "disasm.txt"), encoding="utf-8"):
    m = LINE_RE.match(line.rstrip("\n"))
    if m:
        insns.append((int(m.group(1), 16), m.group(2), m.group(3).strip()))
iaddr = [i[0] for i in insns]

FLOATS = {0x518: "f64", 0x520: None}
TYPES = {
    0xa: "FLOAT_SLIDER", 0x9: "ANGLE", 0x6: "COLOR", 0x7: "POINT",
    0x8: "POPUP", 0x18: "POPUP", 0x5: "CHECKBOX", 0x2: "SLIDER",
    0x1: "BUTTON", 0x3: "COLOR", 0x4: "LAYER", 0xb: "GROUP",
}

def decode(imm_hex, n):
    b = bytes.fromhex(imm_hex)
    if n == 4 and len(b) >= 4:
        return struct.unpack("<f", b[:4])[0]
    if n == 8 and len(b) >= 8:
        return struct.unpack("<d", b[:8])[0]
    return int.from_bytes(b, "little")

def main():
    lo, hi = 0x28b190, 0x290eeb
    anchors = []
    for i, (a, code, txt) in enumerate(insns):
        if not (IB + lo <= a < IB + hi):
            continue
        if txt.startswith("lea rcx, [rbp + 0x4f0]"):
            # previous instruction should be lea rdx,[rip+..] with the label
            label = None
            for j in range(i - 1, max(0, i - 4), -1):
                t = insns[j][2]
                if t.startswith("lea rdx, [rip + 0x"):
                    disp = int(t.split("0x")[-1].rstrip("]"), 16)
                    tgt = insns[j][0] + len(insns[j][1]) // 2 + disp
                    s = str_at(tgt)
                    if s:
                        label = s
                    break
            anchors.append((i, a, label))
    for k, (i, a, label) in enumerate(anchors):
        nxt = anchors[k + 1][0] if k + 1 < len(anchors) else i + 80
        nxt = min(nxt, i + 90)
        info = []
        for j in range(i, nxt):
            addr, code, txt = insns[j]
            m = re.search(r"dword ptr \[rbp \+ (0x[0-9a-f]+)\], (?:0x)?([0-9a-f]+)$", txt)
            if m:
                off = int(m.group(1), 16) - 0x4e0
                val = int(m.group(2), 16)
                info.append(f"+{off:#04x}=0x{val:x}")
                continue
            m = re.search(r"qword ptr \[rbp \+ (0x[0-9a-f]+)\], (?:0x)?([0-9a-f]+)$", txt)
            if m:
                off = int(m.group(1), 16) - 0x4e0
                val = int(m.group(2), 16)
                info.append(f"+{off:#04x}={(val & 0xffffffff):#x}/{val >> 32:#x}")
                continue
            m = re.search(r"\[rbp \+ (0x[0-9a-f]+)\], xmm", txt)
            if m and "movss" in txt:
                off = int(m.group(1), 16) - 0x4e0
                info.append(f"+{off:#04x}=<xmm>")
                continue
            if txt.startswith("lea rax, [rip + 0x"):
                disp = int(txt.split("0x")[-1].rstrip("]"), 16)
                tgt = addr + len(code) // 2 + disp
                s = str_at(tgt)
                if s and "mov qword ptr [rbp" in insns[j - 1][2]:
                    info.append(f'str="{s}"')
        print(f"{a - IB:06x}  {label}")
        if info:
            print("        " + "  ".join(info))

if __name__ == "__main__":
    main()
