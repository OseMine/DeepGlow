import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_64
from capstone.x86 import X86_OP_MEM, X86_REG_RIP

BIN = r"C:\Users\Oskar\Desktop\DeepGlow\DeepGlow2.aex"
pe = pefile.PE(BIN)
base = pe.OPTIONAL_HEADER.ImageBase
text = pe.sections[0]
code = text.get_data()
tva = base + text.VirtualAddress

md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True
md.skipdata = True

targets = {}
for name, va in {
    "STEPS": 0x1803dbf78,
    "fadeMultiplier": 0x1803dbf80,
    "bypassB": 0x1803dbf90,
    "isBlurWidthB": 0x1803dbf98,
    "isAngleB": 0x1803dbfa8,
    "strideX": 0x1803dbfb8,
    "strideY": 0x1803dbfc0,
    "is_glow_aberrationB": 0x1803dbfc8,
    "redMultF": 0x1803dbfe0,
    "greenMultF": 0x1803dbff0,
    "blueMultF": 0x1803dc000,
}.items():
    targets[va] = name

hits = []
count = 0
for ins in md.disasm(code, tva):
    count += 1
    try:
        ops = ins.operands
    except Exception:
        continue
    if not ops:
        continue
    for op in ops:
        if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
            tgt = ins.address + ins.size + op.mem.disp
            if tgt in targets:
                hits.append((ins.address, targets[tgt], f"{ins.mnemonic} {ins.op_str}"))

print("decoded", count)
for a, n, s in hits:
    print(f"0x{a - base:x}  {n:<22} {s}")
print("total", len(hits))
