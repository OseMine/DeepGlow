"""Index the DeepGlow2.aex .text section: instruction-level xrefs, call graph,
string references, and address->file-offset mapping."""
import pefile, capstone, json, sys, os, collections

PATH = r"C:\Users\Oskar\Desktop\DeepGlow\DeepGlow2.aex"
OUT = os.path.join(os.path.dirname(__file__), "..", "work", "analysis.json")

pe = pefile.PE(PATH, fast_load=True)
IB = pe.OPTIONAL_HEADER.ImageBase

def sec(name):
    for s in pe.sections:
        if s.Name.rstrip(b"\x00").decode() == name:
            return s
    raise KeyError(name)

text = sec(".text")
tdata = text.get_data()
base = IB + text.VirtualAddress

md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
md.detail = True
md.skipdata = True

insns = {}                 # addr -> (mnemonic, op_str, bytes_len)
call_targets = collections.defaultdict(list)   # caller -> [callee]
jmp_targets = collections.defaultdict(list)
refs = collections.defaultdict(list)           # data target -> [insn addr]
rip_refs = []                                  # (insn_addr, target, kind)
ret_addrs = set()

for ins in md.disasm(tdata, base):
    a = ins.address
    if ins.id == 0:  # skipdata pseudo-instruction
        insns[a] = (".byte", ins.bytes.hex(), ins.size)
        continue
    insns[a] = (ins.mnemonic, ins.op_str, ins.size)
    ops = ins.operands
    # direct call/jmp
    if ins.mnemonic in ("call", "jmp") and ops and ops[0].type == capstone.x86.X86_OP_IMM:
        t = ops[0].imm
        if ins.mnemonic == "call":
            call_targets[a].append(t)
        else:
            jmp_targets[a].append(t)
        refs[t].append(a)
        if ins.mnemonic == "call":
            ret_addrs.add(a + ins.size)
    for op in ops:
        if op.type == capstone.x86.X86_OP_MEM and op.mem.base == capstone.x86.X86_REG_RIP:
            t = a + ins.size + op.mem.disp
            rip_refs.append((a, t, ins.mnemonic))
            refs[t].append(a)
        elif op.type == capstone.x86.X86_OP_IMM and ins.mnemonic in ("mov", "lea"):
            t = op.imm
            if IB <= t < IB + 0x600000:
                rip_refs.append((a, t, "abs"))
                refs[t].append(a)

funcs = sorted({t for v in call_targets.values() for t in v if base <= t < base + len(tdata)})

data = {
    "imagebase": IB,
    "text": {"va": base, "size": len(tdata)},
    "call_targets": {hex(k): [hex(x) for x in v] for k, v in call_targets.items()},
    "refs": {hex(k): [hex(x) for x in v] for k, v in refs.items()},
    "funcs": [hex(f) for f in funcs],
}
json.dump(data, open(OUT, "w"), indent=0)

# also persist raw disassembly text for grepping
with open(os.path.join(os.path.dirname(__file__), "..", "work", "disasm.txt"), "w", encoding="utf-8") as f:
    for ins in md.disasm(tdata, base):
        f.write(f"{ins.address:08x}  {ins.bytes.hex():<28} {ins.mnemonic} {ins.op_str}\n")

print("insns", len(insns), "funcs", len(funcs), "rip_refs", len(rip_refs), "callers", len(call_targets))
