import pefile, sys, json, hashlib, os, struct

p = sys.argv[1]
pe = pefile.PE(p, fast_load=False)
out = {}
out["file"] = p
out["size"] = os.path.getsize(p)
out["sha256"] = hashlib.sha256(open(p,'rb').read()).hexdigest()
out["machine"] = hex(pe.FILE_HEADER.Machine)
out["timestamp"] = pe.FILE_HEADER.TimeDateStamp
out["dllchars"] = hex(pe.FILE_HEADER.Characteristics)
out["subsystem"] = pe.OPTIONAL_HEADER.Subsystem
out["imagebase"] = hex(pe.OPTIONAL_HEADER.ImageBase)
out["ep"] = hex(pe.OPTIONAL_HEADER.AddressOfEntryPoint)
out["dllname"] = pe.OPTIONAL_HEADER.DllCharacteristics
out["sections"] = [{"name": s.Name.rstrip(b'\x00').decode('latin1'), "va": hex(s.VirtualAddress),
                    "vsize": hex(s.Misc_VirtualSize), "raw": hex(s.SizeOfRawData),
                    "rawptr": hex(s.PointerToRawData), "chars": hex(s.Characteristics)} for s in pe.sections]

if hasattr(pe, "DIRECTORY_ENTRY_EXPORT"):
    ex = pe.DIRECTORY_ENTRY_EXPORT
    out["export_dll"] = ex.name.decode('latin1') if ex.name else None
    out["exports"] = [{"ord": e.ordinal, "name": (e.name.decode('latin1') if e.name else None), "rva": hex(e.address)} for e in ex.symbols]
else:
    out["exports"] = []

imps = []
if hasattr(pe, "DIRECTORY_ENTRY_IMPORT"):
    for d in pe.DIRECTORY_ENTRY_IMPORT:
        fns = []
        for imp in d.imports:
            fns.append(imp.name.decode('latin1') if imp.name else f"ord#{imp.ordinal}")
        imps.append({"dll": d.dll.decode('latin1'), "count": len(fns), "funcs": fns})
out["imports"] = imps

# delay load
dl = []
if hasattr(pe, "DIRECTORY_ENTRY_DELAY_IMPORT"):
    for d in pe.DIRECTORY_ENTRY_DELAY_IMPORT:
        fns = [ (i.name.decode('latin1') if i.name else f"ord#{i.ordinal}") for i in d.imports]
        dl.append({"dll": d.dll.decode('latin1'), "funcs": fns})
out["delay_imports"] = dl

json.dump(out, open(sys.argv[2], "w", encoding="utf-8"), indent=1)
print("secs", len(out["sections"]), "exports", len(out["exports"]), "imports", len(imps), "delay", len(dl))
for s in out["sections"]:
    print(f"  {s['name']:10} va={s['va']:>12} vsize={int(s['vsize'],16):>10} raw={int(s['raw'],16):>10} chars={s['chars']}")
for d in imps:
    print(f"  IMP {d['dll']} ({d['count']})")
for d in dl:
    print(f"  DL  {d['dll']} ({len(d['funcs'])})")
