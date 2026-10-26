"""Decode DeepGlow2 param-def blocks (0xb0 PF_ParamDef at [rbp+0x4e0]) into a table.

Walks the custom-UI builder, tracks rip-relative constants loaded into xmm,
and prints one row per parameter: id, label, type, default, min, max.
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

def read_va(va, n=16):
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
    if i >= 0 and va == saddr[i]:
        return strs[saddr[i]]
    return None

LINE_RE = re.compile(r"^([0-9a-f]+)\s\s([0-9a-f]+)\s+(.*)$")
insns = []
for line in open(os.path.join(WORK, "disasm.txt"), encoding="utf-8"):
    m = LINE_RE.match(line.rstrip("\n"))
    if m:
        insns.append((int(m.group(1), 16), m.group(2), m.group(3).strip()))

BASE = 0x4e0
LO, HI = 0x28b190, 0x290eeb
TYPES = {0xa: "float", 0xf: "float", 0x4: "color", 0x7: "popup", 0x8: "popup",
         0xd: "group", 0xe: "text", 0x6: "layer", 0x5: "layer", 0x9: "angle",
         0x3: "color", 0xc: "button"}

def main():
    a = bytes.fromhex(insns[0][1])
    xmm = {}          # xmm name -> float/double value
    xmmd = {}
    strings = {}
    params = []
    cur = None
    for i, (addr, code, txt) in enumerate(insns):
        if not (IB + LO <= addr < IB + HI):
            continue
        ln = len(code) // 2
        m = re.match(r"movss xmm(\d+), dword ptr \[rip \+ (0x[0-9a-f]+)\]", txt)
        if m:
            disp = int(m.group(2), 16)
            b = read_va(addr + ln + disp, 4)
            if len(b) == 4:
                xmm[int(m.group(1))] = struct.unpack("<f", b)[0]
            continue
        m = re.match(r"movsd xmm(\d+), qword ptr \[rip \+ (0x[0-9a-f]+)\]", txt)
        if m:
            disp = int(m.group(2), 16)
            b = read_va(addr + ln + disp, 8)
            if len(b) == 8:
                xmmd[int(m.group(1))] = struct.unpack("<d", b)[0]
            continue
        m = re.match(r"lea rdx, \[rip \+ (0x[0-9a-f]+)\]", txt)
        if m:
            disp = int(m.group(1), 16)
            s = str_at(addr + ln + disp)
            if s:
                # only a label if the very next instruction copies it into the name field
                nxt = insns[i + 1][2] if i + 1 < len(insns) else ""
                if "lea rcx, [rbp + 0x4f0]" in nxt:
                    strings["label"] = s
            continue
        m = re.match(r"lea rax, \[rip \+ (0x[0-9a-f]+)\]", txt)
        if m:
            disp = int(m.group(1), 16)
            s = str_at(addr + ln + disp)
            if s:
                strings["items"] = s
            continue
        # struct field stores
        m = re.match(r"mov dword ptr \[rbp \+ (0x[0-9a-f]+)\], (0x[0-9a-f]+)$", txt)
        if m:
            off = int(m.group(1), 16) - BASE
            val = int(m.group(2), 16)
            if cur is None:
                cur = {}
            cur[off] = ("i", val)
            continue
        m = re.match(r"mov qword ptr \[rbp \+ (0x[0-9a-f]+)\], (0x[0-9a-f]+)$", txt)
        if m:
            off = int(m.group(1), 16) - BASE
            val = int(m.group(2), 16)
            if cur is None:
                cur = {}
            cur[off] = ("q", val)
            continue
        m = re.match(r"mov dword ptr \[rbp \+ (0x[0-9a-f]+)\], e(si|bx|ax|dx|cx|di)$", txt)
        if m and cur is not None:
            pass
        m = re.match(r"movss dword ptr \[rbp \+ (0x[0-9a-f]+)\], xmm(\d+)$", txt)
        if m:
            off = int(m.group(1), 16) - BASE
            n = int(m.group(2))
            if cur is None:
                cur = {}
            cur[off] = ("f", xmm.get(n))
            continue
        m = re.match(r"movsd qword ptr \[rbp \+ (0x[0-9a-f]+)\], xmm(\d+)$", txt)
        if m:
            off = int(m.group(1), 16) - BASE
            n = int(m.group(2))
            if cur is None:
                cur = {}
            cur[off] = ("d", xmmd.get(n))
            continue
        m = re.match(r"mov qword ptr \[rbp \+ (0x[0-9a-f]+)\], rax$", txt)
        if m:
            off = int(m.group(1), 16) - BASE
            if cur is None:
                cur = {}
            cur[off] = ("items", strings.get("items", ""))
            continue
        # add-param call site: after this, reset
        if txt.startswith("call rax") and cur is not None and 0x00 in cur:
            entry = {"label": strings.get("label", ""), "fields": dict(cur)}
            params.append(entry)
            cur = None
            strings = {}
            continue

    for p in params:
        f = p["fields"]
        def g(k, d=None):
            v = f.get(k)
            return v[1] if v else d
        label = p["label"]
        pid = g(0x00)
        ptype = g(0x0c)
        default = g(0x38)
        def fl(k):
            v = f.get(k)
            if not v:
                return None
            kind, val = v
            if isinstance(val, float):
                return val
            if kind == "i":
                b = val.to_bytes(4, "little")
                return struct.unpack("<f", b)[0]
            return val
        vmin, vmax = fl(0x68), fl(0x6c)
        smin, smax = fl(0x70), fl(0x74)
        items = g(0x40)
        def fmt(x):
            if isinstance(x, float):
                return f"{x:g}"
            return str(x)
        print(f"id=0x{pid:02x} type={TYPES.get(ptype, ptype)} {label!r} "
              f"default={fmt(default)} valid=[{fmt(vmin)},{fmt(vmax)}] "
              f"slider=[{fmt(smin)},{fmt(smax)}] items={items!r}")

if __name__ == "__main__":
    main()
