import struct, sys, pefile

BIN = r"C:\Users\Oskar\Desktop\DeepGlow\DeepGlow2.aex"
pe = pefile.PE(BIN, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
secs = [(s.VirtualAddress, s.Misc_VirtualSize, s.PointerToRawData, s.SizeOfRawData) for s in pe.sections]
data = pe.__data__


def off(va):
    rva = va - base
    for vaddr, vsize, raw, rsize in secs:
        if vaddr <= rva < vaddr + max(vsize, rsize):
            return raw + (rva - vaddr)
    return None


def show(va, n=8):
    o = off(va)
    if o is None:
        print(f"0x{va:09x}  <unmapped>")
        return
    b = data[o:o + n]
    f32 = struct.unpack("<f", b[:4])[0]
    f64 = struct.unpack("<d", b[:8])[0] if n >= 8 else None
    i32 = struct.unpack("<i", b[:4])[0]
    print(f"0x{va:09x}  bytes={b.hex()}  f32={f32:<18.9g} f64={f64!r:<24} i32={i32}  ascii={b!r}")


if __name__ == "__main__":
    for arg in sys.argv[1:]:
        if arg.startswith("+"):
            continue
        show(int(arg, 16), 16)
