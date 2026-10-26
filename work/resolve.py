import re
targets = {0x1803dfc48:"360.0",0x1803dfc00:"2.0",0x1803dc730:"1.0"}
rx = re.compile(r"^([0-9a-f]+)\s+([0-9a-f]+)\s+(.*)$")
hits={}
for line in open("disasm.txt", encoding="utf-8", errors="ignore"):
    m=rx.match(line.strip())
    if not m: continue
    addr=int(m.group(1),16); byt=m.group(2); txt=m.group(3)
    mm=re.search(r"rip \+ 0x([0-9a-f]+)", txt)
    if not mm: continue
    nxt=addr+len(byt)//2
    tgt=nxt+int(mm.group(1),16)
    if tgt in targets:
        hits.setdefault(targets[tgt],[]).append((hex(addr),txt))
for k,v in hits.items():
    print(k, len(v))
    for a,t in v[:12]: print("   ",a,t)
