import re
lines=open("disasm.txt",encoding="utf-8",errors="ignore").read().splitlines()
def dump(start,count):
    out=[]
    on=False
    for ln in lines:
        if ln.startswith(start): on=True
        if on:
            out.append(ln)
            if len(out)>=count: break
    return out
for ln in dump("1800151c0",70): print(ln)
