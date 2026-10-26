"""Parse .pdata RUNTIME_FUNCTION entries -> exact function table (begin/end RVA)."""
import pefile, struct, json, os, sys

PATH = sys.argv[1] if len(sys.argv) > 1 else r"C:\Users\Oskar\Desktop\DeepGlow\DeepGlow2.aex"
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "work", "funcs.json")
pe = pefile.PE(PATH, fast_load=True)
IB = pe.OPTIONAL_HEADER.ImageBase
s = [x for x in pe.sections if x.Name.rstrip(b"\x00") == b".pdata"][0]
blob = s.get_data()
ents = []
for i in range(0, len(blob) - 11, 12):
    b, e, u = struct.unpack_from("<III", blob, i)
    if b == 0 and e == 0:
        continue
    ents.append((b, e, u))
ents.sort()
json.dump({"imagebase": IB, "funcs": [{"begin": b, "end": e, "unwind": u} for b, e, u in ents]},
          open(OUT, "w"), indent=0)
print("functions:", len(ents))
print("first:", [(hex(b), hex(e)) for b, e, _ in ents[:5]])
print("largest:", sorted(((e - b), hex(b), hex(e)) for b, e, _ in ents)[-8:])
