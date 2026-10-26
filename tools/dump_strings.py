import pefile, sys, re, json

p = sys.argv[1]
pe = pefile.PE(p, fast_load=True)
sections = []
for s in pe.sections:
    name = s.Name.rstrip(b'\x00').decode('latin1')
    sections.append((name, s.PointerToRawData, s.SizeOfRawData, s.VirtualAddress, s.Misc_VirtualSize))

data = open(p, 'rb').read()
results = []
pat_ascii = re.compile(rb'[\x20-\x7e]{5,400}')
pat_wide = re.compile(rb'(?:[\x20-\x7e]\x00){5,400}')
for name, off, size, va, vs in sections:
    if name not in ('.rdata', '.data', '.rsrc', '.text'):
        continue
    blob = data[off:off+size]
    for m in pat_ascii.finditer(blob):
        results.append((va + m.start(), 'a', m.group().decode('latin1')))
    for m in pat_wide.finditer(blob):
        results.append((va + m.start(), 'w', m.group().decode('utf-16-le')))

results.sort()
with open(sys.argv[2], 'w', encoding='utf-8') as f:
    for rva, kind, s in results:
        f.write(f"{rva:08x}\t{kind}\t{s}\n")
print("strings:", len(results))
