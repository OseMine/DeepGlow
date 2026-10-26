"""Extract contiguous GLSL shader blocks ('// begin shader' .. '// end shader')
from DeepGlow2.aex .rdata, byte-exactly."""
import pefile, re, os, json

PATH = r"C:\Users\Oskar\Desktop\DeepGlow\DeepGlow2.aex"
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "extracted", "shaders")
os.makedirs(OUT, exist_ok=True)

pe = pefile.PE(PATH, fast_load=True)
IB = pe.OPTIONAL_HEADER.ImageBase
s = [x for x in pe.sections if x.Name.rstrip(b"\x00") == b".rdata"][0]
rva = s.VirtualAddress
blob = s.get_data()

marks = []
for m in re.finditer(rb"// begin shader", blob):
    end = blob.find(b"// end shader", m.start())
    if end < 0:
        continue
    end += len(b"// end shader")
    text = blob[m.start():end].split(b"\x00")[0].decode("utf-8", "replace")
    marks.append((rva + m.start(), text))

print("blocks:", len(marks))
for i, (a, t) in enumerate(marks):
    open(os.path.join(OUT, f"shader_{i:02d}_{a:06x}.glsl"), "w", encoding="utf-8", newline="\n").write(t)
    first = [l for l in t.split("\n") if l.strip()][0:2]
    print(f"[{i}] rva={a:06x} lines={t.count(chr(10))+1} chars={len(t)} :: {first}")

json.dump([{"rva": a, "text": t} for a, t in marks], open(os.path.join(OUT, "..", "shaders.json"), "w"), indent=1)
